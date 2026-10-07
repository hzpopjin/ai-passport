import importlib.util,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('legacy',root/'tests/build_h2h_cloud_host.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
cache=Path(os.environ.get('H2H_CJSON_DIR',str(root/'build/host-dependencies/cjson-v1.7.19')))
for name,sha in m.HASHES.items():
 import hashlib
 assert hashlib.sha256((cache/name).read_bytes()).hexdigest()==sha,'preverified cJSON required'
output=root/'build/phone_host';output.parent.mkdir(exist_ok=True)
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Imain/h2h','-I'+str(cache),'tests/phone_host.c',
 'main/h2h/h2h_phone.c','main/h2h/pocket_profile.c','main/h2h/h2h_fullsave.c','main/h2h/h2h_pocket.c','main/h2h/h2h_ownership.c','main/h2h/h2h_model.c','main/h2h/stayc_game.c','main/h2h/stayc_game_store.c',str(cache/'cJSON.c'),'-lcrypto','-lm','-o',str(output)],cwd=root,check=True)
print(output)
