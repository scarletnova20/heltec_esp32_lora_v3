// Executes the actual embedded dashboard script with a small DOM/WebSocket harness.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const page=fs.readFileSync('examples/Universal_LoRa_Bridge/DashboardPage.h','utf8');
class Element {
  constructor(){this.children=[];this.value='';this.checked=false;this.textContent='';this.scrollTop=0;}
  append(...items){for(const x of items){x.parent=this;this.children.push(x);}}
  appendChild(x){this.append(x);}
  replaceChildren(...items){this.children=[];this.append(...items);}
  remove(){this.parent.children.splice(this.parent.children.indexOf(this),1);}
  get firstChild(){return this.children[0];}
  get scrollHeight(){return this.children.length*20;}
}
const elements={};const get=id=>elements[id]??=new Element();
get('mode').value='TEXT';get('sendmode').value='TEXT';get('autoscroll').checked=true;
class Socket {static OPEN=1;constructor(url){this.url=url;this.readyState=1;this.sent=[];Socket.instance=this;}send(s){this.sent.push(s);}close(){this.readyState=3;}}
const context=vm.createContext({document:{getElementById:get,createElement:()=>new Element()},WebSocket:Socket,location:{hostname:'192.168.4.1'},TextEncoder,Uint8Array,setTimeout:()=>{}});
vm.runInContext(page.match(/<script>([\s\S]*?)<\/script>/)[1],context);
const socket=Socket.instance;assert.equal(socket.url,'ws://192.168.4.1:81/ws');socket.onopen();
const emit=e=>socket.onmessage({data:JSON.stringify(e)});
emit({type:'payload',direction:'TX',timestamp:1234,length:5,hex:'48656C6C6F'});
emit({type:'payload',direction:'RX',timestamp:2000,length:4,hex:'FD0900FF'});
assert.match(get('console').children[0].textContent,/00:00:01.234 TX \[5\]  Hello/);
assert.match(get('console').children[1].textContent,/RX \[4\]  \\xFD\\x09\\x00\\xFF/);
assert.equal(get('console').scrollTop,get('console').scrollHeight);
get('mode').value='HEX';get('mode').onchange();assert.match(get('console').children[1].textContent,/FD 09 00 FF/);
get('autoscroll').checked=false;const scroll=get('console').scrollTop;
emit({type:'payload',direction:'RX',timestamp:2100,length:1,hex:'00'});assert.equal(get('console').scrollTop,scroll);
get('clear').onclick();assert.equal(get('console').children.length,0);assert.equal(socket.sent.length,0);
get('sendtext').value='Hello';get('send').onclick();assert.equal(socket.sent.pop(),'send:48656c6c6f');
get('sendmode').value='HEX';get('sendtext').value='FD 09 00 FF';get('send').onclick();assert.equal(socket.sent.pop(),'send:fd0900ff');
get('sendtext').value='F';get('send').onclick();assert.equal(socket.sent.length,0);
for(let i=0;i<700;++i)emit({type:'payload',direction:'RX',timestamp:i,length:1,hex:'FF'});
assert.equal(get('console').children.length,500);
get('mode').value='TEXT';get('mode').onchange();get('clear').onclick();
emit({type:'payload',direction:'RX',timestamp:0,length:8,hex:'3C7363726970743E'});
assert.equal(get('console').children.length,1);assert.match(get('console').children[0].textContent,/<script>/);
assert(!page.includes('.innerHTML'));
get('clear').onclick();
emit({type:'payload',direction:'TX',timestamp:100,length:5,hex:'48656C6C6F',source:1,destination:2,sequence:7,outcome:0});
emit({type:'payload',direction:'RX',timestamp:200,length:5,hex:'48656C6C6F',source:1,destination:2,sequence:7,outcome:1});
emit({type:'payload',direction:'RX',timestamp:300,length:2,hex:'4869',source:2,destination:1,sequence:8,outcome:0});
emit({type:'payload',direction:'TX',timestamp:400,length:5,hex:'48656C6C6F',source:1,destination:2,sequence:9,outcome:2});
assert.match(get('console').children[0].textContent,/Master 00000001 -> Remote 00000002 sending.*Hello/);
assert.match(get('console').children[1].textContent,/RX.*Remote 00000002 received from Master 00000001 \(ACK confirmed\).*Hello/);
assert.match(get('console').children[2].textContent,/RX.*Master 00000001 received from Remote 00000002.*Hi/);
assert.match(get('console').children[3].textContent,/delivery UNCONFIRMED/);
get('mode').value='HEX';get('mode').onchange();assert.match(get('console').children[1].textContent,/48 65 6C 6C 6F/);
const stats={type:'stats',local:1,peer:0,now:80000,nodes:[{id:2,master:false,peer:1,lastHeard:79000}]};
const pairButton=()=>get('nodes').children[0].children[4].children[0];
emit(stats);assert.equal(pairButton().disabled,false);pairButton().onclick();assert.equal(socket.sent.pop(),'pair:2');
emit({...stats,peer:2,linked:false});assert.equal(pairButton().textContent,'Retry pair');assert.equal(pairButton().disabled,false);
emit({...stats,peer:2,linked:true});assert.equal(pairButton().disabled,true);assert.equal(pairButton().textContent,'Connected');
emit({...stats,nodes:[{id:2,master:true,peer:0,lastHeard:79000}]});assert.equal(pairButton().textContent,'Change to Remote');assert.equal(pairButton().disabled,true);
emit({...stats,nodes:[{id:2,master:false,peer:99,lastHeard:79000}]});assert.equal(pairButton().textContent,'Unpair Remote first');assert.equal(pairButton().disabled,true);
emit({...stats,nodes:[{id:2,master:false,peer:0,lastHeard:1000}]});assert.equal(pairButton().textContent,'Offline');assert.equal(pairButton().disabled,true);
console.log('Dashboard WebSocket, TX/RX, text/hex, clear, autoscroll, bounded history and SEND tests passed');
