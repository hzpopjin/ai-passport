"""Check shipped audio, images, generated font coverage and packer failure paths."""
import hashlib,importlib.util,json,re,struct,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('packer',ROOT/'tools/h2h/pack_music.py');packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
class Assets(unittest.TestCase):
    def test_music_integrity_and_opus_header(self):
        catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
        self.assertGreater(len(catalog['tracks']),0)
        self.assertEqual(catalog['encoding'],'Opus CBR 16000 bps, mono, 48000 Hz, 20 ms')
        self.assertEqual(catalog['song_count'],7)
        self.assertEqual(len(catalog['tracks']),14)
        self.assertLessEqual(catalog['flash_audio_bytes'],5*1024*1024)
        self.assertEqual(catalog['seek_index_bytes'],0)
        for song in range(catalog['song_count']):
            self.assertEqual(catalog['tracks'][song]['title'],catalog['tracks'][song+catalog['song_count']]['title'])
        for track in catalog['tracks']:
            data=(ROOT/'assets/music/h2h'/track['file']).read_bytes()
            self.assertEqual(len(data),track['bytes']);self.assertEqual(hashlib.sha256(data).hexdigest(),track['sha256'])
            self.assertTrue(data.startswith(b'OggS'))
            pos=data.index(b'OpusHead');self.assertEqual(data[pos+9],1)
            self.assertEqual(struct.unpack_from('<I',data,pos+12)[0],48000)
            self.assertGreater(track['duration_ms'],0)
    def test_seek_index_and_rejection(self):
        catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
        for track in catalog['tracks']:
            data=(ROOT/'assets/music/h2h'/track['file']).read_bytes()
            packets,pre,pcm=packer.opus_index(data)
            self.assertEqual(len(packets),track['packet_count'])
            self.assertEqual(round(pcm/48),track['duration_ms'])
            self.assertEqual(track['seek_index_bytes'],0)
            raw=(ROOT/'assets/music/h2h'/track['flash_file']).read_bytes()
            self.assertEqual(raw,b''.join(data[offset:offset+length] for offset,length in packets))
            self.assertEqual(len(raw),track['flash_bytes'])
            self.assertEqual(hashlib.sha256(raw).hexdigest(),track['flash_sha256'])
            self.assertEqual(len(raw),len(packets)*packer.PACKET_BYTES)
            self.assertGreater(pre,0)
            for offset,length in packets:self.assertTrue(length==40 and offset+length<=len(data))
            for invalid in (data[:-1],data[1:],data+data,b'',data[:100]):
                with self.assertRaises(ValueError):packer.opus_index(invalid)
    def test_sprite_dimensions(self):
        for outfit in range(3):
            for pose in range(5):
                data=(ROOT/f'assets/images/h2h/ian_{outfit}_{pose}.png').read_bytes()
                self.assertEqual(data[:8],b'\x89PNG\r\n\x1a\n');self.assertEqual(struct.unpack('>II',data[16:24]),(64,80))
        c=(ROOT/'assets/images/h2h/sprites.c').read_text()
        self.assertEqual(c.count('.data_size=15360'),15)
        for outfit in range(3):
            for pose in range(5):
                block=re.search(rf'ian_{outfit}_{pose}_data\[\] = \{{(.*?)\}};',c,re.S).group(1)
                values=[int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',block)]
                alpha=values[64*80*2:];self.assertEqual(len(alpha),64*80)
                rows=[i//64 for i,a in enumerate(alpha) if a>=128]
                self.assertEqual((min(rows),max(rows)),(4,77),'Character height or baseline changed')
    def test_font_inventory(self):
        inventory=set((ROOT/'assets/fonts/h2h/glyphs.txt').read_text())
        for src in (ROOT/'main/h2h').glob('*.c'):
            for char in set(re.findall('[\u3400-\u9fff]',src.read_text())): self.assertIn(char,inventory)
        for track in json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())['tracks']:
            for char in track['title']+track['artist']:self.assertIn(char,inventory)
        font=(ROOT/'assets/fonts/h2h/h2h_font_16.c').read_text()
        self.assertIn('h2h_font_16',font)
        self.assertNotIn(chr(0x9F98),inventory)
    def test_missing_input_keeps_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp);marker=out/'catalog.json';marker.write_text('original')
            with self.assertRaises(ValueError):packer.pack([{'file':str(out/'missing.mp3'),'title':'Song','artist':'Artist'}],out)
            self.assertEqual(marker.read_text(),'original')
    def test_empty_and_invalid_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):packer.pack([],Path(tmp))
            with self.assertRaises(ValueError):packer.pack([{}]*100,Path(tmp))
if __name__=='__main__':unittest.main()
