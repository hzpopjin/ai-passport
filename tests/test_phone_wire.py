import json,subprocess,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PROTOCOL='pocket-phone-jwt-v1'
DEVICE='11'*16
class WireTests(unittest.TestCase):
 def setUp(self):self.p=subprocess.Popen([str(ROOT/'build/phone_host')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);self.addCleanup(self.close)
 def close(self):
  self.p.terminate();self.p.wait(timeout=5);self.p.stdin.close();self.p.stdout.close()
 def send(self,body):self.p.stdin.write(json.dumps(body)+'\n');self.p.stdin.flush();return json.loads(self.p.stdout.readline())
 def rpc(self,action='phone_status',card=None,authority=None,**fields):
  body=dict(protocol=PROTOCOL,request_id='aa'*16,action=action,**fields)
  if card is not None:body.update(expected_epoch=card['ownership_epoch'],expected_generation=card['save_generation'],authority=authority)
  return self.send(body)
 def authority(self,**change):return dict(protocol=PROTOCOL,device_id=DEVICE,ownership_epoch=1,account_id=7,activated=True,save_status='activated',save_revision=0,save=None,restore_required=False,**change)
 def test_activation_is_one_durable_journal_and_reboot_keeps_actual_record(self):
  card=self.rpc();self.assertFalse(card['activated']);card=self.rpc('phone_apply',card,self.authority());self.assertEqual(card['state'],'ok');self.assertTrue(card['dirty']);self.assertTrue(card['durable']);self.assertEqual(card['account_id'],7)
  self.send({'test_control':'reboot'});self.assertEqual(self.rpc()['save'],card['save'])
 def test_storage_faults_cannot_claim_activation(self):
  for fault in ['fail_bank','fail_pointer']:
   self.send({'test_control':fault});failed=self.rpc('phone_apply',self.rpc(),self.authority());self.assertEqual(failed['code'],'phone_storage_failed');self.assertFalse(failed['activated']);self.assertFalse(failed['durable'])
   self.send({'test_control':'reboot'});self.assertFalse(self.rpc()['activated'])
 def test_extra_token_duplicate_field_wrong_device_and_owner_are_rejected(self):
  card=self.rpc(jwt='public-synthetic');self.assertEqual(card['code'],'phone_invalid_fields')
  a=self.authority();a['device_id']='ff'*16;self.assertEqual(self.rpc('phone_apply',self.rpc(),a)['code'],'phone_invalid_authority')
  card=self.rpc('phone_apply',self.rpc(),self.authority());a=self.authority();a['account_id']=8;self.assertEqual(self.rpc('phone_apply',card,a)['code'],'phone_owner_mismatch')
 def test_inflight_generation_cannot_discard_newer_progress(self):
  card=self.rpc('phone_apply',self.rpc(),self.authority());self.send({'test_control':'advance'});self.assertEqual(self.rpc('phone_apply',card,self.authority())['code'],'phone_local_changed');self.assertNotEqual(self.rpc()['save']['sha256'],card['save']['sha256'])
 def test_conflict_explicit_local_restore_keeps_recovery_and_ack_across_reboot(self):
  card=self.rpc('phone_apply',self.rpc(),self.authority());saved=card['save'];a=self.authority();a.update(save_status='updated',save_revision=1,save=dict(saved,revision=1))
  card=self.rpc('phone_apply',card,a);self.assertFalse(card['dirty'])
  self.send({'test_control':'advance'});local=self.rpc();a.update(save_status='revision_conflict',save_revision=2,save=dict(saved,revision=2))
  conflict=self.rpc('phone_apply',local,a);self.assertEqual(conflict['code'],'phone_save_conflict');self.assertTrue(conflict['conflict']);self.assertEqual(conflict['save'],local['save'])
  self.assertTrue(self.send({'test_control':'restore'})['restored']);self.send({'test_control':'reboot'});restored=self.rpc()
  self.assertFalse(restored['conflict']);self.assertTrue(restored['restore_ack']);self.assertEqual(restored['save']['sha256'],saved['sha256']);self.assertEqual(restored['save_revision'],2)
  a.update(save_status='unchanged');done=self.rpc('phone_apply',restored,a);self.assertEqual(done['state'],'ok');self.assertFalse(done['restore_ack']);self.assertTrue(done['durable'])
if __name__=='__main__':unittest.main()
