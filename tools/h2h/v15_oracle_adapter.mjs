import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {createInterface} from 'node:readline';
const protocol='stayc.gameplay-oracle.v1';
const wasm=readFileSync(new URL('./reference/v15/model.wasm',import.meta.url));
const sha=createHash('sha256').update(wasm).digest('hex');
if(sha!=='0ea799bb898a3a7f21bd2baee42934327c0f94396d23b36e0ef582c50e1a9fbc')throw Error('Unexpected genuine oracle digest');
const {instance}=await WebAssembly.instantiate(wasm,{wasi_snapshot_preview1:{fd_close:()=>8,fd_seek:()=>8,fd_write:()=>8}});
const e=instance.exports;e._initialize();e.preview_init(12,6);
const uint=(request,key,defaultValue)=>{const value=request[key]??defaultValue;if(!Number.isInteger(value)||value<0||value>0xffffffff)throw Error('Invalid uint32 '+key);return value;};
const flag=(request,key)=>{const value=request[key]??false;if(value===true||value===1)return 1;if(value===false||value===0)return 0;throw Error('Invalid boolean '+key);};
const pointer=()=>e.preview_buffer();
const transfer=()=>new Uint8Array(e.memory.buffer,pointer(),128);
const save=()=>{const before=transfer().slice();const length=e.preview_save(pointer());if(length!==92)throw Error('Invalid public save length');const bytes=transfer().slice(0,length);transfer().set(before);return Buffer.from(bytes).toString('hex');};
const state=()=>{const memory=new Uint8Array(e.memory.buffer),start=e.preview_state(),end=memory.indexOf(0,start);if(end<start||end-start>16384)throw Error('Invalid public state string');return JSON.parse(new TextDecoder().decode(memory.subarray(start,end)));};
function setHex(hex){if(typeof hex!=='string'||hex.length%2||hex.length>256||!/^[0-9a-f]*$/.test(hex))throw Error('Invalid transfer hex');transfer().fill(0);transfer().set(Buffer.from(hex,'hex'));return hex.length/2;}
for await(const raw of createInterface({input:process.stdin,crlfDelay:Infinity})){
 try{
  const r=JSON.parse(raw);let result=null,extra={};
  if(r.op==='identity'){
   if(r.protocol!==protocol)throw Error('Unsupported protocol');
   console.log(JSON.stringify({protocol,source_sha256:sha,engine:'genuine-v15-public-wasm'}));continue;
  }
  switch(r.op){
   case 'init':e.preview_init(uint(r,'tracks',12),uint(r,'songs',6));break;
   case 'input':{const key=uint(r,'key'),event=uint(r,'event');if(key>2||event>2)throw Error('Invalid input');e.preview_input(key,event);break;}
   case 'tick':e.preview_tick(uint(r,'ms'));break;
   case 'audio':e.preview_audio(uint(r,'ms',0),uint(r,'generation'),flag(r,'ended'),flag(r,'failed'));break;
   case 'position':e.preview_position(uint(r,'position_ms'),uint(r,'duration_ms'),uint(r,'generation'));break;
   case 'load':result=Boolean(e.preview_load(pointer(),setHex(r.hex)));break;
   case 'save':result=92;break;
   case 'shop_prepare':result=e.preview_shop_prepare(pointer());extra.candidate_hex=Buffer.from(transfer().slice(0,result)).toString('hex');break;
   case 'shop_validate':{const length=r.hex===undefined?uint(r,'length',92):setHex(r.hex);result=Boolean(e.preview_shop_validate(pointer(),length,uint(r,'generation'),uint(r,'debug_epoch')));break;}
   case 'shop_finish':{const length=r.hex===undefined?uint(r,'length',92):setHex(r.hex);result=e.preview_shop_finish(pointer(),length,uint(r,'result'),uint(r,'generation'),uint(r,'debug_epoch'));break;}
   default:throw Error('Unknown operation '+r.op);
  }
  console.log(JSON.stringify({result,...extra,state:state(),save_hex:save()}));
 }catch(error){console.error(error.stack);process.exit(2);}
}
