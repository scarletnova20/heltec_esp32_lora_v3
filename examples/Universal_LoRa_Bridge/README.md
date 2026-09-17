# Universal bidirectional LoRa bridge

One firmware image for two **Heltec WiFi LoRa 32 V3 / SX1262** boards. This application uses the repository's radio, OLED and button instances without modifying the upstream library. It transports opaque bytes between the on-board USB serial connection or a separate hardware UART on each board. It does not interpret MAVLink or other serial protocols.

**OLED power on V3.2:** the application enables Vext (GPIO 36, active LOW) before calling `heltec_setup()`. V3.2 powers the OLED through this rail; enabling it after display initialization is too late. Startup stages appear on the OLED before radio and Wi-Fi initialization. See the [upstream hardware-revision report](https://github.com/ropg/heltec_esp32_lora_v3/issues/84). This also enables the external Vext output while the bridge runs.

## Build and flash

Dependencies used by the application:

| Component | Build version |
| --- | --- |
| Arduino ESP32 core | 3.3.0 |
| RadioLib | 7.2.1 |
| WebSockets by Markus Sattler / Links2004 | 2.7.0 |
| HotButton | 0.1.1 |
| ESP8266 and ESP32 OLED driver for SSD1306 displays | 4.6.2 |
| Heltec_ESP32_LoRa_v3 | This checkout |

From the repository root, using Arduino CLI:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.0 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install RadioLib@7.2.1 HotButton@0.1.1 "ESP8266 and ESP32 OLED driver for SSD1306 displays@4.6.2" WebSockets@2.7.0
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --library . examples/Universal_LoRa_Bridge
arduino-cli upload -p YOUR_PORT --fqbn esp32:esp32:heltec_wifi_lora_32_V3 examples/Universal_LoRa_Bridge
```

For Arduino IDE, install this repository as a library, open `Universal_LoRa_Bridge.ino`, select **Heltec WiFi LoRa 32(V3)**, disable **USB CDC On Boot**, and set **Core Debug Level: None**. The V3 board's USB connector uses its USB-to-UART chip; this application deliberately uses that `HardwareSerial` port, not native ESP32 USB CDC. Flash the **same compiled image** to both boards.

`BridgeConfig.h` contains the common RF frequency (default 866.3 MHz), power (default 0 dBm), pin assignments, buffer limits and presets. Choose frequency, power and operating duty cycle appropriate for your deployment before use. This application does not implement a regulatory duty-cycle limiter.

## Connect and pair

1. Attach the antennas and power both boards. Both initially start as Remote with Wi-Fi off.
2. On the board attached to your dashboard computer, hold **PRG for 6–7 seconds, then release**. It saves the Master role and restarts. Repeat this gesture to switch back to Remote. Role selection is stored in NVS, not compiled into the firmware. The earlier 3-second role gesture now opens the message menu instead.
3. Join `LoRa-Bridge-XXXXXXXX`, password **`LoRaBridge32`**, and open **http://192.168.4.1**. The HTTP page uses **ws://192.168.4.1:81/ws** for both live payloads and statistics. It does not poll or refresh for messages.
4. Remote pairing is open for 60 seconds after startup. A **double-click** opens another 60-second window. Select the discovered Remote using **Pair** on the Master dashboard.
5. Pairing is persistent and one-to-one. To replace a pair, stop senders and clear pairing on **both** boards: hold PRG **8 seconds or longer, then release**, or use the dashboard's local Unpair control on Master. Unpair is rejected while application queues or settings transactions are busy.

The local Master is shown separately from discovered nodes and never needs to hear its own announcement. Each valid received protocol frame updates its sender's discovery `lastHeard`. Discovery entries are bounded to eight; stale entries remain visible with their age and cannot be selected for pairing.

Master/Remote controls Wi-Fi and settings authority; it does not determine serial direction. Both boards transmit and receive application bytes. Neither is permanently assigned to an air or ground endpoint.

## PRG message menu

After pairing, either board can send a test message without a computer:

1. From the rotating status screens, **hold PRG for about 2 seconds, then release** (2–5 seconds opens the menu).
2. The menu shows **Back** and **Send message**, initially selecting Back. **Single-click** to move between them; **double-click** to select.
3. Selecting Send message queues `Hello from XXXXXXXX`, using the sender's node ID. The sender shows **TX queued**, **TX sending**, then **TX delivered** after an ACK, or **TX unconfirmed** when retries expire. An unpaired/offline board shows a send-unavailable message.
4. The receiver shows **RX message**, the sender's ID and the text for eight seconds. The last received test message remains available on an additional rotating OLED page.
5. Select Back to return to the status screens. Holding PRG for two seconds inside the menu also exits; it never changes roles or unpairs from inside the menu.

Outside the menu, single-click still cycles serial interfaces, double-click opens pairing, a **6–7 second** hold changes role, and an **8+ second** hold unpairs. Gestures take effect on release. The menu does not pause serial or radio processing.

Button messages use a dedicated reliable RF message type with ACKs, bounded retries and duplicate suppression. **They never enter either board's USB/UART byte stream.** The Master dashboard observes their payload once as TX or RX, using the same nonblocking copy logger. Packet/byte counters include these messages. Web SEND retains its existing behavior of explicitly injecting bytes into the remote serial stream. Flash this updated firmware on **both** boards for OLED message support; older firmware ignores the new message type. There is one bounded queued button message, which expires after 30 seconds if it cannot be scheduled.

## Serial wiring and selection

| Connection | Configuration |
| --- | --- |
| On-board USB serial | 115200 baud, 8N1, no flow control |
| Separate hardware UART RX | GPIO 4, connects to the other device's TX |
| Separate hardware UART TX | GPIO 5, connects to the other device's RX |
| Ground | Common ground, 3.3 V UART logic |

UART baud is configurable to 9600, 19200, 38400, 57600, 115200 or 230400. It changes on both paired boards; the USB serial baud stays 115200. The UART pins avoid the library's radio/OLED pins. This is not an RS-232 voltage interface.

**Auto mode** selects the first port with incoming bytes (USB wins a same-loop tie) and keeps that selection until reboot or a mode change. It does not detect baud or infer attachment from USB power. Before activity, received bytes go to USB. Only the selected input is consumed, and received traffic is sent only to the selected output; the two local ports are never merged or echoed.

For a receive-only UART device, select UART explicitly. Use the dashboard on Master or **single-click PRG** on either board to cycle Auto → USB → UART → Auto. Interface changes are accepted only while application queues and the pending transmit packet are empty. The selection is persisted. Stop both sources before changing it; bytes already buffered in a hardware driver are not a safe interface-switch boundary.

## Byte preservation and reliability

```text
selected serial input → bounded input queue → opaque payload chunk → reliable RF frame
                                                   └─ copy → Master TX log

validated RF payload → deduplication → bounded output queue → selected serial output
                                              └─ copy → Master RX log
```

Payload chunks contain up to **192 bytes**. Chunk boundaries are transport details, not MAVLink/message boundaries. No TX/RX labels, terminators or newlines are inserted into the serial stream. No application payload is parsed. Web SEND injects explicitly requested bytes into the same outgoing queue without serial echo.

RF frames use a versioned envelope: magic `UB`, version/type, length/reserved fields, 32-bit source/destination IDs, boot session, sequence, ACK session, payload, and CRC32, with integers encoded little-endian. The envelope and CRC are stripped before serial output. Radio CRC is also enabled. Node IDs are derived from the device MAC; sessions are generated per boot. This is a private point-to-point protocol, not LoRaWAN, and pairing/CRC do not provide encryption or cryptographic authentication.

Each direction has one outstanding reliable frame. ACKs take priority; other transmissions use nonblocking channel activity detection. Retry timing includes frame airtime and randomized exponential backoff. Up to **five transmission attempts** are made. A receiver ACKs data only after accepting it into its output queue. Retransmitted or stale sequence numbers within the current peer session are ACKed without forwarding the bytes or logging RX again. A peer restart resets the receive window; an outstanding payload is marked unconfirmed instead of replayed automatically into the new session.

The radio is half duplex. Both computers can submit bytes concurrently, and the scheduler alternates RF activity; this is not simultaneous RF transmit and receive.

**Delivery limits:** bounded retries and finite memory cannot promise lossless operation during an unlimited outage or when serial input continuously exceeds RF capacity. Retry exhaustion marks a chunk's delivery **unconfirmed** and moves on: the receiver may have delivered it while all ACKs were lost. No failure text is injected into serial. Hardware receive overruns are counted as events, not exact lost byte counts. The application input and output queues each hold 4096 bytes, plus one pending TX chunk; hardware RX buffers also hold 4096 bytes. There is no RTS/CTS flow control. ACK confirms retention in the destination queue, not delivery to its external application or persistence through power loss.

Application diagnostics never write to the telemetry port. ESP32 ROM/bootloader output can still appear on the on-board USB/UART0 during reset; capture application telemetry after startup. This application does not change bootloader settings or burn eFuses.

## Master payload console

- **TX** is the local queued payload copied when selected for RF transport, once per chunk, before its first attempt. **RX** is a Remote payload copied after validation/deduplication and acceptance into the local serial output queue. PRG test messages also appear as TX/RX, but are delivered to the OLED instead of serial.
- Timestamps are Master uptime in milliseconds, displayed as `hh:mm:ss.mmm`; they are not wall-clock time. Millisecond timestamps wrap after approximately 49 days.
- **TEXT** displays printable ASCII and escapes other bytes as `\xNN`. **HEX** displays every byte. **AUTO** uses text only for entirely printable ASCII chunks. Original bytes remain available when changing display modes.
- **Clear** clears only the browser's history. **Autoscroll** controls only the browser viewport. History is limited to 500 events.
- **SEND** accepts 1–192 UTF-8 text bytes or explicit HEX bytes; it never appends a newline. A WebSocket command being queued is not proof of RF delivery—check firmware status and ACK statistics.

The transport passes `const uint8_t*` payloads to a logger that copies into a **24-event static FreeRTOS queue** with **zero wait**. If full, it increments `logDrops` and continues. It performs no serial output, networking, string/HEX conversion or allocation. The separate lower-priority web task on core 0 owns HTTP/WebSocket sockets and JSON formatting. The transport loop runs on core 1. The fixed-size memory copy has a small bounded CPU cost; socket waits, disconnected clients and browser speed cannot make the logger wait for consumers. A blocked web task can drop observable events, never hold a transport buffer or prevent its reuse.

Events use this lossless format:

```json
{"type":"payload","direction":"RX","timestamp":123789,"length":4,"hex":"FD0900FF"}
```

Statistics are pushed over the same WebSocket at up to 4 Hz, using a one-slot overwrite queue. Remote never starts Wi-Fi, HTTP or a WebSocket service. The Master sees remote outgoing traffic as its own RX; no extra payload mirroring frames are sent over LoRa.

## Settings and recovery

| Preset | Bandwidth | Spreading factor | Coding rate denominator |
| --- | --- | --- | --- |
| Long Range | 125 kHz | 11 | 7 |
| Balanced | 250 kHz | 9 | 5 |
| High Speed | 500 kHz | 7 | 5 |

Stop serial senders and allow queues to drain before applying RF/UART settings. Master sends a reliable settings proposal; Remote validates it and ACKs. Both schedule the change after a 15-second guard interval. Payload packetization pauses during a staged/trial transition; serial input can still accumulate in bounded buffers. They exchange matching settings tokens on the new profile and persist the confirmed configuration. A trial without peer confirmation expires after 45 seconds. Loss of a confirmed link triggers a return to Balanced rendezvous after 30 seconds; additional recovery handles asymmetric confirmation loss.

Both boards boot on Balanced for discovery, irrespective of their saved preset. Once paired contact is re-established, Master negotiates its saved RF/UART configuration again. Recovery may temporarily interrupt application traffic; do not treat a setting change as a byte-stream synchronization primitive. Frequency is a common compile-time setting, not negotiated.

## Dashboard statistics

TX/RX packet and byte counters count unique **payload chunks**, excluding RF headers, ACKs, discovery and retransmitted payload copies. TX is attempted application traffic, RX is accepted application traffic. ACKed TX, unconfirmed TX/bytes, retransmissions, duplicates, malformed/CRC frames, radio errors, serial overrun events, backpressure events, rejected commands, dropped console events, RSSI/SNR, payload bytes/s, queue usage, device/link uptime and last peer packet age are exposed. Backpressure counts both serial saturation episodes and RF data rejected for lack of output space.

The **unconfirmed delivery rate** is failed outbound chunks divided by completed outbound chunks (ACKed + unconfirmed). It is not a measured RF packet-loss percentage: missing ACKs do not establish whether data was lost. RSSI/SNR reflect the latest valid paired packet; last packet age includes peer control traffic. Link uptime resets when contact is re-established. Counters and timestamps are 32-bit and reset on reboot.

## Validation

Host tests compile the real `ReliableLink.cpp` against simulated radio/serial/NVS/FreeRTOS interfaces. They exercise binary framing, CRC rejection, ring wrap, duplicate suppression, sequence/timer wrap, lost ACKs, bounded retry exhaustion, simultaneous bidirectional traffic, output backpressure, log-copy isolation/saturation, discovery timestamps, pairing, and settings transition/recovery.

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I tests/universal_bridge/fakes -I examples/Universal_LoRa_Bridge tests/universal_bridge/protocol_test.cpp examples/Universal_LoRa_Bridge/ReliableLink.cpp -o bridge-test
./bridge-test
g++ -std=c++17 -Wall -Wextra -Werror -I tests/universal_bridge/fakes -I examples/Universal_LoRa_Bridge tests/universal_bridge/serial_test.cpp examples/Universal_LoRa_Bridge/SerialBridge.cpp -o serial-test
./serial-test
node tests/universal_bridge/dashboard_test.js
```

The dashboard test executes the embedded JavaScript and checks live TX/RX rendering, format switching, Clear, Autoscroll, bounded history, safe text rendering and text/HEX injection. Protocol tests additionally exercise menu gestures, both directions of button messaging, lost message ACKs, duplicate suppression, unconfirmed delivery, and separation from concurrent binary serial data. CI also compiles the sketch for the Heltec V3.

Before operational use, perform these tests with two physical boards:

1. Pair the same firmware on both boards and check that only Master advertises Wi-Fi.
2. Send `Hello` and `Hello back` in opposite directions; verify original serial bytes and matching Master TX/RX entries, without local echo or added newline.
3. Send a paced binary fixture containing every byte value, including NUL/CR/LF/0xFF, in both directions. Compare complete received files byte-for-byte. Repeat USB↔USB, USB↔UART and UART↔UART.
4. Disconnect/reconnect the browser and use a deliberately slow client during traffic. Compare serial captures and watch console-drop counters.
5. Interrupt RF reception to exercise retries and disconnect one peer long enough to hit the retry limit. Verify explicit unconfirmed counters and no repeated downstream payload from ACK loss.
6. Try all RF presets/UART bauds, restart either board, and interrupt a settings transition. Verify eventual Balanced rendezvous and re-negotiation.
7. Verify rotating OLED pages, discovery ages, local Master separation, interface overrides and saved roles after power cycling.
8. Hold PRG for two seconds, select Send message, and check sender ACK status and the receiving OLED. Repeat from the other board and during paced binary serial traffic; test-message text must never appear in serial captures. Confirm Back exits without changing the serial interface or role.

Hardware timings, usable range, RF interference and electrical UART behavior require this bench validation; simulation does not establish those properties.

API references: [RadioLib SX126x](https://jgromes.github.io/RadioLib/class_s_x126x.html), [Arduino ESP32 UART](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html), [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets).
