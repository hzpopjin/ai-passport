'use strict';
const canvas=document.getElementById('screen'),ctx=canvas.getContext('2d');
ctx.imageSmoothingEnabled=false;
const C={cream:'#fffcf2',ink:'#26466b',sky:'#dceffa',blue:'#80bbe6',pink:'#f7cdd9',lemon:'#ffea8a'};
const home=['IAN 小房间','随身听','柠檬接接乐','收藏册','应援牌','设置'];
const cards=['初见晴空','柠檬心事','海风来信','比心时刻','粉色舞台','夏日星光','一起跳舞','心动满格'];
const modes=['顺序循环','随机播放','单曲循环'],outfits=['晴空水手服','柠檬针织衫','粉色舞台装'],rooms=['晴空小屋','柠檬花园','心动舞台'];
let state=null,catalog=null,images=[],imageLoads=[],audio=new Audio(),audioGeneration=0,reported=0,audioEnabled=false,queue=Promise.resolve(),audioFailed=false;
const error=document.getElementById('connection');
let iconPatterns=null;
const iconsReady=fetch('/assets/images/h2h/icons.json').then(r=>r.json()).then(data=>{iconPatterns=Object.values(data);});
const galleryMode=Boolean(document.getElementById('gallery'));
const font=new FontFace('H2HPixel','url(/preview/h2h/font.ttf)');
const fontReady=font.load().then(f=>document.fonts.add(f)).catch(()=>{});
for(let r=0;r<3;r++){images[r]=[];for(let c=0;c<5;c++){const img=new Image();imageLoads.push(new Promise((resolve,reject)=>{img.onload=resolve;img.onerror=()=>reject(Error('Unable to load '+img.src));}));img.src=`/assets/images/h2h/ian_${r}_${c}.png`;images[r][c]=img;}}
function box(x,y,w,h,fill,r=10){ctx.fillStyle=fill;ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fill();}
function text(value,x,y,width=200,align='left',color=C.ink){ctx.font='16px H2HPixel, "PingFang SC", sans-serif';ctx.fillStyle=color;ctx.textAlign=align;ctx.textBaseline='top';let v=String(value);while(ctx.measureText(v).width>width&&v.length>1)v=v.slice(0,-2)+'…';ctx.fillText(v,align==='center'?x+width/2:align==='right'?x+width:x,y);}
function room(r){
box(12,43,216,199,[C.sky,'#ebf3ce','#f8e5ee'][r],18);
if(r===0){box(154,58,54,58,'#fffef9',12);box(159,63,44,48,'#bde5f5',8);box(178,63,4,48,'#fffef9',0);box(159,85,44,4,'#fffef9',0);box(29,170,26,32,'#f5b6ca',6);box(34,151,16,26,'#9bcb9a',8);}
else if(r===1){box(168,63,29,29,C.lemon,14);box(30,178,177,25,'#b6d798',9);for(let i=0;i<3;i++){box(33+i*70,150,4,35,'#79a967',0);box(27+i*70,140,16,22,C.lemon,6);}}
else{box(22,51,18,140,C.pink,6);box(200,51,18,140,C.pink,6);box(36,195,170,12,'#d4b6e6',4);box(26,207,188,12,'#b5cce9',4);for(let i=0;i<5;i++)box(56+i*29,54,8,8,C.lemon,4);}
}

function sprite(x,y,scale=2,pose=null,outfit=null){
 const p=pose??state.anim_pose??0,o=outfit??state.outfit,img=images[o][p];
 const dx=pose===null?(state.anim_dx||0):0,dy=pose===null?(state.anim_dy||0):0;
 if(img.complete&&img.naturalWidth>0)ctx.drawImage(img,x+32-32*scale+dx,y+40-40*scale+dy,64*scale,80*scale);
 if(pose===null&&state.anim_hearts)for(let i=0;i<3;i++)text('♥',[40,188,174][i],112+i*14-(Math.floor(performance.now()/60)+i*7)%24,18,'left','#eaafca');
}
function pixelIcon(index,x,y){if(!iconPatterns)return;ctx.fillStyle=C.ink;iconPatterns[index].forEach((row,py)=>{for(let px=0;px<16;px++)if(row[px]==='#')ctx.fillRect(x+px*2,y+py*2,2,2);});}
function row(index,y,label,selected,locked=false){box(16,y,208,32,selected?C.lemon:'white');text(label,26,y+6,190,'left',locked?'#98a4af':C.ink);}
function time(ms){return Math.floor(ms/60000)+':'+String(Math.floor(ms/1000)%60).padStart(2,'0');}
function render(){if(!state||!catalog)return;const s=state;ctx.clearRect(0,0,240,320);box(0,0,240,320,C.cream,0);let title='H2H POCKET',hint='↑ ↓ 选择   ● 打开';const track=catalog.tracks[s.track];switch(s.page){
case 0:room(s.room);sprite(88,94);for(let i=0;i<6;i++)box(91+i*10,231,5,5,i===s.selection?C.ink:C.blue,3);box(16,244,208,34,C.lemon,12);text('←',24,251,16);text('→',200,251,16);text(home[s.selection],44,251,152,'center');break;
case 1:title='IAN 小房间';room(s.room);sprite(88,94);for(let i=0;i<4;i++){box(16+i*54,246,46,34,s.selection===i?C.lemon:C.sky,10);pixelIcon(i,23+i*54,247);}if(s.selection>=2)text(s.selection===2?outfits[s.outfit]:rooms[s.room],22,221,196,'center');hint='↑ ↓ 切换   ● 互动';break;
case 2:title='随身听';text(track.title,22,52,196);{const paired=s.song_count<catalog.tracks.length;const labels=['▶ 播放器','选择歌曲',...(paired?['播放内容  '+(s.chorus?'副歌':'全曲')]:[]),'播放模式  '+modes[s.mode],'开跳倒计时  '+(s.countdown_on?'开':'关')];labels.forEach((v,i)=>row(i,(paired?81:112)+i*39,v,s.selection===i));}hint='● 选择   长按返回';break;
case 3:title='随身听';box(12,43,216,179,C.sky,18);sprite(88,106,2);text(track.title,26,49,188);box(24,229,192,6,'#e1e9eb',3);box(24,229,Math.max(1,192*(s.position_ms||0)/track.duration_ms),6,C.blue,3);text(time(s.position_ms||0)+' / '+time(track.duration_ms),24,241,192,'center');text(s.countdown?'准备开跳  '+Math.ceil(s.countdown/1000):s.error?'播放失败  确定重试':(s.play?'正在播放':'已暂停')+'  音量'+s.volume+'%',20,263,200,'center');hint='短按音量  三连切歌';break;
case 4:title=s.chorus?'选择副歌':'选择全曲';{const start=Math.floor(s.selection/5)*5;catalog.tracks.slice(start,Math.min(start+5,s.song_count)).forEach((t,i)=>row(i,62+i*42,t.title,s.selection===i+start));}hint='▶ 播放   长按返回';break;
case 5:title='柠檬接接乐';box(12,43,216,232,C.sky,18);text((s.paused?'暂停 ':'剩余 ')+Math.ceil(s.game_ms/1000)+'s   接住 '+s.caught,24,50,192,'center');for(let i=0;i<5;i++)box(27+i*40,79,2,165,'#c9e4f3',0);for(const d of s.drops)if(d.active)text(d.heart?'♥':'●',29+d.lane*40,79+d.y,20,'left',d.heart?'#eaafca':'#eab94a');box(22+s.basket*40,251,32,14,C.pink,5);hint='上键左 下键右 确定暂停';break;
case 6:title='接住小幸运';room(s.room);sprite(88,100);text('接住 '+s.caught+' 个  获得 '+s.reward+' 爱心',20,246,200,'center');hint='● 再玩   长按返回';break;
case 7:title='收藏册';text('已收藏 '+s.cards.toString(2).replaceAll('0','').length+'/8  每5爱心解锁',20,45,200,'center');for(let i=0;i<8;i++){const unlocked=s.cards&(1<<i);box(16+i%2*108,73+Math.floor(i/2)*44,100,39,s.selection===i?C.lemon:unlocked?C.sky:'#eeede8',10);text(unlocked?cards[i]:'等待心动',21+i%2*108,85+Math.floor(i/2)*44,90,'center',unlocked?C.ink:'#98a4af');}for(let i=8;i<10;i++){box(16+(i-8)*108,253,100,27,s.selection===i?C.lemon:C.sky,8);text(i===8?'服装 '+(s.outfit+1)+'/'+s.outfits:'背景 '+(s.room+1)+'/'+s.rooms,20+(i-8)*108,257,92,'center');}hint='● 选择   长按返回';break;
case 8:title='心动收藏';box(18,44,204,226,s.selection%2?C.pink:C.sky,16);text(cards[s.selection],29,55,182,'center');sprite(88,111,2,s.selection%5,s.selection%3);text('IAN / H2H',44,239,152);hint='♥ 设为应援牌';break;
case 9:title='IAN 应援牌';room(s.room);sprite(88,113,2,s.badge_card<8&&!s.play?s.badge_card%5:null,s.badge_card<8?s.badge_card%3:null);text('I A N',22,51,196,'center');text(s.badge_card<8?cards[s.badge_card]:'HEARTS2HEARTS',20,251,200,'center');for(let i=0;i<4;i++)text('♥',27+i*53,95+Math.floor(performance.now()/110+i*39)%125,20,'left','#eaafca');hint='长按确定返回';break;
case 10:title='设置';['音量  '+s.volume+'%','倒计时  '+(s.countdown_on?'开':'关'),'模式  '+modes[s.mode],'AI PASSPORT'].forEach((v,i)=>row(i,58+i*48,v,s.selection===i));hint='● 更改   长按返回';break;
case 11:title='AI PASSPORT';text('LOCAL PREVIEW',20,45,200,'center');box(20,76,200,129,C.sky,14);text('Account / BLE / Wi-Fi',29,101,182,'center');text('Device testing required',29,137,182,'center');['Confirm','Refresh','Forget'].forEach((v,i)=>{box(9+i*75,244,71,31,s.selection===i?C.lemon:C.sky,8);text(v,11+i*75,251,67,'center');});hint='Hold OK to return';break;
}text(title,24,12,112);text('♥'+s.hearts+'  --',130,12,87,'right');text(hint,20,286,200,'center');if(s.pose_ms&&s.pose===4&&s.unlocked<8){box(20,270,200,20,C.lemon,2);text('解锁了一张新收藏卡',20,270,200,'center');}}
let audioReady=true,pendingRequests=0;
function update(s){
 if(!catalog)return;error.textContent='';state=s;
 const seekHint=document.getElementById('seek-hint');if(seekHint)seekHint.hidden=s.page!==3;
 if(s.generation!==audioGeneration){
  audio.pause();audioGeneration=s.generation;reported=s.start_ms;audioFailed=false;audioReady=false;
  const generation=s.generation,target=s.start_ms/1000;
  audio.onloadedmetadata=()=>{if(generation!==audioGeneration)return;audio.currentTime=target;reported=Math.round(target*1000);audioReady=true;syncPlayback();};
  audio.src='/assets/music/h2h/'+catalog.tracks[s.track].file;
 }
 audio.volume=s.volume/100;syncPlayback();render();
}
function syncPlayback(){
 if(state?.play&&audioEnabled&&!audioFailed&&audioReady){if(audio.paused)audio.play().catch(()=>{error.textContent='点击“开启预览声音”以允许浏览器播放。';});}
 else audio.pause();
}
function request(path,data){pendingRequests++;queue=queue.then(async()=>{const r=await fetch(path,data?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}:{});if(!r.ok)throw Error(r.status);const s=await r.json();update(s);return s;}).catch(e=>{error.textContent='预览连接中断，请确认本地预览服务正在运行。';console.error(e);}).finally(()=>{pendingRequests--;});return queue;}
function input(key,event){if(!catalog||galleryMode)return;request('/api/input',{key,event});}
let holding=new Map();
function down(key){if(holding.has(key))return;const press={long:false};holding.set(key,press);input(key,0);press.timer=setTimeout(()=>{press.long=true;input(key,2);},500);}
function up(key){const press=holding.get(key);if(!press)return;clearTimeout(press.timer);holding.delete(key);if(!press.long)input(key,1);}
function cancel(key){const press=holding.get(key);if(press)clearTimeout(press.timer);holding.delete(key);}
for(const button of document.querySelectorAll('[data-key]')){const key=Number(button.dataset.key);button.addEventListener('pointerdown',e=>{e.preventDefault();button.setPointerCapture(e.pointerId);down(key);});button.addEventListener('pointerup',()=>up(key));button.addEventListener('pointercancel',()=>cancel(key));}
window.addEventListener('blur',()=>{for(const key of holding.keys())cancel(key);});
window.addEventListener('keydown',e=>{const key={ArrowUp:0,ArrowDown:1,Enter:2}[e.key];if(key!==undefined){e.preventDefault();down(key);}if(e.key==='Escape'){e.preventDefault();input(2,2);}});
window.addEventListener('keyup',e=>{const key={ArrowUp:0,ArrowDown:1,Enter:2}[e.key];if(key!==undefined){e.preventDefault();up(key);}});
document.getElementById('sound').onclick=()=>{audioEnabled=!audioEnabled;document.getElementById('sound').textContent=audioEnabled?'关闭预览声音':'开启预览声音';error.textContent='';if(audioEnabled&&state.play)audio.play().catch(()=>{error.textContent='浏览器无法播放 Opus，请使用 Chrome 或 Edge。';});if(!audioEnabled)audio.pause();};
function reportAudio(ended=false){
 if(!audioReady||audio.seeking)return;
 const position=Math.round((audio.currentTime||0)*1000),ms=audioEnabled&&(ended||!audio.paused)?Math.max(0,position-reported):0;
 reported=position;
 request('/api/audio',{ms,generation:audioGeneration,position_ms:position,duration_ms:catalog.tracks[state.track].duration_ms,ended});
}
audio.onended=()=>reportAudio(true);
audio.onerror=()=>{audioFailed=true;if(audioGeneration)request('/api/audio',{generation:audioGeneration,failed:true,position_ms:state.position_ms,duration_ms:catalog.tracks[state.track].duration_ms});};
fetch('/assets/music/h2h/catalog.json').then(r=>r.json()).then(async c=>{catalog=c;if(galleryMode){await fontReady;await iconsReady;await Promise.all(imageLoads);buildGallery();return;}audio.src='/assets/music/h2h/'+c.tracks[0].file;return request('/api/state');});
setInterval(()=>{if(galleryMode||!catalog||!state||pendingRequests)return;if(audioReady&&audioGeneration&&!audio.seeking)reportAudio();else request('/api/state');},150);
if(!galleryMode)setInterval(render,100);

function buildGallery(){
 const base={anim_pose:0,anim_dx:0,anim_dy:0,anim_hearts:false,position_ms:0,page:0,selection:0,track:0,song_count:catalog.song_count||catalog.tracks.length,chorus:false,generation:0,play:false,error:false,countdown:0,pose:0,pose_ms:0,hearts:40,cards:255,outfit:0,room:0,volume:35,mode:0,countdown_on:1,badge_card:8,game_ms:30000,paused:false,caught:10,reward:2,basket:2,unlocked:8,outfits:3,rooms:3,drops:[{lane:0,y:30,active:true,heart:false},{lane:3,y:80,active:true,heart:true}]};
 const views=[];
 ['主页','小房间','随身听','播放器','选曲','小游戏','结算','收藏册','收藏卡','应援牌','设置','AI Passport 本地预览'].forEach((name,page)=>views.push({name,state:{...base,page}}));
 for(let room=0;room<3;room++)for(let outfit=0;outfit<3;outfit++)views.push({name:rooms[room]+' / '+outfits[outfit],state:{...base,page:1,room,outfit,selection:outfit===2?3:2}});
 for(let outfit=0;outfit<3;outfit++)for(let pose=0;pose<5;pose++)views.push({name:outfits[outfit]+' / '+['待机','挥手','比心','跳舞','庆祝'][pose],state:{...base,page:1,outfit,anim_pose:pose,anim_hearts:pose===2}});
 for(let i=0;i<8;i++)views.push({name:'收藏卡 / '+cards[i],state:{...base,page:8,selection:i}});
 views.push({name:'长歌名与错误状态',state:{...base,page:3,error:true},long:true});
 for(const view of views){state=view.state;const old=catalog.tracks[0].title;if(view.long)catalog.tracks[0].title='Lemon Tang / Hearts2Hearts / Lemon Tang';render();catalog.tracks[0].title=old;const frame=document.createElement('figure'),picture=document.createElement('canvas'),caption=document.createElement('figcaption');picture.width=240;picture.height=320;picture.getContext('2d').drawImage(canvas,0,0);caption.textContent=view.name;frame.append(picture,caption);document.getElementById('gallery').append(frame);}
}
