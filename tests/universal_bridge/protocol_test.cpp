#include <assert.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include "ReliableLink.h"

uint32_t testNow = 0;
FakeEsp ESP;
int SX1262::startTransmit(const uint8_t* p, size_t n) {
  sent.assign(p,p+n); tx=true; receiving=false; collision=false;
  finishAt=testNow+getTimeOnAir(n)/1000; return 0;
}
int SX1262::startChannelScan() { scan=true; scanBusy=false; receiving=false; finishAt=testNow+10; return 0; }
// Test the real ReliableLink scheduler and queues; emulate serial draining explicitly below.
namespace bridge {
void SerialBridge::begin(uint32_t, Interface selection) { setMode(selection); }
void SerialBridge::setMode(Interface selection) { mode=selection; active=selection==Interface::Uart?selection:Interface::Usb; }
void SerialBridge::poll(Counters&) { (void)selected; (void)pressured; }
}
using namespace bridge;
struct Board {
  SX1262 radio; SerialBridge serial; TelemetryLog log; SettingsManager settings;
  ReliableLink link{radio,serial,log,settings};
  void begin(uint32_t id,bool master,uint32_t peer=0) {
    ESP.mac=id; settings.master=master;settings.peer=peer;log.begin();assert(link.begin());
  }
  Snapshot snapshot() { Snapshot s; link.snapshot(s,testNow);return s; }
};
struct World {
  Board a,b; unsigned dropAcks=0; bool allAcks=false,dropData=false,drain=true;
  std::vector<uint8_t> atA,atB;
  World(bool paired=true) { testNow=0; a.begin(1,true,paired?2:0); b.begin(2,false,paired?1:0); }
  void deliver(Board& from,Board& to) {
    auto& r=from.radio;
    if(r.scan&&due(testNow,r.finishAt)){r.scan=false;r.irq=true;return;}
    if(!r.tx || !due(testNow,r.finishAt))return;
    Packet p; assert(decode(r.sent.data(),r.sent.size(),p));
    bool drop=dropData&&p.kind==Kind::Data;
    if(p.kind==Kind::Ack&&(allAcks||dropAcks)){drop=true;if(dropAcks)--dropAcks;}
    if(!drop&&!r.collision&&to.radio.receiving&&to.radio.bw==r.bw&&to.radio.sf==r.sf) {
      to.radio.inbox=r.sent;to.radio.irq=true;to.radio.receiving=false;
    }
    r.tx=false;r.irq=true;
  }
  void step() {
    testNow+=5;
    if(a.radio.tx&&b.radio.tx){a.radio.collision=b.radio.collision=true;}
    if(a.radio.scan&&b.radio.tx)a.radio.scanBusy=true;
    if(b.radio.scan&&a.radio.tx)b.radio.scanBusy=true;
    deliver(a,b);deliver(b,a);
    bool ai=a.radio.irq,bi=b.radio.irq;a.radio.irq=b.radio.irq=false;
    a.link.tick(ai,testNow);b.link.tick(bi,testNow);
    if(drain)for(auto pair:{std::make_pair(&a,&atA),std::make_pair(&b,&atB)}) {
      uint8_t bytes[256];size_t n=pair.first->serial.outgoing.peek(bytes,sizeof(bytes));
      pair.second->insert(pair.second->end(),bytes,bytes+n);pair.first->serial.outgoing.discard(n);
    }
  }
  void run(uint32_t duration) { uint32_t until=testNow+duration;while(!due(testNow,until))step(); }
  void connect(){run(18000);assert(a.snapshot().linked&&b.snapshot().linked);}
};
static void framing() {
  Packet p;p.source=1;p.destination=2;p.session=123;p.sequence=0xffffffff;p.kind=Kind::Data;
  uint8_t wire[FrameMax];Packet q;
  for(size_t n=0;n<=PayloadMax;++n){p.length=n;for(size_t i=0;i<n;++i)p.data[i]=uint8_t(i*31+n);
    auto before=p;size_t length=encode(p,wire,sizeof(wire));assert(length==HeaderSize+n+4);
    assert(memcmp(&before,&p,sizeof(p))==0);assert(decode(wire,length,q));assert(q.length==n&&q.sequence==p.sequence);assert(!memcmp(p.data,q.data,n));
    for(size_t i=0;i<length;++i){wire[i]^=0x80;assert(!decode(wire,length,q));wire[i]^=0x80;}
    assert(!decode(wire,length-1,q));
  }
  ReceiveWindow w;w.accept(0xfffffffe);assert(w.duplicate(0xfffffffe));assert(!w.duplicate(0xffffffff));w.accept(0xffffffff);assert(!w.duplicate(0));w.accept(0);assert(w.duplicate(0xffffffff));w.reset();assert(!w.duplicate(0));
  assert(due(4,0xfffffff0));assert(!due(0xfffffff0,4));
  ByteRing<7> ring;uint8_t bytes[]={0,1,2,3,4,5,6},out[7];assert(ring.push(bytes,6));ring.discard(4);assert(ring.push(bytes,5));assert(!ring.push(bytes,1));assert(ring.peek(out,7)==7);uint8_t expected[]={4,5,0,1,2,3,4};assert(!memcmp(out,expected,7));
}
static void observer() {
  TelemetryLog log;log.begin();uint8_t bytes[]={0,0xff,'H','i',13,10};auto original=std::vector<uint8_t>(bytes,bytes+6);
  assert(log.copy(true,bytes,6,321));memset(bytes,0,6);PayloadEvent event;assert(log.pop(event));assert(!memcmp(event.data,original.data(),6));assert(event.timestamp==321&&event.tx);
  for(size_t i=0;i<LogDepth;++i)assert(log.copy(false,original.data(),6,i));
  assert(!log.copy(true,original.data(),6,0));assert(original[1]==0xff);
}
static void bidirectional() {
  World w;w.connect();std::vector<uint8_t> a(1536),b(1024);
  for(size_t i=0;i<a.size();++i)a[i]=uint8_t(i);
  for(size_t i=0;i<b.size();++i)b[i]=uint8_t(255-i);
  w.a.serial.incoming.push(a.data(),a.size());w.b.serial.incoming.push(b.data(),b.size());w.dropAcks=2;w.run(110000);
  if(w.atA!=b||w.atB!=a) {
    auto sa=w.a.snapshot(),sb=w.b.snapshot();
    std::cerr<<"A bytes="<<w.atA.size()<<" B bytes="<<w.atB.size()<<" failed="<<sa.counters.failed<<","<<sb.counters.failed<<" retries="<<sa.counters.retries<<","<<sb.counters.retries<<" queues="<<sa.txQueued<<","<<sb.txQueued<<" status="<<sa.status<<","<<sb.status<<"\n";
  }
  assert(w.atA==b&&w.atB==a);assert(w.a.snapshot().counters.retries+w.b.snapshot().counters.retries>0);
  assert(w.a.snapshot().counters.duplicates+w.b.snapshot().counters.duplicates>0);
  std::vector<uint8_t> tx,rx;PayloadEvent e;while(w.a.log.pop(e)){auto& v=e.tx?tx:rx;v.insert(v.end(),e.data,e.data+e.length);}
  assert(tx==a&&rx==b);assert(w.b.log.size()==0);
}
static void boundedRetries() {
  World w;w.connect();uint8_t bytes[]={0,0xff,13,10};w.allAcks=true;w.a.serial.incoming.push(bytes,4);w.run(45000);
  assert(w.atB==std::vector<uint8_t>(bytes,bytes+4));assert(w.a.snapshot().counters.failed==1);assert(w.a.snapshot().counters.retries==MaxAttempts-1);
  assert(w.b.snapshot().counters.rxPackets==1);
}
static void saturation() {
  World w;w.connect();uint8_t payload[PayloadMax]{};
  for(unsigned i=0;i<LogDepth;++i)assert(w.a.log.copy(true,payload,sizeof(payload),testNow));
  w.a.serial.incoming.push(payload,sizeof(payload));w.run(10000);
  assert(w.atB.size()==sizeof(payload));assert(w.a.snapshot().counters.logDrops==1);assert(w.a.snapshot().counters.acknowledged==1);
  w.drain=false;uint8_t full[BufferSize]{};assert(w.b.serial.outgoing.push(full,sizeof(full)));
  w.a.serial.incoming.push(payload,sizeof(payload));w.run(1000);assert(w.a.snapshot().counters.acknowledged==1);
  w.b.serial.outgoing.discard(sizeof(full));w.drain=true;w.run(20000);assert(w.a.snapshot().counters.acknowledged==2);assert(w.atB.size()==2*sizeof(payload));
}
static void pairingAndSettings() {
  World w(false);w.run(7000);assert(w.a.snapshot().nodes[0].id==2);assert(w.a.snapshot().nodes[0].lastHeard>0);
  Command c;c.type=CommandType::Pair;c.value=2;w.a.link.command(c,testNow);w.run(18000);assert(w.a.snapshot().linked&&w.b.snapshot().linked);
  c.type=CommandType::Settings;c.value=0;c.baud=57600;w.a.link.command(c,testNow);w.run(40000);
  assert(w.a.settings.active.preset==0&&w.b.settings.active.preset==0);assert(w.a.settings.active.baud==57600&&w.b.settings.active.baud==57600);
  assert(!w.a.settings.pending&&!w.b.settings.pending);
  // Remote disappears: Master must return to the shared discovery configuration.
  for(unsigned i=0;i<10000;++i){testNow+=5;bool irq=(w.a.radio.tx||w.a.radio.scan)&&due(testNow,w.a.radio.finishAt);if(irq){w.a.radio.tx=w.a.radio.scan=false;}w.a.link.tick(irq,testNow);}
  assert(w.a.settings.active.preset==1&&w.a.settings.active.token==0);
}
static void failedSettingsAndStaleSession() {
  World w;w.connect();auto before=w.a.snapshot().counters.rxPackets;
  Packet stale;stale.kind=Kind::Data;stale.source=2;stale.destination=1;stale.session=0x12345678;stale.sequence=1;stale.length=1;stale.data[0]=0xff;
  uint8_t bytes[FrameMax];size_t n=encode(stale,bytes,sizeof(bytes));
  w.a.radio.inbox.assign(bytes,bytes+n);w.a.radio.irq=true;w.step();
  assert(w.a.snapshot().counters.rxPackets==before&&w.atA.empty());
  Command c;c.type=CommandType::Settings;c.value=2;c.baud=38400;w.allAcks=true;w.a.link.command(c,testNow);
  w.run(160000);w.allAcks=false;w.run(60000);
  assert(!w.a.settings.pending&&!w.b.settings.pending);
  assert(w.a.settings.active.preset==1&&w.b.settings.active.preset==1);
  assert(w.a.snapshot().linked&&w.b.snapshot().linked);
}
int main(){framing();observer();bidirectional();boundedRetries();saturation();pairingAndSettings();failedSettingsAndStaleSession();std::cout<<"All bridge protocol, transport, discovery, settings and observer tests passed\n";}
