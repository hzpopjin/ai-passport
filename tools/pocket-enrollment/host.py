#!/usr/bin/env python3
"""Local secure-terminal enrollment. No real key is created by inspect or tests."""
from __future__ import annotations
import argparse
import getpass
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys
import time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

sys.dont_write_bytecode = True
SERVER = "root@115.190.229.100"
SERVER_ROOT = "/www/wwwroot/apps.randomdance.cn/backend/build/pocket-enrollment"
PUBLIC_FIELDS = ("v", "device_id", "chip_mac", "helper_elf_sha256", "nonce", "operation_id", "allow_create_relay_only", "slug", "expected_owner_id")
class SafeError(Exception):
    pass
def fail(code):
    raise SafeError(code)
def hex_value(value, length):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{%d}" % length, value) is not None
def unique(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            fail("DUPLICATE_JSON_FIELD")
        obj[key] = value
    return obj
def parse(data, limit=8192):
    if len(data) > limit:
        fail("INPUT_TOO_LONG")
    try:
        return json.loads(data, object_pairs_hook=unique, parse_constant=lambda _: fail("INVALID_JSON"))
    except (ValueError, UnicodeError, TypeError):
        fail("INVALID_JSON")
def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("ascii")
def context(p):
    return "\n".join(("pocket-enrollment-v1", p["device_id"], p["chip_mac"], p["helper_elf_sha256"], p["nonce"], p["operation_id"], "written")).encode("ascii")
def public_input(p):
    if not isinstance(p, dict) or set(p) != set(PUBLIC_FIELDS) or type(p["v"]) is not int or p["v"] != 1 or type(p["allow_create_relay_only"]) is not bool:
        fail("PUBLIC_INPUT_INVALID")
    for name, size in (("device_id",32),("chip_mac",12),("helper_elf_sha256",64),("nonce",32),("operation_id",32),("slug",32)):
        if not hex_value(p[name], size):
            fail("PUBLIC_INPUT_INVALID")
    if p["expected_owner_id"] is not None and (type(p["expected_owner_id"]) is not int or p["expected_owner_id"]<1 or p["expected_owner_id"]>2**53-1):
        fail("PUBLIC_INPUT_INVALID")
    return p
def receipt_valid(receipt, p, key):
    return isinstance(receipt,dict) and receipt.get("v")==1 and receipt.get("status")=="written" and receipt.get("key_size")==32 and all(receipt.get(k)==p[k] for k in ("device_id","chip_mac","helper_elf_sha256","nonce","operation_id")) and hex_value(receipt.get("proof"),64) and hmac.compare_digest(receipt["proof"],hmac.new(key,context(p),hashlib.sha256).hexdigest())
def private_read(path, limit=16384):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    try:
        s=os.fstat(fd)
        if not stat.S_ISREG(s.st_mode) or s.st_size>limit or s.st_uid!=os.getuid() or s.st_mode&0o077:
            fail("PRIVATE_FILE_REQUIRED")
        return parse(os.read(fd,limit+1),limit)
    finally:
        os.close(fd)
def persist_new(path, data):
    """Exclusive file and directory fsync precede any NVS mutation."""
    path=Path(path)
    if path.parent.is_symlink() or not path.parent.is_dir():
        fail("PRIVATE_DIRECTORY_REQUIRED")
    parent=path.parent.stat()
    if parent.st_uid!=os.getuid() or parent.st_mode&0o077:
        fail("PRIVATE_DIRECTORY_REQUIRED")
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:
        view=memoryview(data)
        while view:
            n=os.write(fd,view)
            if n<=0:
                fail("CAPSULE_PERSIST_FAILED")
            view=view[n:]
        os.fsync(fd)
    finally:
        os.close(fd)
    directory=os.open(path.parent,os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
def seal(public, key, password, salt, nonce):
    """Explicit entropy arguments keep synthetic tests deterministic/no real RNG."""
    public_input(public)
    if len(key)!=32 or len(salt)!=16 or len(nonce)!=12 or len(password)<16:
        fail("CAPSULE_INPUT_INVALID")
    wrapping=hashlib.scrypt(password.encode("utf8"),salt=salt,n=32768,r=8,p=1,maxmem=64*1024*1024,dklen=32)
    payload=AESGCM(wrapping).encrypt(nonce,bytes(key),canonical(public))
    return {"v":1,"public":public,"kdf":"scrypt-32768-8-1","salt":salt.hex(),"nonce":nonce.hex(),"ciphertext":payload.hex()}
def unseal(capsule, password, expected):
    if not isinstance(capsule,dict) or set(capsule)!={"v","public","kdf","salt","nonce","ciphertext"} or capsule["v"]!=1 or capsule["kdf"]!="scrypt-32768-8-1" or capsule["public"]!=expected:
        fail("CAPSULE_TARGET_MISMATCH")
    public_input(capsule["public"])
    if not hex_value(capsule["salt"],32) or not hex_value(capsule["nonce"],24) or not hex_value(capsule["ciphertext"],96):
        fail("CAPSULE_INVALID")
    try:
        wrapping=hashlib.scrypt(password.encode("utf8"),salt=bytes.fromhex(capsule["salt"]),n=32768,r=8,p=1,maxmem=64*1024*1024,dklen=32)
        return bytearray(AESGCM(wrapping).decrypt(bytes.fromhex(capsule["nonce"]),bytes.fromhex(capsule["ciphertext"]),canonical(expected)))
    except Exception:
        fail("CAPSULE_AUTH_FAILED")
def check_approval(a):
    fields={"v","approval","device_id","chip_mac","usb_serial_sha256","helper_app_sha256","helper_elf_sha256","r6_app_sha256","server_script_sha256","server","allow_create_relay_only","capsule_directory","expected_owner_id"}
    if not isinstance(a,dict) or set(a)!=fields or a["v"]!=1 or a["approval"]!="approved" or a["server"]!=SERVER or type(a["allow_create_relay_only"]) is not bool:
        fail("EXPLICIT_APPROVAL_REQUIRED")
    for name,size in (("device_id",32),("chip_mac",12),("usb_serial_sha256",64),("helper_app_sha256",64),("helper_elf_sha256",64),("r6_app_sha256",64),("server_script_sha256",64)):
        if not hex_value(a[name],size):
            fail("APPROVAL_INVALID")
    if not isinstance(a["capsule_directory"],str) or not Path(a["capsule_directory"]).is_absolute():
        fail("APPROVAL_INVALID")
    if a["expected_owner_id"] is not None and (type(a["expected_owner_id"]) is not int or not 1<=a["expected_owner_id"]<=2**53-1):
        fail("APPROVAL_INVALID")
    return a
def check_hello(h,a,initial=True):
    if not isinstance(h,dict) or h.get("v")!=1 or any(h.get(k)!=a[k] for k in ("device_id","chip_mac","helper_elf_sha256")) or not hex_value(h.get("nonce"),32):
        fail("DEVICE_TARGET_MISMATCH")
    if initial and (h.get("status")!="ready" or h.get("key_size")!=-1):
        fail("DEVICE_NOT_EMPTY_OR_READY")
    return h
class USB:
    def __init__(self,port,approved):
        import serial
        from serial.tools import list_ports
        candidates=[p for p in list_ports.comports() if p.vid==0x303a and p.pid==0x1001 and p.device.startswith("/dev/cu.")]
        if len(candidates)!=1 or candidates[0].device!=port or not candidates[0].serial_number or hashlib.sha256(candidates[0].serial_number.encode()).hexdigest()!=approved["usb_serial_sha256"]:
            fail("USB_TARGET_NOT_UNIQUE")
        self.serial=serial.Serial(port,115200,timeout=0.2,write_timeout=2,exclusive=True)
        self.serial.reset_input_buffer()
    def send(self,obj):
        data=canonical(obj)+b"\n"
        if len(data)>767:
            fail("USB_INPUT_TOO_LONG")
        self.serial.write(data)
        self.serial.flush()
    def wait(self,accepted,timeout,approved=None,public=None):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            line=self.serial.read_until(b"\n",768)
            if not line.startswith(b"POCKET_ENROLL ") or not line.endswith(b"\n"):
                continue
            value=parse(line[14:],768)
            if approved:
                check_hello(value,approved,False)
            if public and any(value.get(k)!=public[k] for k in ("device_id","chip_mac","helper_elf_sha256","nonce","operation_id")):
                fail("DEVICE_CONTEXT_MISMATCH")
            if value.get("status") in ("indeterminate","refused"):
                fail("DEVICE_WRITE_UNCONFIRMED_OR_REFUSED")
            if value.get("status") in accepted:
                return value
        fail("DEVICE_RESPONSE_UNCONFIRMED")
    def close(self):
        self.serial.close()
def server_register(public,key,proof,approved):
    """Fixed host/path, strict known host, code pin and stdin-only new key."""
    public_input(public)
    if not hex_value(proof,64):
        fail("DEVICE_RECEIPT_REQUIRED")
    # No shell interpolation of secrets. All interpolated IDs/hashes were strict hex.
    program=("import hashlib,os,sys; p='"+SERVER_ROOT+"/server.cjs'; "
             "assert hashlib.sha256(open(p,'rb').read()).hexdigest()=='"+approved["server_script_sha256"]+"'; "
             "os.execvp('node',['node',p,'--apply','--approval','"+SERVER_ROOT+"/"+public["operation_id"]+".json'])")
    import shlex
    command="python3 -c "+shlex.quote(program)
    supplied={**public,"key_hex":bytes(key).hex(),"proof":proof}
    proc=subprocess.run(["ssh","-T","-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","ConnectTimeout=10","-o","LogLevel=ERROR",SERVER,command],input=canonical(supplied),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    # Remote errors may include SQL: never print raw child stderr/stdout.
    if proc.returncode:
        fail("SERVER_RESULT_UNCONFIRMED")
    result=parse(proc.stdout,4096)
    if result.get("device_id")!=public["device_id"] or result.get("operation_id")!=public["operation_id"] or result.get("status") not in ("registered","already_registered_same_new_key"):
        fail("SERVER_RESULT_UNCONFIRMED")
    return result
def run_install(usb,approved,public,key,persist_capsule,persist_receipt,register):
    """Injected boundaries let tests cover fault order without a real key/device."""
    public_input(public)
    # Durability must succeed BEFORE arm or any secret is sent to the card.
    persist_capsule()
    arm={k:public[k] for k in ("v","device_id","chip_mac","helper_elf_sha256","nonce","operation_id")}
    usb.send({**arm,"op":"arm"})
    usb.wait({"awaiting_button"},3,approved,public)
    usb.wait({"confirmed"},58,approved,public)
    usb.send({**arm,"op":"install","key_hex":bytes(key).hex()})
    receipt=usb.wait({"written"},5,approved,public)
    if not receipt_valid(receipt,public,key):
        fail("DEVICE_WRITE_UNCONFIRMED")
    persist_receipt(receipt)  # Interrupted here leaves capsule + key, not success.
    result=register(public,key,receipt["proof"],approved)
    return result
def password(confirm=False):
    if not sys.stdin.isatty() or not sys.stderr.isatty():
        fail("SECURE_LOCAL_TERMINAL_REQUIRED")
    value=getpass.getpass("New recovery passphrase (16+ characters; do not send to chat): ")
    if len(value)<16:
        fail("PASSPHRASE_TOO_SHORT")
    if confirm and value!=getpass.getpass("Confirm recovery passphrase: "):
        fail("PASSPHRASE_MISMATCH")
    return value
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="command",required=True)
    apply=sub.add_parser("apply")
    apply.add_argument("--approval",type=Path,required=True)
    apply.add_argument("--port",required=True)
    resume=sub.add_parser("resume-server")
    resume.add_argument("--approval",type=Path,required=True)
    resume.add_argument("--capsule",type=Path,required=True)
    # The approval file MUST first be recorded after action-time user approval.
    args=parser.parse_args()
    approved=check_approval(private_read(args.approval))
    if args.command=="resume-server":
        capsule=private_read(args.capsule)
        public=public_input(capsule.get("public"))
        if any(public[k]!=approved[k] for k in ("device_id","chip_mac","helper_elf_sha256","allow_create_relay_only","expected_owner_id")) or args.capsule.resolve()!=Path(approved["capsule_directory"]).resolve()/(public["operation_id"]+".capsule.json"):
            fail("CAPSULE_TARGET_MISMATCH")
        # No valid committed-write receipt means no automatic registration.
        receipt=private_read(args.capsule.parent/(public["operation_id"]+".device-receipt.json"))
        key=unseal(capsule,password(),public)
        try:
            if not receipt_valid(receipt,public,key):
                fail("DEVICE_RECEIPT_REQUIRED")
            result=server_register(public,key,receipt["proof"],approved)
            path=args.capsule.parent/(public["operation_id"]+".server-receipt.json")
            if not path.exists():
                persist_new(path,canonical(result))
            print(json.dumps({"status":"enrolled_binding_not_tested","operation_id":public["operation_id"],"device_id":public["device_id"]}))
        finally:
            for i in range(len(key)):
                key[i]=0
        return
    usb=USB(args.port,approved);key=bytearray()
    try:
        hello=check_hello(usb.wait({"ready"},3,approved),approved)
        phrase=password(True)
        operation=secrets.token_hex(16)
        public=public_input({"v":1,**{k:hello[k] for k in ("device_id","chip_mac","helper_elf_sha256","nonce")},"operation_id":operation,"slug":secrets.token_hex(16),"allow_create_relay_only":approved["allow_create_relay_only"],"expected_owner_id":approved["expected_owner_id"]})
        # All live key creation is BELOW explicit approval, identity and terminal gates.
        key.extend(secrets.token_bytes(32))
        capsule=seal(public,key,phrase,secrets.token_bytes(16),secrets.token_bytes(12))
        directory=Path(approved["capsule_directory"])
        # Public server approval transfer contains no key; strict SSH connection only.
        public_file=directory/(operation+".json")
        persist_new(public_file,canonical(public))
        scp=subprocess.run(["scp","-q","-o","BatchMode=yes","-o","StrictHostKeyChecking=yes",str(public_file),SERVER+":"+SERVER_ROOT+"/"+operation+".json"],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
        if scp.returncode:
            fail("SERVER_PUBLIC_APPROVAL_TRANSFER_FAILED")
        print("Release all keys, hold OK at least 3 seconds, then release within 60 seconds.",flush=True)
        result=run_install(usb,approved,public,key,
            lambda:persist_new(directory/(operation+".capsule.json"),canonical(capsule)),
            lambda receipt:persist_new(directory/(operation+".device-receipt.json"),canonical(receipt)),
            server_register)
        persist_new(directory/(operation+".server-receipt.json"),canonical(result))
        print(json.dumps({"status":"enrolled_binding_not_tested","operation_id":operation,"device_id":public["device_id"],"next":"Restore verified R6 and test normal account pairing/BLE save; no debug bypass."}))
    finally:
        for i in range(len(key)):
            key[i]=0
        usb.close()
if __name__=="__main__":
    try:
        main()
    except (SafeError,OSError,subprocess.SubprocessError):
        # Never stringify a library exception: it may contain secret buffers/SQL.
        print("Enrollment failed or remains unconfirmed. Preserve encrypted capsule and receipts; no overwrite/erase/retry of device key.",file=sys.stderr)
        sys.exit(1)
