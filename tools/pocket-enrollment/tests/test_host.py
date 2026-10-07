"""Public fixed synthetic vectors; no USB/SSH, key RNG or real registration."""
import contextlib
import copy
import hashlib
import hmac
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import host
P={"v":1,"device_id":"112233445566778899aabbccddeeff00","chip_mac":"112233445566","helper_elf_sha256":"0123456789abcdef"*4,"nonce":"ffeeddccbbaa99887766554433221100","operation_id":"00112233445566778899aabbccddeeff","allow_create_relay_only":False,"slug":"aa"*16,"expected_owner_id":None}
K=bytearray(range(32))  # Public SYNTHETIC ONLY; never a real enrollment key.
PASS="synthetic recovery test passphrase"
APP={"v":1,"approval":"approved","device_id":P["device_id"],"chip_mac":P["chip_mac"],"usb_serial_sha256":"01"*32,"helper_app_sha256":"02"*32,"helper_elf_sha256":P["helper_elf_sha256"],"r6_app_sha256":"03"*32,"server_script_sha256":"04"*32,"server":host.SERVER,"allow_create_relay_only":False,"capsule_directory":"/tmp/never-used-synthetic","expected_owner_id":None}
def receipt():
    return {"v":1,"status":"written","device_id":P["device_id"],"chip_mac":P["chip_mac"],"helper_elf_sha256":P["helper_elf_sha256"],"nonce":P["nonce"],"operation_id":P["operation_id"],"key_size":32,"proof":hmac.new(K,host.context(P),hashlib.sha256).hexdigest()}
class FakeUSB:
    def __init__(self,events,cut=None):
        self.events=events;self.cut=cut;self.writes=0
    def send(self,obj):
        self.events.append(obj["op"])
        if obj["op"]=="install":
            self.writes+=1
            if self.cut=="send":
                host.fail("DEVICE_RESPONSE_UNCONFIRMED")
    def wait(self,accepted,*args):
        status=next(iter(accepted));self.events.append(status)
        if status==self.cut:
            host.fail("DEVICE_RESPONSE_UNCONFIRMED")
        return receipt() if status=="written" else {"status":status}
class HostTests(unittest.TestCase):
    def test_encrypted_recovery_and_bound_aad(self):
        c=host.seal(P,K,PASS,bytes(range(16)),bytes(range(12)))
        self.assertEqual(host.unseal(c,PASS,P),K)
        self.assertNotIn(bytes(K).hex(),host.canonical(c).decode())
        for name in ("device_id","chip_mac","helper_elf_sha256","nonce","operation_id","slug"):
            changed=copy.deepcopy(P);changed[name]="0"*len(P[name])
            with self.assertRaises(host.SafeError):
                host.unseal(c,PASS,changed)
        with self.assertRaises(host.SafeError):
            host.unseal(c,"wrong synthetic passphrase",P)
        for field in ("salt","nonce","ciphertext"):
            bad=copy.deepcopy(c);bad[field]=("1" if bad[field][0]!="1" else "0")+bad[field][1:]
            with self.assertRaises(host.SafeError):
                host.unseal(bad,PASS,P)
    def test_private_file_exclusive_no_follow_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"test.json";host.persist_new(path,host.canonical(P))
            self.assertEqual(path.stat().st_mode&0o777,0o600)
            self.assertEqual(host.private_read(path),P)
            with self.assertRaises(FileExistsError):
                host.persist_new(path,b"changed")
            link=Path(tmp)/"link.json";link.symlink_to(path)
            with self.assertRaises(OSError):
                host.private_read(link)
            path.chmod(0o644)
            with self.assertRaises(host.SafeError):
                host.private_read(path)
    def test_capsule_file_fsync_failure_prevents_device_arm(self):
        events=[];usb=FakeUSB(events)
        def capsule():
            events.append("capsule");raise OSError("synthetic fsync failed")
        with self.assertRaises(OSError):
            host.run_install(usb,APP,P,K,capsule,lambda _:None,lambda *_:None)
        self.assertEqual(events,["capsule"]);self.assertEqual(usb.writes,0)
    def test_success_order_and_no_stdout_secret(self):
        events=[];usb=FakeUSB(events)
        def register(*args):
            events.append("server");return {"status":"registered"}
        output=io.StringIO()
        with contextlib.redirect_stdout(output):
            host.run_install(usb,APP,P,K,lambda:events.append("durable_capsule"),lambda _:events.append("durable_device_receipt"),register)
        self.assertEqual(events,["durable_capsule","arm","awaiting_button","confirmed","install","written","durable_device_receipt","server"])
        self.assertNotIn(bytes(K).hex(),output.getvalue())
    def test_button_and_usb_failure_boundaries(self):
        for cut in ("awaiting_button","confirmed","send","written"):
            events=[];usb=FakeUSB(events,cut)
            with self.assertRaises(host.SafeError):
                host.run_install(usb,APP,P,K,lambda:events.append("capsule"),lambda _:events.append("receipt"),lambda *_:events.append("server"))
            self.assertNotIn("receipt",events);self.assertNotIn("server",events)
            self.assertEqual(usb.writes,1 if cut in ("send","written") else 0)
    def test_device_ack_persistence_failure_stops_server_no_second_write(self):
        events=[];usb=FakeUSB(events)
        def persist(_):
            events.append("receipt-failed");raise OSError("synthetic failure")
        with self.assertRaises(OSError):
            host.run_install(usb,APP,P,K,lambda:events.append("capsule"),persist,lambda *_:events.append("server"))
        self.assertEqual(usb.writes,1);self.assertNotIn("server",events)
    def test_server_failure_preserves_prior_durable_evidence(self):
        events=[];usb=FakeUSB(events)
        def server(*_):
            events.append("server");host.fail("SERVER_RESULT_UNCONFIRMED")
        with self.assertRaises(host.SafeError):
            host.run_install(usb,APP,P,K,lambda:events.append("capsule"),lambda _:events.append("receipt"),server)
        self.assertEqual(events[-2:],["receipt","server"]);self.assertEqual(usb.writes,1)
    def test_wrong_receipt_rejected_without_register(self):
        for field in ("status","device_id","chip_mac","helper_elf_sha256","nonce","operation_id","proof","key_size"):
            r=receipt();r[field]=0 if field=="key_size" else "0"*len(r[field])
            self.assertFalse(host.receipt_valid(r,P,K))
        self.assertTrue(host.receipt_valid(receipt(),P,K))
    def test_identity_approval_and_duplicate_strictness(self):
        self.assertEqual(host.check_approval(APP),APP)
        for field in ("device_id","chip_mac","helper_elf_sha256","usb_serial_sha256","server","approval"):
            a=copy.deepcopy(APP);a[field]="bad"
            with self.assertRaises(host.SafeError):
                host.check_approval(a)
        for data in (b'{"v":1,"v":2}',b'{"v":1,"\\u0076":2}',b'{"x":NaN}'):
            with self.assertRaises(host.SafeError):
                host.parse(data)
        h={"v":1,**{k:P[k] for k in ("device_id","chip_mac","helper_elf_sha256","nonce")},"status":"ready","key_size":-1}
        self.assertEqual(host.check_hello(h,APP),h)
        for field in ("device_id","chip_mac","helper_elf_sha256","nonce","status","key_size"):
            bad=copy.deepcopy(h);bad[field]="bad"
            with self.assertRaises(host.SafeError):
                host.check_hello(bad,APP)
    def test_non_tty_prevents_secret_entry(self):
        with patch.object(host.sys.stdin,"isatty",return_value=False), patch.object(host.getpass,"getpass") as ask:
            with self.assertRaises(host.SafeError):
                host.password()
            ask.assert_not_called()
    def test_remote_errors_do_not_echo_secret_sql(self):
        proc=type("P",(),{"returncode":1,"stdout":b"", "stderr":bytes(K).hex().encode()})()
        with patch.object(host.subprocess,"run",return_value=proc) as run:
            with self.assertRaisesRegex(host.SafeError,"^SERVER_RESULT_UNCONFIRMED$"):
                host.server_register(P,K,receipt()["proof"],APP)
            args=run.call_args.args[0]
            self.assertNotIn(bytes(K).hex(),str(args))
            self.assertIn("StrictHostKeyChecking=yes",args)
            self.assertEqual(host.parse(run.call_args.kwargs["input"])["key_hex"],bytes(K).hex())
if __name__=="__main__":
    # Force accidental entropy use in development tests to fail immediately.
    with patch.object(host.secrets,"token_bytes",side_effect=AssertionError("NO_REAL_RNG_IN_SYNTHETIC_TESTS")), patch.object(host.secrets,"token_hex",side_effect=AssertionError("NO_REAL_RNG_IN_SYNTHETIC_TESTS")):
        unittest.main()
