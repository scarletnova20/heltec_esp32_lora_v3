#include "WebDashboard.h"
#include "DashboardPage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <stdlib.h>
namespace bridge {
// All network objects, JSON encoding and socket writes belong exclusively to the web task.
// The radio loop interacts only through bounded zero-wait queues.
bool WebDashboard::begin(uint32_t id) {
  snapshots = xQueueCreateStatic(1, sizeof(Snapshot), snapshotStorage, &snapshotControl);
  commands = xQueueCreateStatic(8, sizeof(Command), commandStorage, &commandControl);
  char ssid[32]; snprintf(ssid, sizeof(ssid), "LoRa-Bridge-%08lX", (unsigned long)id);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, ApPassword, 1, false, 4)) return false;
  return xTaskCreatePinnedToCore(task, "bridge-web", 16384, this, 1, nullptr, 0) == pdPASS;
}
void WebDashboard::task(void* arg) { static_cast<WebDashboard*>(arg)->run(); }
static int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static bool number(const char* s, uint32_t& value) {
  if (!*s) return false;
  uint64_t v = 0;
  while (*s) { if (*s < '0' || *s > '9') return false; v = v * 10 + (*s++ - '0'); if (v > UINT32_MAX) return false; }
  value = v; return true;
}
void WebDashboard::run() {
  WebServer http(80); WebSocketsServer ws(81);
  http.on("/", HTTP_GET, [&]() { http.send_P(200, "text/html; charset=utf-8", DashboardPage); });
  http.onNotFound([&]() { http.send(404, "text/plain", "Not found"); });
  ws.onEvent([&](uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
      if (length != 3 || memcmp(payload, "/ws", 3)) ws.disconnect(client);
      return;
    }
    if (type != WStype_TEXT) return;
    Command c; bool valid = false;
    char input[420]{};
    if (length && length < sizeof(input) && !memchr(payload, 0, length)) {
      memcpy(input, payload, length);
      if (!strncmp(input, "send:", 5)) {
        size_t n = length - 5; valid = n && n % 2 == 0 && n / 2 <= PayloadMax;
        if (valid) { c.type = CommandType::Send; c.length = n / 2;
          for (size_t i = 0; i < c.length; ++i) { int a = nibble(input[5+i*2]), b = nibble(input[6+i*2]);
            if (a < 0 || b < 0) { valid = false; break; } c.data[i] = uint8_t(a*16+b); }
        }
      } else if (!strncmp(input, "pair:", 5)) { c.type = CommandType::Pair; valid = number(input+5, c.value) && c.value; }
      else if (!strcmp(input, "unpair")) { c.type = CommandType::Unpair; valid = true; }
      else if (!strncmp(input, "interface:", 10)) { c.type = CommandType::Interface; valid = number(input+10, c.value) && c.value <= 2; }
      else if (!strncmp(input, "settings:", 9)) {
        c.type = CommandType::Settings; char* comma = strchr(input+9, ',');
        if (comma) { *comma = 0; valid = number(input+9, c.value) && c.value <= 2 && number(comma+1, c.baud) && validBaud(c.baud); }
      }
    }
    const bool queued = valid && xQueueSend(commands, &c, 0) == pdTRUE;
    ws.sendTXT(client, queued ? "{\"type\":\"result\",\"message\":\"Queued for firmware validation; watch status.\"}" : "{\"type\":\"result\",\"message\":\"Rejected: invalid command or command queue full.\"}");
  });
  http.begin(); ws.begin(); ws.enableHeartbeat(10000, 3000, 2);
  Snapshot snapshot; PayloadEvent event; char json[3600];
  for (;;) {
    http.handleClient(); ws.loop();
    // Limit each pass so command input and connection housekeeping also run.
    for (unsigned i = 0; i < 8 && log.pop(event); ++i) {
      char hex[PayloadMax*2+1]; const char* digits = "0123456789ABCDEF";
      for (size_t j = 0; j < event.length; ++j) { hex[j*2] = digits[event.data[j] >> 4]; hex[j*2+1] = digits[event.data[j] & 15]; }
      hex[event.length*2] = 0;
      snprintf(json, sizeof(json), "{\"type\":\"payload\",\"direction\":\"%s\",\"timestamp\":%lu,\"length\":%u,\"hex\":\"%s\"}", event.tx ? "TX" : "RX", (unsigned long)event.timestamp, event.length, hex);
      ws.broadcastTXT(json);
    }
    if (xQueueReceive(snapshots, &snapshot, 0) == pdTRUE) {
      auto& s = snapshot; auto& c = s.counters;
      // All status strings are fixed firmware literals, never payload/user strings.
      size_t used = snprintf(json, sizeof(json),
        "{\"type\":\"stats\",\"local\":%lu,\"peer\":%lu,\"now\":%lu,\"lastPacket\":%lu,\"linkSince\":%lu,"
        "\"baud\":%lu,\"preset\":%u,\"interfaceMode\":%u,\"activeInterface\":%u,\"linked\":%s,\"seenPacket\":%s,\"settingsPending\":%s,"
        "\"txPackets\":%lu,\"rxPackets\":%lu,\"txBytes\":%lu,\"rxBytes\":%lu,\"acknowledged\":%lu,\"retries\":%lu,\"duplicates\":%lu,"
        "\"failed\":%lu,\"failedBytes\":%lu,\"malformed\":%lu,\"radioErrors\":%lu,\"backpressure\":%lu,\"logDrops\":%lu,\"serialOverflows\":%lu,\"commandRejects\":%lu,"
        "\"txRate\":%lu,\"rxRate\":%lu,\"txQueued\":%u,\"rxQueued\":%u,\"logQueued\":%u,\"rssi\":%.1f,\"snr\":%.1f,\"status\":\"%s\",\"nodes\":[",
        (unsigned long)s.local, (unsigned long)s.peer, (unsigned long)s.now, (unsigned long)s.lastPacket, (unsigned long)s.linkSince,
        (unsigned long)s.baud, s.preset, s.interfaceMode, s.activeInterface, s.linked?"true":"false", s.seenPacket?"true":"false", s.settingsPending?"true":"false",
        (unsigned long)c.txPackets,(unsigned long)c.rxPackets,(unsigned long)c.txBytes,(unsigned long)c.rxBytes,(unsigned long)c.acknowledged,(unsigned long)c.retries,(unsigned long)c.duplicates,
        (unsigned long)c.failed,(unsigned long)c.failedBytes,(unsigned long)c.malformed,(unsigned long)c.radioErrors,(unsigned long)c.backpressure,(unsigned long)c.logDrops,(unsigned long)c.serialOverflows,(unsigned long)c.commandRejects,
        (unsigned long)s.txRate,(unsigned long)s.rxRate,s.txQueued,s.rxQueued,s.logQueued,s.rssi,s.snr,s.status);
      bool first = true;
      for (const auto& n : s.nodes) if (n.id && used < sizeof(json)) {
        used += snprintf(json+used, sizeof(json)-used, "%s{\"id\":%lu,\"lastHeard\":%lu,\"master\":%s,\"peer\":%lu,\"rssi\":%.1f,\"snr\":%.1f}", first?"":",",(unsigned long)n.id,(unsigned long)n.lastHeard,n.master?"true":"false",(unsigned long)n.peer,n.rssi,n.snr);
        first = false;
      }
      if (used + 3 < sizeof(json)) { memcpy(json+used, "]}", 3); ws.broadcastTXT(json); }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
}
