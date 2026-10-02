#!/usr/bin/env python3
"""Compare packed Opus decoding and seek windows with FFmpeg on the host.
Requires FFmpeg and a local libopus shared library; this is not an ESP32 test.
"""
import array
import ctypes as C
import ctypes.util
import json
import subprocess
from pathlib import Path
from pack_music import ROOT, opus_index

def verify(path):
    library=ctypes.util.find_library('opus')
    if not library: raise SystemExit('Install libopus to run host seek verification.')
    opus=C.CDLL(library)
    opus.opus_decoder_create.argtypes=[C.c_int,C.c_int,C.POINTER(C.c_int)];opus.opus_decoder_create.restype=C.c_void_p
    opus.opus_decoder_destroy.argtypes=[C.c_void_p]
    opus.opus_decode.argtypes=[C.c_void_p,C.c_void_p,C.c_int,C.c_void_p,C.c_int,C.c_int];opus.opus_decode.restype=C.c_int
    data=path.read_bytes();packets,pre,pcm=opus_index(data)
    reference=subprocess.run(['ffmpeg','-v','error','-c:a','libopus','-i',str(path),'-f','s16le','-acodec','pcm_s16le','-'],check=True,stdout=subprocess.PIPE).stdout
    assert len(reference)==pcm*2
    def decode(position_ms,limit=None):
        target=pre+position_ms*48;start=max(0,(target-24000)//960)
        error=C.c_int();decoder=opus.opus_decoder_create(48000,1,C.byref(error));assert decoder and not error.value
        result=bytearray();buffer=(C.c_int16*960)()
        try:
            for index in range(start,len(packets)):
                offset,length=packets[index];packet=data[offset:offset+length]
                n=opus.opus_decode(decoder,packet,length,buffer,960,0);assert n==960
                first=max(index*960,target);last=min((index+1)*960,pre+pcm)
                if last>first: result.extend(bytes(buffer)[(first-index*960)*2:(last-index*960)*2])
                if limit and len(result)>=limit*2:break
        finally:opus.opus_decoder_destroy(decoder)
        return bytes(result[:limit*2] if limit else result)
    complete=decode(0);assert len(complete)==len(reference)
    expected=array.array('h',reference);actual=array.array('h',complete)
    # Independent fixed/floating point implementations can differ by one LSB.
    full_max=max(abs(a-b) for a,b in zip(expected,actual));assert full_max<=2,full_max
    seeks=[]
    duration_ms=pcm//48
    for ms in sorted({0,1000,10000,duration_ms//2,max(0,duration_ms-20)}):
        if ms>=duration_ms: continue
        samples=min(9600,pcm-ms*48);a=array.array('h',decode(ms,samples));b=expected[ms*48:ms*48+samples]
        assert len(a)==len(b)==samples
        rms=(sum((x-y)**2 for x,y in zip(a,b))/max(1,samples))**.5
        # Pre-roll converges decoder state; transient differences stay small.
        settled=(sum((x-y)**2 for x,y in zip(a[4800:],b[4800:]))/max(1,samples-4800))**.5
        assert rms<1000 and settled<30, (ms,rms,settled)
        seeks.append({'position_ms':ms,'samples':samples,'rms_difference_int16':round(rms,3),'settled_rms_after_100ms':round(settled,3)})
    return {'decoded_seconds':pcm/48000,'packets':len(packets),'pre_skip_samples':pre,'full_decode_max_difference_int16':full_max,'seek_windows':seeks,'result':'PASS'}
if __name__=='__main__':
    catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
    print(json.dumps([verify(ROOT/'assets/music/h2h'/t['file']) for t in catalog['tracks']],indent=2))
