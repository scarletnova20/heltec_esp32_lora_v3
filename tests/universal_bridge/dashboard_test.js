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
console.log('Dashboard WebSocket, TX/RX, text/hex, clear, autoscroll, bounded history and SEND tests passed');
