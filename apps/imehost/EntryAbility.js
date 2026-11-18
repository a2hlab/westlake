// Standalone stage-mode host. Never load ART or a second OHOS runtime here.
const UIAbility = globalThis.requireNapi('app.ability.UIAbility');
const hilog = globalThis.requireNapi('hilog');
const fs = globalThis.requireNapi('file.fs');
const processInfo = globalThis.requireNapi('process');
const TAG = 'WL_IME_HOST';
const HOST_BUNDLE = 'org.westlake.imehost';
class EntryAbility extends UIAbility {
    record(event, details) {
        const value = JSON.stringify({event: event, time: Date.now(), details: details,
            ownerPid: processInfo.pid, mainWindowId: this.mainWindowId || 0});
        hilog.info(0, TAG, '%{public}s', value);
        try {
            const path = this.context.filesDir + '/window-state.json';
            const temp = path + '.' + processInfo.pid + '.tmp';
            const file = fs.openSync(temp,
                fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
            try { fs.writeSync(file.fd, value); } finally { fs.closeSync(file); }
            fs.renameSync(temp, path);
        } catch (error) {
            hilog.error(0, TAG, 'record failed %{public}s', String(error));
        }
    }
    // Development-only bridge for standalone APK processes. Only this HAP calls
    // context.startAbility, so OH validates its real ability token and foreground
    // state. The mailbox shares the host UID's trust boundary; it is not per-APK
    // isolation. It carries window routing only, never authentication material.
    configureWindowBridge(want) {
        const parameter = HOST_BUNDLE.startsWith('org.westlake.apk.') ? 'westlake.ownAppSettings' : 'westlake.remoteHostWindows';
        if (want && want.parameters && want.parameters[parameter] === true) {
            this.windowBridgeEnabled = true;
        }
        if (this.windowBridgeEnabled && !this.windowBridgeTimer) {
            this.windowBridgeTimer = setInterval(() => this.pollWindowBridge(), 50);
        }
    }
    windowReply(directory, id, accepted, reason) {
        const temporary = directory + '/reply-' + id + '.tmp';
        const output = directory + '/reply-' + id + '.json';
        const file = fs.openSync(temporary, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
        try { fs.writeSync(file.fd, JSON.stringify({id: id, accepted: accepted, reason: reason})); }
        finally { fs.closeSync(file); }
        fs.renameSync(temporary, output);
        setTimeout(() => { try { fs.unlinkSync(output); } catch (error) {} }, 10000);
        hilog.info(0, TAG, 'window handoff %{public}s', reason);
    }
    pollWindowBridge() {
        if (!this.windowBridgeEnabled || this.windowBridgeBusy) return;
        const directory = this.context.filesDir + '/westlake-host-window';
        // These separate development HAPs keep this experiment away from hosts
        // already running unrelated APKs. General routing needs installer policy.
        const peer = HOST_BUNDLE === 'org.westlake.authclienthost' ? 'org.westlake.authservicehost' :
            HOST_BUNDLE === 'org.westlake.authservicehost' ? 'org.westlake.authclienthost' : undefined;
        const ownSettings = HOST_BUNDLE.startsWith('org.westlake.apk.') &&
            this.context.abilityInfo.bundleName === HOST_BUNDLE;
        if (!peer && !ownSettings) return;
        try {
            const names = fs.listFileSync(directory);
            for (const name of names.slice(0, 32)) {
                const match = /^request-([a-f0-9-]{36})\.json$/.exec(name);
                if (!match) continue;
                const id = match[1];
                const path = directory + '/' + name;
                let body;
                try {
                    if (fs.statSync(path).size > 1024) throw new Error('oversized');
                    body = JSON.parse(fs.readTextSync(path));
                } catch (error) {
                    fs.unlinkSync(path); this.windowReply(directory, id, false, 'invalid'); continue;
                }
                fs.unlinkSync(path);
                if (!this.windowHandledIds) this.windowHandledIds = new Set();
                if (this.windowHandledIds.has(id)) continue;
                this.windowHandledIds.add(id);
                if (this.windowHandledIds.size > 64) this.windowHandledIds.delete(this.windowHandledIds.values().next().value);
                const age = Date.now() - body.created;
                if (!body || body.id !== id || body.target !== (ownSettings ? 'app-settings' : peer) || typeof body.created !== 'number' ||
                    !Number.isFinite(age) || age < -1000 || age > 1500) {
                    this.windowReply(directory, id, false, 'invalid'); continue;
                }
                if (!this.windowForeground) {
                    this.windowReply(directory, id, false, 'background'); continue;
                }
                this.windowBridgeBusy = true;
                const want = ownSettings ? {
                    bundleName: 'com.ohos.settings', abilityName: 'com.ohos.settings.MainAbility',
                    uri: 'application_info_entry', parameters: {pushParams: HOST_BUNDLE}
                } : {bundleName: peer, abilityName: 'EntryAbility', parameters: {'westlake.remoteHostWindows': true}};
                this.context.startAbility(want).then(() => {
                    this.windowReply(directory, id, true, 'started');
                }).catch(error => {
                    this.windowReply(directory, id, false, 'oh-denied');
                }).finally(() => { this.windowBridgeBusy = false; });
                break;
            }
        } catch (error) { /* The launcher creates the private mailbox before opting in. */ }
    }
    onCreate(want) { this.record('created', {}); this.configureWindowBridge(want); }
    onNewWant(want) { this.configureWindowBridge(want); }
    onWindowStageCreate(stage) {
        this.stage = stage;
        this.mainWindow = undefined;
        this.mainWindowId = 0;
        this.record('window-creating', {});
        stage.on('windowStageEvent', event => {
            if (this.stage !== stage) return;
            const properties = stage.getMainWindowSync().getWindowProperties();
            this.mainWindowId = properties.id;
            this.record('stage-event', {event: event, window: properties});
        });
        stage.loadContent('pages/Index', error => {
            if (this.stage !== stage) return;
            if (error && error.code) { this.record('load-error', error); return; }
            this.mainWindow = stage.getMainWindowSync();
            this.mainWindow.setWindowBackgroundColor('#ff000000');
            const properties = this.mainWindow.getWindowProperties();
            this.mainWindowId = properties.id;
            this.record('window-ready', properties);
        });
    }
    onForeground() { this.windowForeground = true; this.record('foreground', {}); }
    onBackground() { this.windowForeground = false; this.record('background', {}); }
    onWindowStageDestroy() {
        this.stage = undefined;
        this.mainWindow = undefined;
        this.mainWindowId = 0;
        this.record('window-destroyed', {});
    }
    onDestroy() {
        if (this.windowBridgeTimer) clearInterval(this.windowBridgeTimer);
        this.windowForeground = false;
        this.stage = undefined;
        this.mainWindowId = 0;
        this.record('destroyed', {});
    }
}
globalThis.exports.default = EntryAbility;
