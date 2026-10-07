import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=path.resolve(process.argv[2]);
const read=p=>JSON.parse(fs.readFileSync(path.join(root,p),'utf8'));
const {validateManifest,adaptAction,drawThemedRoom,themedDecor}=await import(pathToFileURL(path.join(root,'reference/core.mjs')));
const manifest=read('manifest.json'),actions=read('reference/actions.json').actions;
const members=manifest.members.map(member=>member.outfits.map(outfit=>{
 const atlas=outfit.atlases.find(a=>a.kind==='rich');
 const meta=validateManifest(read(atlas.manifest),'stayc',member.member,outfit.id);
 const pack={poses:meta.poses,availablePoses:meta.poses.map(p=>p.id)};
 return actions.map(a=>adaptAction(a,pack));
}));
const rooms=[0,1,2].map(room=>{const boxes=[];drawThemedRoom((...box)=>boxes.push(box),'stayc',room);return boxes;});
const decor=read('reference/life-gallery.json').filter(a=>a.name.startsWith('装饰 /')).map(a=>({label:a.state.decor_choice_name,slot:a.state.decor_slot,commands:themedDecor(a.state.decor_scene,'stayc')}));
process.stdout.write(JSON.stringify({members,rooms,decor}));
