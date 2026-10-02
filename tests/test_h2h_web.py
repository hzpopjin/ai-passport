"""WSGI session isolation, validation and durable saves using the actual C model."""
import importlib.util,io,json,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('h2h_web',ROOT/'tools/h2h/web_server.py');web=importlib.util.module_from_spec(spec);spec.loader.exec_module(web)
class WebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.library=Path(cls.tmp.name)/'model.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'main/h2h'),str(ROOT/'main/h2h/h2h_model.c'),str(ROOT/'main/h2h/h2h_motion.c'),str(ROOT/'tools/h2h/web_bridge.c'),'-o',str(cls.library)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def setUp(self):
        self.data=tempfile.TemporaryDirectory();self.app=web.WebApp(self.library,self.data.name,'https://example.test')
    def tearDown(self):self.app.db.close();self.data.cleanup()
    def request(self,path='/api/state',cookie='',data=None,origin='https://example.test',method=None):
        raw=json.dumps(data).encode() if data is not None else b''
        env={'PATH_INFO':path,'REQUEST_METHOD':method or ('POST' if data is not None else 'GET'),'HTTP_COOKIE':cookie,'HTTP_ORIGIN':origin,'CONTENT_TYPE':'application/json','CONTENT_LENGTH':str(len(raw)),'wsgi.input':io.BytesIO(raw)}
        result={}
        def start(status,headers):result.update(code=int(status[:3]),headers=dict(headers))
        body=b''.join(self.app(env,start));return result['code'],json.loads(body),result['headers'].get('Set-Cookie',cookie).split(';')[0]
    def key(self,cookie,key,event=0):return self.request('/api/input',cookie,{'key':key,'event':event})
    def click(self,cookie):self.key(cookie,2);return self.key(cookie,2,1)
    def test_independent_visitors(self):
        _,a,ca=self.request();_,b,cb=self.request();self.assertNotEqual(ca,cb)
        self.key(ca,1);self.assertEqual(self.request(cookie=ca)[1]['selection'],1)
        self.assertEqual(self.request(cookie=cb)[1]['selection'],0)
        self.assertEqual(self.request(cookie='h2h_visitor=../private')[1]['page'],0)
    def test_pause_and_reversed_seek(self):
        _,_,cookie=self.request();self.key(cookie,1);self.click(cookie);self.click(cookie)
        self.click(cookie);state=self.request(cookie=cookie)[1];self.assertFalse(state['play'])
        self.request('/api/audio',cookie,{'generation':state['generation'],'position_ms':15000,'duration_ms':163000})
        self.key(cookie,0);state=self.key(cookie,0,2)[1];self.assertEqual(state['position_ms'],25000);self.assertFalse(state['play'])
        self.key(cookie,0,1);self.assertEqual(self.request(cookie=cookie)[1]['volume'],35)
        self.key(cookie,1);self.key(cookie,1,2);self.assertEqual(self.request(cookie=cookie)[1]['position_ms'],15000)
    def test_full_chorus_switch_and_song_selection(self):
        _,_,cookie=self.request();self.key(cookie,1);self.click(cookie)
        self.key(cookie,1);self.key(cookie,1);state=self.click(cookie)
        self.assertTrue(state[1]['chorus']);self.assertEqual(state[1]['track'],7)
        self.key(cookie,0);state=self.click(cookie);self.assertEqual(state[1]['page'],4)
        self.key(cookie,1);self.key(cookie,1);state=self.click(cookie)
        self.assertEqual(state[1]['track'],9)
        self.assertEqual(self.app.catalog[state[1]['track']]['title'],'ICONIC HEART')
        for _ in range(3):
            self.key(cookie,1);state=self.key(cookie,1,1)[1]
        self.assertEqual(state['track'],10)
        for _ in range(3):
            self.key(cookie,0);state=self.key(cookie,0,1)[1]
        self.assertEqual(state['track'],9)
    def test_restart_and_corrupt_save(self):
        _,_,cookie=self.request();self.key(cookie,0);self.click(cookie);self.click(cookie)
        sid=cookie.split('=')[1];self.app.sessions[sid]['saved_at']-=4;self.request(cookie=cookie)
        self.app.db.close();self.app=web.WebApp(self.library,self.data.name,'https://example.test')
        self.assertEqual(self.request(cookie=cookie)[1]['volume'],40)
        self.app.db.execute('UPDATE saves SET data=? WHERE id=?',(b'invalid',sid));self.app.db.commit();self.app.sessions.clear()
        self.assertEqual(self.request(cookie=cookie)[1]['volume'],35)
    def test_invalid_requests_and_private_paths(self):
        self.assertEqual(self.request('/api/input',data={'key':0,'event':0},origin='https://elsewhere.test')[0],403)
        for data in ({'key':3,'event':0},{'key':True,'event':0},{'key':0,'event':-1},[]):
            self.assertEqual(self.request('/api/input',data=data)[0],400)
        self.assertEqual(self.request('/api/audio',data={'generation':-1})[0],400)
        self.assertEqual(self.request('/api/audio',data={'generation':0,'ms':3000})[0],400)
        self.assertEqual(self.request('/api/input')[0],405)
        for path in ('/build/progress.sqlite3','/.git/config','/tools/h2h/web_server.py'):
            self.assertEqual(self.request(path)[0],404)
if __name__=='__main__':unittest.main()
