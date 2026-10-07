#!/usr/bin/env python3
"""Compare the native session controls with published JS + the real v15 WASM.

Every model field, V4 save byte, history entry/cursor and random read is compared.
The native-only 256-entry history limit is checked separately against the suffix
of genuine browser history; it is deliberately not claimed as unlimited parity.
"""
import ctypes
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "tools/h2h/reference/v15"
HASHES = {
    "player-controls.mjs": "4be7e5b88c16655cef53b461e5cd72b2f19edf4014f3a415ba2f1c2feac10533",
    "model.wasm": "0ea799bb898a3a7f21bd2baee42934327c0f94396d23b36e0ef582c50e1a9fbc",
}
RANDOM_VALUES = [0, .19, .2, .399999999999, .4, .6, .8, .999999999999,
                 1, -1, "NaN", "Infinity", "-Infinity", .54321]
COMMANDS = {"previous": 0, "next": 1, "mode": 2, "toggle": 3, "open": 4}
NODE = r'''
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=process.argv[2],scenarios=JSON.parse(fs.readFileSync(0,'utf8'));
const {createRuntime}=await import(pathToFileURL(path.join(root,'runtime.mjs')));
const {createPlayerControls}=await import(pathToFileURL(path.join(root,'player-controls.mjs')));
const wasmBytes=fs.readFileSync(path.join(root,'model.wasm'));
const output=[];
for(const scenario of scenarios){
 const data=new Map();let reads=0;
 const runtime=await createRuntime({wasmBytes,tracks:scenario.tracks??12,songs:scenario.songs??6,
  storage:{getItem:k=>data.get(k)??null,setItem:(k,v)=>data.set(k,v)},
  locks:{request:async(_n,_o,fn)=>fn({})},now:()=>0});
 const controls=createPlayerControls(runtime,{paired:(scenario.tracks??12)>(scenario.songs??6),
  random:()=>Number(scenario.random[reads++%scenario.random.length])});
 function capture(status=0){
  const state=runtime.state();
  // These four fields belong to browser storage, outside the native player.
  for(const key of ['save_blocked','storage_status','writer_status','migrated_from_legacy'])delete state[key];
  return {state,save_hex:Buffer.from(runtime.saveBytes()).toString('hex'),history:controls.history(),reads,status};
 }
 const records=[capture()];
 for(const op of scenario.ops){
  let status=0;
  try{
   if(op[0]==='input')controls.dispatch('input',{key:op[1],event:op[2]});
   else if(op[0]==='cmd')controls.command(op[1],op[2]);
   else if(op[0]==='tick'){runtime.tick(op[1]);controls.observe(runtime.state());}
   else if(op[0]==='audio'){
    const s=runtime.state(),generation=op[1]==='current'?s.generation:Math.max(0,s.generation-1);
    controls.dispatch('audio',{generation,ms:op[2],position_ms:op[3],duration_ms:op[4],ended:op[5],failed:op[6]});
   }else if(op[0]==='suspend')runtime.setSuspended(op[1]);
   else throw Error('test operation');
  }catch(e){
   if(e instanceof RangeError)status=1;
   else if(e.message.includes('倒计时'))status=3;
   else if(e.message.includes('暂时无法'))status=4;
   else if(e.message.includes('请先'))status=2;
   else throw e;
  }
  records.push(capture(status));
 }
 output.push(records);
}
process.stdout.write(JSON.stringify(output));
'''
PROBE = r'''
#define preview_input raw_preview_input
#define preview_tick raw_preview_tick
#define preview_audio raw_preview_audio
#include "stayc_game_bridge.c"
#undef preview_input
#undef preview_tick
#undef preview_audio
#include "stayc_game_player.h"
static stayc_player_t player;
static double values[32];
static unsigned value_count, reads;
static bool suspended;
static double sample(void *context) { (void)context; return values[reads++ % value_count]; }
void player_probe_init(unsigned tracks,unsigned songs,const double *stream,unsigned count) {
    preview_init(tracks,songs);
    memcpy(values,stream,count*sizeof(values[0]));value_count=count;reads=0;suspended=false;
    if(count)stayc_player_init(&player,&model,sample,NULL);
    else { memset(&player,0,sizeof(player));stayc_player_observe(&player,&model); }
}
void player_probe_input(unsigned key,unsigned event) { stayc_player_input(&player,&model,key,event); }
unsigned player_probe_command(unsigned command,unsigned value) { return stayc_player_command(&player,&model,command,value); }
void player_probe_tick(unsigned ms) { stayc_game_tick(&model,ms);stayc_player_observe(&player,&model); }
void player_probe_suspend(unsigned value) { suspended=value!=0; }
void player_probe_audio(unsigned generation,unsigned ms,unsigned position,unsigned duration,unsigned ended,unsigned failed) {
    /* The runtime/main caller owns the late ended guard, as in runtime.mjs. */
    if(ended&&(suspended||!model.want_play||model.audio_error||generation!=model.generation))return;
    if(generation==model.generation) {
        preview_position(position,duration,generation);
        if(ms&&model.want_play)stayc_game_listened(&model,ms);
        if(ended||failed)stayc_player_end(&player,&model,generation,failed!=0);
    }
    stayc_player_observe(&player,&model);
}
unsigned player_probe_length(void) { return player.length; }
unsigned player_probe_cursor(void) { return player.cursor; }
unsigned player_probe_track(unsigned index) { return stayc_player_history_track(&player,index); }
unsigned player_probe_reads(void) { return reads; }
unsigned player_probe_truncated(void) { return player.truncated; }
unsigned player_probe_size(void) { return sizeof(player); }
'''


def tap(ops, key):
    ops.extend([["input", key, 0], ["input", key, 1]])


def back(ops):
    ops.extend([["input", 2, 0], ["input", 2, 2], ["input", 2, 1]])


def menu_row(ops, row):
    back(ops)
    for _ in range(row):
        tap(ops, 1)


def scenarios():
    result = []
    # Six songs x full/chorus x three modes x paused/playing x countdown enabled.
    for chorus in (False, True):
        for mode in range(3):
            for playing in (False, True):
                for countdown in (False, True):
                    for song in range(6):
                        ops = [["cmd", "open"]]
                        if chorus:
                            menu_row(ops, 2)
                            tap(ops, 2)
                        ops.append(["cmd", "mode", mode])
                        # Return HOME/MUSIC paths through input, no synthetic state.
                        if countdown:
                            if not chorus:
                                back(ops)
                            else:
                                # mode leaves MUSIC unchanged, currently row 3 or 2.
                                # Reset row by entering PLAYER then backing to MUSIC.
                                ops.append(["cmd", "open"])
                                back(ops)
                            for _ in range(4):
                                tap(ops, 1)
                            tap(ops, 2)
                        ops.append(["cmd", "open"])
                        menu_row(ops, 1)
                        tap(ops, 2)
                        for _ in range(song):
                            tap(ops, 1)
                        tap(ops, 2)
                        if not playing:
                            ops.append(["cmd", "toggle"])
                        elif countdown:
                            ops.append(["tick", 5000])
                        setup_index = len(ops)
                        # Direction commands and raw physical triples both paths.
                        for direction in ("previous", "next", "next", "previous", "next"):
                            ops.append(["cmd", direction])
                        for key in (0, 1, 1, 0):
                            for _ in range(3):
                                tap(ops, key)
                        ops.extend([["tick", 5000], ["cmd", "mode", (mode + 1) % 3],
                                    ["audio", "current", 1200, 1200, 30000, False, False],
                                    ["audio", "current", 100, 30000, 30000, True, False]])
                        result.append(dict(name=f"catalog-{chorus}-{mode}-{playing}-{countdown}-{song}",
                                           ops=ops, random=RANDOM_VALUES, setup_index=setup_index,
                                           setup=dict(chorus=chorus, mode=mode, play=playing,
                                                      countdown_on=int(countdown),
                                                      track=song + (6 if chorus else 0))))
    ops = [["cmd", "previous"], ["cmd", "next"], ["cmd", "mode", 3], ["cmd", "toggle"]]
    tap(ops, 2)  # ROOM opens; command open backs through HOME to MUSIC/PLAYER.
    ops.extend([["cmd", "open"], ["cmd", "mode", 1]])
    for _ in range(40):
        ops.append(["cmd", "next"])
    for _ in range(5):
        ops.append(["cmd", "previous"])
    for _ in range(5):
        ops.append(["cmd", "next"])
    # External track selection after previous truncates future history.
    ops.extend([["cmd", "previous"], ["cmd", "previous"]])
    menu_row(ops, 1)
    tap(ops, 2)
    tap(ops, 1)
    tap(ops, 2)
    ops.extend([["cmd", "next"], ["cmd", "toggle"],
                ["audio", "current", 0, 9000, 20000, True, False],
                ["cmd", "toggle"], ["suspend", True],
                ["audio", "current", 0, 20000, 20000, True, False],
                ["suspend", False], ["audio", "stale", 2000, 10000, 20000, True, False],
                ["audio", "current", 0, 1000, 20000, False, True],
                ["audio", "current", 0, 20000, 20000, True, False],
                ["cmd", "toggle"], ["audio", "current", 2000, 2000, 20000, True, False]])
    # Seek long presses, click suppression, tap/volume boundaries.
    for key in (0, 1):
        ops.extend([["input", key, 0], ["input", key, 2], ["input", key, 1]])
        tap(ops, key)
        ops.append(["tick", 279])
        tap(ops, key)
        ops.append(["tick", 280])
    # Reset on chorus then mode, and same-mode no-op including countdown.
    menu_row(ops, 2)
    tap(ops, 2)
    ops.extend([["cmd", "mode", 2], ["cmd", "open"]])
    menu_row(ops, 4)
    tap(ops, 2)
    ops.extend([["cmd", "next"], ["cmd", "mode", 2], ["cmd", "mode", 1],
                ["cmd", "toggle"], ["cmd", "mode", 1], ["cmd", "next"]])
    # DEV switch is an actual Settings input route; history resets on each epoch.
    back(ops)
    back(ops)
    for _ in range(5):
        tap(ops, 1)
    tap(ops, 2)
    for _ in range(4):
        tap(ops, 1)
    tap(ops, 2)
    ops.extend([["cmd", "open"]])  # rejected in Settings, unlike shortcut state assignment.
    tap(ops, 2)
    back(ops)
    ops.extend([["cmd", "open"], ["cmd", "mode", 1], ["cmd", "next"]])
    result.append(dict(name="session-history-errors-seek-audio-dev", ops=ops, random=RANDOM_VALUES))
    for tracks, songs in ((1, 1), (2, 1), (6, 6)):
        result.append(dict(name=f"catalog-edge-{tracks}-{songs}", tracks=tracks, songs=songs,
                           random=RANDOM_VALUES, ops=[["cmd", "open"], ["cmd", "mode", 1],
                           ["cmd", "previous"], ["cmd", "next"], ["cmd", "previous"],
                           ["cmd", "next"], ["cmd", "mode", 2], ["cmd", "next"]]))
    # Keep state parity on overflow, compare native history with browser suffix.
    result.append(dict(name="bounded-history", bounded=True, random=RANDOM_VALUES,
                       ops=[["cmd", "open"], ["cmd", "mode", 1]] + [["cmd", "next"]] * 300))
    # Firmware's zero-initialized session uses its own RNG, never the raw core's.
    seed, fallback_values = 20260929, []
    for _ in range(20):
        seed ^= (seed << 13) & 0xffffffff
        seed ^= seed >> 17
        seed ^= (seed << 5) & 0xffffffff
        fallback_values.append(seed / 4294967296)
    result.append(dict(name="zero-initialized-fallback-rng", native_random=True,
                       random=fallback_values, ops=[["cmd", "open"], ["cmd", "mode", 1]] +
                       [["cmd", "next"]] * 20 + [["cmd", "previous"]] * 4 + [["cmd", "next"]] * 4))
    return result


def native_snapshot(engine, status=0):
    state = json.loads(engine.preview_state())
    saved = ctypes.create_string_buffer(128)
    size = engine.preview_save(saved)
    assert size == 92
    return dict(state=state, save_hex=saved.raw[:size].hex(),
                history=dict(tracks=[engine.player_probe_track(i) for i in range(engine.player_probe_length())],
                             cursor=engine.player_probe_cursor()),
                reads=engine.player_probe_reads(), status=status)


def main():
    for name, expected in HASHES.items():
        assert hashlib.sha256((REFERENCE / name).read_bytes()).hexdigest() == expected, name
    cases = scenarios()
    with tempfile.TemporaryDirectory(prefix="stayc-player-reference-") as directory:
        directory = Path(directory)
        node, probe, library = directory / "reference.mjs", directory / "probe.c", directory / "player.so"
        node.write_text(NODE)
        probe.write_text(PROBE)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-shared", "-fPIC",
                        "-Wall", "-Wextra", "-Werror", "-Imain/h2h", "-Itools/h2h", str(probe),
                        *[f"main/h2h/{name}.c" for name in ("stayc_game", "stayc_game_player", "stayc_game_motion",
                          "stayc_game_life", "stayc_game_reaction", "stayc_game_decor")], "-o", str(library)],
                       cwd=ROOT, check=True)
        expected = json.loads(subprocess.check_output(["node", str(node), str(REFERENCE)],
                                                      input=json.dumps(cases), text=True))
        engine = ctypes.CDLL(str(library))
        engine.preview_state.restype = ctypes.c_char_p
        engine.player_probe_init.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_double), ctypes.c_uint]
        samples, countdown_rejections, resets, overflows = 0, 0, 0, 0
        for case, records in zip(cases, expected):
            stream = (ctypes.c_double * len(case["random"]))(*map(float, case["random"]))
            engine.player_probe_init(case.get("tracks", 12), case.get("songs", 6), stream,
                                     0 if case.get("native_random") else len(stream))
            assert native_snapshot(engine) == records[0], case["name"]
            if "setup" in case:
                setup = records[case["setup_index"]]["state"]
                assert all(setup[key] == value for key, value in case["setup"].items()), (case["name"], setup)
                assert setup["page"] == 3 and setup["countdown"] == 0
            for index, (op, reference) in enumerate(zip(case["ops"], records[1:])):
                status = 0
                if op[0] == "input":
                    engine.player_probe_input(op[1], op[2])
                elif op[0] == "cmd":
                    status = engine.player_probe_command(COMMANDS[op[1]], op[2] if len(op) > 2 else 0)
                elif op[0] == "tick":
                    engine.player_probe_tick(op[1])
                elif op[0] == "suspend":
                    engine.player_probe_suspend(op[1])
                elif op[0] == "audio":
                    generation = json.loads(engine.preview_state())["generation"]
                    if op[1] == "stale":
                        generation = max(0, generation - 1)
                    engine.player_probe_audio(generation, *op[2:])
                actual = native_snapshot(engine, status)
                # All raw model fields + saves still compare on bounded overflow.
                comparison = reference
                if case.get("native_random"):
                    comparison = {**reference, "reads": 0}  # No injected callback in firmware.
                if case.get("bounded") and len(reference["history"]["tracks"]) > 256:
                    comparison = {**reference, "history": dict(tracks=reference["history"]["tracks"][-256:], cursor=255)}
                    assert engine.player_probe_truncated() == 1
                    overflows += 1
                assert actual == comparison, (case["name"], index, op,
                    {k: (actual[k], comparison[k]) for k in actual if actual[k] != comparison[k]})
                countdown_rejections += status == 3
                if reference["state"]["debug_epoch"] != records[index]["state"]["debug_epoch"]:
                    assert len(reference["history"]["tracks"]) == 1
                    resets += 1
                samples += 1
            if case.get("bounded"):
                # Native backtracking at the retained boundary is the stated limit.
                for _ in range(260):
                    assert engine.player_probe_command(COMMANDS["previous"], 0) == 0
                assert engine.player_probe_cursor() == 0 and engine.player_probe_length() == 256
                oldest = engine.player_probe_track(0)
                assert json.loads(engine.preview_state())["track"] == oldest
                before_boundary = native_snapshot(engine)
                engine.player_probe_command(COMMANDS["previous"], 0)
                assert engine.player_probe_cursor() == 0
                assert json.loads(engine.preview_state())["track"] == oldest
                assert engine.player_probe_reads() == before_boundary["reads"]
                engine.player_probe_command(COMMANDS["mode"], 0)
                assert engine.player_probe_truncated() == 0 and engine.player_probe_length() == 1
        assert countdown_rejections > 0 and resets >= 2 and overflows > 0
        assert engine.player_probe_size() <= 384, engine.player_probe_size()
        print(f"STAYC published JS/real-WASM/native player: PASS ({samples} state/save/history samples; "
              f"{len(cases)} scenarios; six songs/full+chorus; all modes; paused/countdown; "
              f"random past/future; seek/audio/DEV; {countdown_rejections} countdown guards; "
              f"256-entry bound, {engine.player_probe_size()}-byte host session)")


if __name__ == "__main__":
    main()
