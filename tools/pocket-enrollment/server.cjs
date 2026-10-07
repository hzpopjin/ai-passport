'use strict';
/* Server-local CLI; runtime application code and HTTP endpoints are unchanged.
 * Secrets enter only stdin and parameterized SQL; never argv, output or files. */
const fs = require('node:fs');
const crypto = require('node:crypto');
const { createRequire } = require('node:module');
const ROOT = '/www/wwwroot/apps.randomdance.cn/backend';
const PURPOSE = 'independent-enrolled-relay-key';
const COLUMNS = 'device_id,purpose,key_revision,revoked';
const SQL = Object.freeze({
  device: 'SELECT id,slug,owner_id FROM passport_devices WHERE id=? FOR UPDATE',
  key: `SELECT ${COLUMNS} FROM passport_relay_keys WHERE device_id=? FOR UPDATE`,
  ownKey: `SELECT ${COLUMNS} FROM passport_relay_keys WHERE device_id=? AND relay_key=? FOR UPDATE`,
  insertDevice: 'INSERT INTO passport_devices (id,slug,owner_id,pairing_hash,state) VALUES (?,?,NULL,NULL,?)',
  insertKey: 'INSERT INTO passport_relay_keys (device_id,relay_key,purpose,key_revision,revoked) VALUES (?,?,?,1,0)',
  logs: 'SELECT @@GLOBAL.general_log AS general_log,@@GLOBAL.slow_query_log AS slow_query_log',
  triggers: "SELECT TRIGGER_NAME FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA=DATABASE() AND EVENT_OBJECT_TABLE IN ('passport_devices','passport_relay_keys')",
});
function fail(code) { const e = new Error(code); e.safeCode = code; throw e; }
function hex(value, length) { return typeof value === 'string' && new RegExp(`^[0-9a-f]{${length}}$`).test(value); }
function flatJSON(text) {
  if (typeof text !== 'string' || Buffer.byteLength(text) > 4096) fail('INPUT_INVALID');
  let rest = text.trim(), result = Object.create(null);
  if (!rest.startsWith('{') || !rest.endsWith('}')) fail('INPUT_INVALID');
  rest = rest.slice(1,-1).trim();
  const token = /^\s*("(?:[^"\\\x00-\x1f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*")\s*:\s*("(?:[^"\\\x00-\x1f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*"|true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?)(\s*)(,|$)/;
  while (rest) {
    const m = rest.match(token); if (!m) fail('INPUT_INVALID');
    const k = JSON.parse(m[1]); if (Object.hasOwn(result,k)) fail('INPUT_DUPLICATE');
    result[k] = JSON.parse(m[2]); rest = rest.slice(m[0].length);
    if (m[4] === ',' && !rest.trim()) fail('INPUT_INVALID');
  }
  return result;
}
const PUBLIC_FIELDS = ['v','device_id','chip_mac','helper_elf_sha256','nonce','operation_id','allow_create_relay_only','slug','expected_owner_id'];
function publicInput(x) {
  if (!x || typeof x !== 'object' || Object.keys(x).sort().join(',') !== [...PUBLIC_FIELDS].sort().join(',') || x.v !== 1 ||
      !hex(x.device_id,32) || !hex(x.chip_mac,12) || !hex(x.helper_elf_sha256,64) || !hex(x.nonce,32) ||
      !hex(x.operation_id,32) || !hex(x.slug,32) || typeof x.allow_create_relay_only !== 'boolean' ||
      (x.expected_owner_id!==null && (!Number.isSafeInteger(x.expected_owner_id)||x.expected_owner_id<1))) fail('TARGET_INVALID');
  return x;
}
function context(x) { return ['pocket-enrollment-v1',x.device_id,x.chip_mac,x.helper_elf_sha256,x.nonce,x.operation_id,'written'].join('\n'); }
function input(x, expected) {
  if (!x || Object.keys(x).sort().join(',') !== [...PUBLIC_FIELDS,'key_hex','proof'].sort().join(',') || !hex(x.key_hex,64) || !hex(x.proof,64)) fail('INPUT_INVALID');
  const p = Object.fromEntries(PUBLIC_FIELDS.map(k=>[k,x[k]])); publicInput(p); publicInput(expected);
  if (PUBLIC_FIELDS.some(k=>p[k]!==expected[k])) fail('TARGET_MISMATCH');
  const key = Buffer.from(x.key_hex,'hex');
  const proof = crypto.createHmac('sha256',key).update(context(p)).digest();
  if (!crypto.timingSafeEqual(proof,Buffer.from(x.proof,'hex'))) { key.fill(0); fail('DEVICE_PROOF_INVALID'); }
  return { public: p, key };
}
async function loggingSafe(db) {
  const [rows] = await db.execute(SQL.logs);
  if (rows.length !== 1 || Number(rows[0].general_log) !== 0 || Number(rows[0].slow_query_log) !== 0) fail('QUERY_LOGGING_ENABLED');
  const [triggers] = await db.execute(SQL.triggers); if (triggers.length) fail('UNREVIEWED_DB_TRIGGER');
}
async function provision(db, supplied, expected) {
  const parsed = input(supplied, expected), p = parsed.public, key = parsed.key;
  let attemptedCommit = false, started = false;
  try {
    /* Preflight has no secret parameters. Do not change global DB settings. */
    await loggingSafe(db); await db.beginTransaction(); started = true;
    const [devices] = await db.execute(SQL.device,[p.device_id]);
    const [keys] = await db.execute(SQL.key,[p.device_id]);
    if (devices.length > 1 || (devices.length===1 && devices[0].owner_id!==p.expected_owner_id) || (!devices.length && p.expected_owner_id!==null)) fail('REGISTRY_OWNER_CHANGED_OR_UNREVIEWED');
    if (keys.length) {
      if (devices.length !== 1 || keys.length !== 1 || keys[0].purpose !== PURPOSE || Number(keys[0].key_revision)!==1 || Number(keys[0].revoked)!==0) fail('EXISTING_KEY_CONFLICT');
      /* Compare only this operation's supplied NEW key inside DB; no old key SELECT. */
      const [same] = await db.execute(SQL.ownKey,[p.device_id,key]);
      if (same.length !== 1) fail('EXISTING_KEY_CONFLICT');
      await db.rollback(); started = false;
      return { v:1,device_id:p.device_id,operation_id:p.operation_id,status:'already_registered_same_new_key',ownership_unchanged:true };
    }
    if (devices.length > 1) fail('REGISTRY_INVALID');
    let created = false;
    if (!devices.length) {
      if (!p.allow_create_relay_only) fail('DEVICE_REGISTRY_MISSING');
      /* Existing state/legacy token is never loaded. New relay-only identity
       * leaves direct HTTPS auth disabled by its existing non-string guard. */
      const state = {id:p.device_id,slug:p.slug,credentialHash:null,owner:null,epoch:0,public:false,pairing:null,save:null,desiredResource:null};
      await db.execute(SQL.insertDevice,[p.device_id,p.slug,JSON.stringify(state)]); created = true;
    }
    await db.execute(SQL.insertKey,[p.device_id,key,PURPOSE]);
    attemptedCommit = true; await db.commit(); started = false;
    return {v:1,device_id:p.device_id,operation_id:p.operation_id,status:'registered',created_relay_only_identity:created,ownership_unchanged:true};
  } catch (e) {
    if (started) { try { await db.rollback(); } catch {} }
    /* A lost commit ACK can conceal a successful commit. Never rotate on retry. */
    if (attemptedCommit) fail('SERVER_COMMIT_INDETERMINATE');
    fail(e.safeCode || 'SERVER_OPERATION_FAILED');
  } finally { key.fill(0); }
}
function privateJSON(file) {
  const fd = fs.openSync(file,fs.constants.O_RDONLY|fs.constants.O_NOFOLLOW);
  try { const s = fs.fstatSync(fd); if (!s.isFile() || s.size>4096 || (s.mode&0o077)) fail('APPROVAL_FILE_INVALID'); return flatJSON(fs.readFileSync(fd,'utf8')); }
  finally { fs.closeSync(fd); }
}
async function readInput() {
  let data = ''; for await (const b of process.stdin) { data += b.toString('utf8'); if (Buffer.byteLength(data)>4096) fail('INPUT_INVALID'); }
  return flatJSON(data);
}
async function main() {
  const args = process.argv.slice(2);
  const inspect=args.length===3 && args[0]==='--inspect' && args[1]==='--device-id' && hex(args[2],32);
  if (!inspect && (args.length!==3 || args[0]!=='--apply' || args[1]!=='--approval' || !new RegExp('^'+ROOT+'/build/pocket-enrollment/[0-9a-f]{32}\\.json$').test(args[2]))) fail('USAGE_INVALID');
  if (fs.realpathSync(ROOT)!==ROOT) fail('SERVER_TARGET_INVALID');
  const expected = inspect ? null : publicInput(privateJSON(args[2]));
  const appRequire = createRequire(ROOT+'/package.json');
  const env = {...appRequire('dotenv').parse(fs.readFileSync(ROOT+'/.env')), ...process.env};
  if (env.DB_SYNCHRONIZE!=='false' || !['localhost','127.0.0.1','::1'].includes(env.DB_HOST)) fail('DB_TARGET_INVALID');
  const db = await appRequire('mysql2/promise').createConnection({host:env.DB_HOST,port:Number(env.DB_PORT||3306),user:env.DB_USERNAME,password:env.DB_PASSWORD,database:env.DB_DATABASE});
  try {
    if (inspect) {
      await loggingSafe(db);
      const [devices]=await db.execute(SQL.device.replace(' FOR UPDATE',''),[args[2]]);
      const [keys]=await db.execute(SQL.key.replace(' FOR UPDATE',''),[args[2]]);
      if(devices.length>1||keys.length>1)fail('REGISTRY_INVALID');
      const [accounts]=await db.execute('SELECT id,username,wp_user_id FROM users WHERE username=?',['hzpopjin']);
      console.log(JSON.stringify({status:'read_only',device_id:args[2],registry_present:devices.length===1,owner_id:devices[0]?.owner_id??null,key_present:keys.length===1,key_metadata:keys[0]??null,account:accounts.length===1?{id:accounts[0].id,username:accounts[0].username,wordpress_identity:accounts[0].id===accounts[0].wp_user_id}:null,account_login_verified:false}));
    } else { const supplied=await readInput(); console.log(JSON.stringify(await provision(db,supplied,expected))); }
  }
  finally { await db.end(); }
}
if (require.main===module) main().catch(e=>{console.error(JSON.stringify({status:'failed_or_unconfirmed',error:e.safeCode||'SERVER_OPERATION_FAILED'}));process.exitCode=1;});
module.exports = {SQL,PURPOSE,flatJSON,publicInput,context,input,provision,loggingSafe};
