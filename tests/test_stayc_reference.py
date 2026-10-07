#!/usr/bin/env python3
"""Compare native rich-action samples with the supplied original JS runtime."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "assets/images/stayc/source"
NODE = r'''
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=process.argv[2];
const read=p=>JSON.parse(fs.readFileSync(path.join(root,p),'utf8'));
const {adaptAction,validateManifest}=await import(pathToFileURL(path.join(root,'reference/core.mjs')));
const {sample}=await import(pathToFileURL(path.join(root,'reference/action-runtime.mjs')));
const actions=read('reference/actions.json').actions,manifest=read('manifest.json');
const props={null:0,heart:1,phone:2,racket:3,basketball:4};
const attachments={null:0,right_hand:1,hands_center:2,right_hand_below:3};
const tests=[];
for(let mi=0;mi<6;mi++)for(let oi=0;oi<2;oi++){
 const member=manifest.members[mi],outfit=member.outfits[oi],atlas=outfit.atlases.find(a=>a.kind==='rich');
 const meta=validateManifest(read(atlas.manifest),'stayc',member.member,outfit.id);
 const pack={poses:meta.poses,availablePoses:meta.poses.map(p=>p.id)};
 for(let ai=0;ai<8;ai++){
  const a=adaptAction(actions[ai],pack),times=new Set([0]);let elapsed=0;
  for(const p of a.phases){times.add(elapsed);times.add(elapsed+1);times.add(elapsed+Math.floor(p.ms/2));elapsed+=p.ms;times.add(elapsed-1);times.add(elapsed);}
  times.add(elapsed+1);times.add(4294967295);
  for(const ms of times){
   const f=sample(a,ms),close=ai!==6&&ai!==7,e=f.effects[0];
   tests.push({input:[mi,oi,ai,ms],expected:[f.pose,props[f.prop],attachments[f.propAttachment],e?(e.id==='ball'?1:2):0,
    close?56+2*f.x:66+f.x,close?60+2*f.y:118+f.y,close?512:256,
    e?(close?e.x*2-76:e.x):0,e?(close?e.y*2-176:e.y):0,
    +(f.phase==='flash'),+(f.phase==='scroll'),f.progressQ8,+f.done,f.duration,f.elapsed]});
  }
 }
}
process.stdout.write(JSON.stringify(tests));
'''
PROBE = r'''
#include "stayc_scene.h"
#include <stdio.h>
int main(void){
 unsigned member,outfit,action,time;
 while(scanf("%u %u %u %u",&member,&outfit,&action,&time)==4){
  stayc_frame_t f;stayc_sample(member,outfit,action,time,&f);
  printf("%u %u %u %u %d %d %d %d %d %u %u %u %u %u %u\n",
   f.pose,f.prop,f.attachment,f.effect,f.x,f.y,f.scale_q8,f.effect?f.effect_x:0,f.effect?f.effect_y:0,
   f.flash,f.scroll,f.progress_q8,f.done,f.duration,f.elapsed);
 }
 return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="stayc-reference-tests-") as directory:
        directory = Path(directory)
        node, probe, binary = directory / "reference.mjs", directory / "probe.c", directory / "probe"
        node.write_text(NODE)
        probe.write_text(PROBE)
        tests = json.loads(subprocess.check_output(["node", str(node), str(REFERENCE)], text=True))
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Imain/h2h", str(probe), "main/h2h/stayc_scene.c", "main/h2h/h2h_model.c",
                        "assets/images/stayc/actions.c", "assets/images/stayc/sprites.c", "-o", str(binary)],
                       cwd=ROOT, check=True)
        inputs = "\n".join(" ".join(map(str, t["input"])) for t in tests) + "\n"
        output = subprocess.check_output([str(binary)], input=inputs, text=True).splitlines()
        assert len(output) == len(tests)
        for test, line in zip(tests, output):
            actual = list(map(int, line.split()))
            assert actual == test["expected"], (test["input"], actual, test["expected"])
        print(f"STAYC original JS/native rich-action comparison: PASS ({len(tests)} samples, all 12 outfits; phase boundaries, finite clocks, scroll/flash and retargeted trajectories)")


if __name__ == "__main__":
    main()
