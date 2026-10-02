#!/usr/bin/env python3
"""Create an allowlisted Python website release; never packages repo/build secrets."""
import argparse,json,shutil,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def package(destination,include_audio=False):
    destination=destination.resolve()
    if destination.exists():raise ValueError('Choose a new release directory')
    destination.mkdir(parents=True)
    def copy(source,target=None):
        out=destination/(target or source);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/source,out)
    for file in ['h2h_model.c','h2h_model.h','h2h_motion.c','h2h_motion.h']:copy('main/h2h/'+file)
    for file in ['web_server.py','passport_card_web.py','passport_account_web.py','web_bridge.c','preview_bridge.c']:copy('tools/h2h/'+file)
    copy('tools/h2h/passport_ble.js','public/passport-ble.js')
    copy('assets/music/h2h/catalog.json')
    for file in ['index.html','gallery.html','app.js','font.ttf']:copy('preview/h2h/'+file,'public/preview/h2h/'+file)
    for outfit in range(3):
        for pose in range(5):copy(f'assets/images/h2h/ian_{outfit}_{pose}.png',f'public/assets/images/h2h/ian_{outfit}_{pose}.png')
    copy('assets/images/h2h/icons.json','public/assets/images/h2h/icons.json')
    copy('assets/music/h2h/catalog.json','public/assets/music/h2h/catalog.json')
    for track in json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())['tracks']:
        file=track['file']
        if Path(file).name!=file or not file.endswith('.ogg'):raise ValueError('Invalid song path')
        if include_audio:copy('assets/music/h2h/'+file,'public/assets/music/h2h/'+file)
    copy('assets/fonts/h2h/OFL.txt','public/licenses/OFL.txt');copy('LICENSE','public/licenses/LICENSE.txt')
    index=destination/'public/preview/h2h/index.html';s=index.read_text()
    s=s.replace('IAN EDITION · OFFLINE COMPANION','IAN EDITION · POCKET COMPANION')
    s=s.replace('这是电脑交互预览，运行与固件相同的 C 状态逻辑。设备音质、按键手感与续航需要真机测试。','无需注册，每个浏览器都有自己的小屋与收藏。清除浏览器数据或更换设备会进入新小屋；网站进度与实体设备相互独立。')
    s=s.replace('本地预览存档与设备存档相互独立','每个浏览器独立收藏 · 点击开启声音后播放音乐').replace('开启预览声音','开启声音')
    index.write_text(s);(destination/'public/index.html').write_text(s)
    js=destination/'public/preview/h2h/app.js';s=js.read_text().replace('开启预览声音','开启声音').replace('关闭预览声音','关闭声音').replace('预览连接中断，请确认本地预览服务正在运行。','连接暂时中断，请稍后重试。');js.write_text(s)
    (destination/'build').mkdir()
    archive=destination.with_suffix('.tar.gz')
    with tarfile.open(archive,'w:gz') as tar:
        for item in sorted(destination.rglob('*')):
            if item.is_file():tar.add(item,arcname=str(item.relative_to(destination)))
    print(archive)
    return archive
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('destination',type=Path);parser.add_argument('--include-audio',action='store_true');args=parser.parse_args();package(args.destination,args.include_audio)
