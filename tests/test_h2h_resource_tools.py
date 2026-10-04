"""Use tracked image bytes and real partition bounds; never create official assets."""
import csv
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools/h2h'/f'{name}.py')
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
class ResourceTools(unittest.TestCase):
    def test_actual_source_and_budget(self):
        packer=module('build_visual_resource');budget=module('resource_budget')
        data,entry=packer.build('h2h-ian-visual','HEARTS2HEARTS','IAN','IAN visual room')
        numbers=budget.audit()
        self.assertEqual(len(data),numbers['visual_payload_bytes']);self.assertEqual(len(data),233984)
        self.assertEqual(entry['manifest']['signature'],'');self.assertEqual(entry['member'],'IAN')
        self.assertEqual(numbers['default_resource_capacity'],0)
        self.assertEqual(numbers['full_audio_visual_ab_excess_lower_bound'],1878203)
        self.assertEqual(numbers['app_bytes'],None)
        self.assertEqual(numbers['known_embedded_data_lower_bound'],3411595)
        with self.assertRaises(ValueError):budget.audit(0x6f0001)
        budget.audit(0x6f0000)
    def test_opt_in_layout_preserves_nvs_phy_and_has_no_ota(self):
        rows=[row for row in csv.reader((ROOT/'config/partitions-pocket-visual.csv').read_text().splitlines()) if row and not row[0].startswith('#')]
        slots={row[0].strip():tuple(x.strip() for x in row) for row in rows}
        self.assertEqual(set(slots),{'nvs','phy_init','factory','pocket_a','pocket_b'})
        expected={'nvs':(0x9000,0x6000),'phy_init':(0xf000,0x1000),'factory':(0x10000,0x770000),'pocket_a':(0x780000,0x40000),'pocket_b':(0x7c0000,0x40000)}
        for name,(start,size) in expected.items():
            self.assertEqual((int(slots[name][3],0),int(slots[name][4],0)),(start,size))
            self.assertLessEqual(start+size,0x800000)
        with self.assertRaises(ValueError):module('build_visual_resource').build('../escape','H2H','IAN','IAN')
if __name__=='__main__':unittest.main()
