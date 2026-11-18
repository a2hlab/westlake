const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../../apps/imehost/EntryAbility.js'), 'utf8')
  .replace("const HOST_BUNDLE = 'org.westlake.imehost'", "const HOST_BUNDLE = 'org.westlake.authclienthost'");
const files = new Map(), handles = new Map(); let fd = 0, calls = 0, failOH = false;
const nativeFS = {
  OpenMode: {CREATE: 1, WRITE_ONLY: 2, TRUNC: 4},
  openSync: p => { handles.set(++fd, p); return {fd}; },
  writeSync: (n, s) => files.set(handles.get(n), s), closeSync: n => handles.delete(n),
  readTextSync: p => files.get(p), statSync: p => ({size: files.get(p).length}),
  renameSync: (a, b) => { files.set(b, files.get(a)); files.delete(a); }, unlinkSync: p => files.delete(p),
  listFileSync: dir => [...files.keys()].filter(p => p.startsWith(dir + '/')).map(p => p.slice(dir.length + 1))
};
let nextTimer = 0;
const clearedTimers = [];
const context = {exports: {}, requireNapi: name => name === 'file.fs' ? nativeFS :
  name === 'process' ? {pid: 71} : name === 'hilog' ? {info() {}, error() {}} : class {},
  setInterval: () => nextTimer++, clearInterval(id) { clearedTimers.push(id); }, setTimeout() {}, Date};
vm.runInNewContext(source, context);
const host = new context.exports.default();
host.context = {filesDir: '/private', startAbility: async want => {
  calls++; assert.equal(want.bundleName, 'org.westlake.authservicehost');
  if (failOH) throw new Error('OH denied');
}};
const root = '/private/westlake-host-window'; let serial = 0;
function request(overrides = {}, content) {
  const id = '00000000-0000-0000-0000-' + String(++serial).padStart(12, '0');
  const body = {id, target: 'org.westlake.authservicehost', created: Date.now(), ...overrides};
  files.set(`${root}/request-${id}.json`, content || JSON.stringify(body)); return id;
}
function reply(id) { return JSON.parse(files.get(`${root}/reply-${id}.json`)); }
const flush = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  host.onCreate({}); host.onForeground();
  const windowState = () => JSON.parse(files.get('/private/window-state.json'));
  const stage = id => ({
    on(event, callback) { this.stageEvent = callback; },
    loadContent(page, callback) { this.ready = callback; },
    getMainWindowSync() { return {getWindowProperties: () => ({id}), setWindowBackgroundColor() {}}; }
  });
  assert.equal(windowState().mainWindowId, 0);
  const oldStage = stage(16), newStage = stage(31);
  host.onWindowStageCreate(oldStage); oldStage.ready();
  host.onBackground(); host.onForeground();
  assert.equal(windowState().ownerPid, 71);
  assert.equal(windowState().mainWindowId, 16);
  host.onWindowStageDestroy(); assert.equal(windowState().mainWindowId, 0);
  host.onWindowStageCreate(newStage); newStage.stageEvent(1);
  oldStage.ready(); oldStage.stageEvent(1);
  assert.equal(windowState().mainWindowId, 31);
  const disabled = request(); host.pollWindowBridge(); await flush(); assert.equal(calls, 0);
  files.delete(`${root}/request-${disabled}.json`);
  host.configureWindowBridge({parameters: {'westlake.remoteHostWindows': true}});
  assert.equal(host.windowBridgeTimer, 0);
  host.onNewWant({parameters: {'westlake.remoteHostWindows': true}});
  assert.equal(nextTimer, 1, 'a repeated launch must keep the first interval');
  const accepted = request(); host.pollWindowBridge(); await flush(); assert.equal(calls, 1); assert.equal(reply(accepted).accepted, true);
  files.set(`${root}/request-${accepted}.json`, JSON.stringify({id: accepted, target: 'org.westlake.authservicehost', created: Date.now()}));
  host.pollWindowBridge(); await flush(); assert.equal(calls, 1, 'duplicate request must not launch again');
  host.onBackground(); const background = request(); host.pollWindowBridge(); await flush();
  assert.equal(reply(background).reason, 'background'); assert.equal(calls, 1);
  host.onForeground();
  for (const options of [{target: 'org.example.unconfigured'}, {created: Date.now() - 3000}, {created: Date.now() + 5000}, {id: 'different'}, {created: 'invalid'}]) {
    const id = request(options); host.pollWindowBridge(); await flush(); assert.equal(reply(id).accepted, false); assert.equal(calls, 1);
  }
  const oversized = request({}, 'x'.repeat(1025)); host.pollWindowBridge(); await flush(); assert.equal(reply(oversized).accepted, false);
  failOH = true; const denied = request(); host.pollWindowBridge(); await flush();
  assert.equal(reply(denied).reason, 'oh-denied'); assert.equal(reply(denied).accepted, false); assert.equal(calls, 2);
  host.onDestroy(); assert.equal(host.windowForeground, false);
  assert.deepEqual(clearedTimers, [0]);
  const apkHost = 'org.westlake.apk.org.example.maps';
  const dedicatedContext = {...context, exports: {}};
  vm.runInNewContext(source.replace("const HOST_BUNDLE = 'org.westlake.authclienthost'",
    "const HOST_BUNDLE = '" + apkHost + "'"), dedicatedContext);
  const dedicated = new dedicatedContext.exports.default();
  const opened = [];
  dedicated.context = {filesDir: '/private', abilityInfo: {bundleName: apkHost},
    startAbility: async want => opened.push(JSON.parse(JSON.stringify(want)))};
  dedicated.onForeground();
  dedicated.configureWindowBridge({parameters: {'westlake.ownAppSettings': true}});
  const wrongTarget = request();
  dedicated.pollWindowBridge(); await flush();
  assert.equal(reply(wrongTarget).accepted, false);
  const settings = request({target: 'app-settings', pushParams: 'org.other.app'});
  dedicated.pollWindowBridge(); await flush();
  assert.equal(reply(settings).accepted, true);
  assert.deepEqual(opened, [{bundleName: 'com.ohos.settings', abilityName: 'com.ohos.settings.MainAbility',
    uri: 'application_info_entry', parameters: {pushParams: apkHost}}]);
  dedicated.onBackground();
  const backgroundSettings = request({target: 'app-settings'});
  dedicated.pollWindowBridge(); await flush();
  assert.equal(reply(backgroundSettings).accepted, false);
  assert.equal(opened.length, 1);
  dedicated.onForeground();
  dedicated.onNewWant({parameters: {'westlake.remoteHostWindows': true}});
  const launch = request();
  dedicated.pollWindowBridge(); await flush(); assert.equal(reply(launch).accepted, true);
  assert.deepEqual(opened[1], {bundleName: 'org.westlake.authservicehost', abilityName: 'EntryAbility',
    parameters: {'westlake.remoteHostWindows': true, 'westlake.fromApkWindow': true}});
  dedicated.onDestroy();

  files.clear();
  const serviceName = 'org.westlake.authservicehost', shared = 'org.westlake.authclienthost';
  const second = 'org.westlake.apk.org.example.reader';
  const serviceContext = {...context, exports: {}};
  vm.runInNewContext(source.replace("const HOST_BUNDLE = 'org.westlake.authclienthost'",
    "const HOST_BUNDLE = '" + serviceName + "'"), serviceContext);
  const service = new serviceContext.exports.default();
  const destinations = [];
  service.context = {filesDir: '/private', abilityInfo: {bundleName: serviceName},
    startAbility: async want => destinations.push(want.bundleName)};
  const visit = (caller, entry = 'EntryAbility') => service.onNewWant({parameters: {
    ...opened[1].parameters, 'ohos.aafwk.param.callerBundleName': caller,
    'ohos.aafwk.param.callerAbilityName': entry}});
  async function restore(target) {
    const id = request({target});
    service.pollWindowBridge(); await flush();
    const accepted = reply(id).accepted;
    files.delete(`${root}/reply-${id}.json`);
    return accepted;
  }
  service.onCreate({parameters: {'westlake.remoteHostWindows': true}}); service.onForeground();
  assert.equal(await restore(apkHost), false);
  visit(apkHost); visit(second);
  assert.equal(await restore(apkHost), false);
  assert.equal(await restore(shared), false);
  assert.equal(await restore(second), true);
  assert.equal(await restore(second), false);
  visit(apkHost); assert.equal(await restore(apkHost), true);
  visit(apkHost); service.onBackground(); service.onForeground();
  assert.equal(await restore(apkHost), false);
  visit(undefined); assert.equal(await restore(apkHost), false);
  visit(apkHost, 'OtherAbility'); assert.equal(await restore(apkHost), false);
  visit(apkHost); service.onNewWant({}); assert.equal(await restore(apkHost), false);
  visit(apkHost);
  const start = service.context.startAbility;
  service.context.startAbility = () => { throw new Error('OS rejected the native launch'); };
  assert.equal(await restore(apkHost), false);
  service.context.startAbility = start;
  assert.equal(await restore(apkHost), true);
  let completeStart;
  visit(apkHost);
  service.context.startAbility = () => new Promise(resolve => { completeStart = resolve; });
  const inFlight = request({target: apkHost});
  service.pollWindowBridge(); await flush();
  visit(second); completeStart(); await flush();
  assert.equal(reply(inFlight).accepted, true);
  service.context.startAbility = start;
  assert.equal(await restore(second), true);
  service.onNewWant({parameters: {'westlake.remoteHostWindows': true,
    'ohos.aafwk.param.callerBundleName': shared, 'ohos.aafwk.param.callerAbilityName': 'EntryAbility'}});
  assert.equal(await restore(shared), true);
  assert.deepEqual(destinations, [second, apkHost, apkHost, second, shared]);
  service.onDestroy();
  console.log('HOST_WINDOW_POLICY_PASS foreground, dedicated and shared callers, overlap, expiry, replay and OH refusal');
})().catch(error => { console.error(error); process.exitCode = 1; });
