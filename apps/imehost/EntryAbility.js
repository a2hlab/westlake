// Standalone stage-mode host. Never load ART or a second OHOS runtime here.
const UIAbility = globalThis.requireNapi('app.ability.UIAbility');
const hilog = globalThis.requireNapi('hilog');
const fs = globalThis.requireNapi('file.fs');
const processInfo = globalThis.requireNapi('process');
const TAG = 'WL_IME_HOST';
const HOST_BUNDLE = 'org.westlake.imehost';
const CLIENT_HOST = 'org.westlake.authclienthost';
const SERVICE_HOST = 'org.westlake.authservicehost';
const APK_HOST = /^org\.westlake\.apk\.[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+$/;
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
        const params = (want && want.parameters) || {};
        const dedicated = APK_HOST.test(HOST_BUNDLE) && this.context.abilityInfo.bundleName === HOST_BUNDLE;
        if (dedicated && params['westlake.ownAppSettings'] === true) this.ownSettingsEnabled = true;
        if ((dedicated || HOST_BUNDLE === CLIENT_HOST || HOST_BUNDLE === SERVICE_HOST) &&
                params['westlake.remoteHostWindows'] === true) this.remoteWindowsEnabled = true;
        if (HOST_BUNDLE === SERVICE_HOST) {
            // AbilityManager supplies these reserved fields from the calling ability token.
            const caller = params['ohos.aafwk.param.callerBundleName'];
            const entry = params['ohos.aafwk.param.callerAbilityName'] === 'EntryAbility';
            if (params['westlake.fromApkWindow'] === true) {
                this.apkVisit = {caller: entry && typeof caller === 'string' && APK_HOST.test(caller) ? caller : null};
            } else if (entry && caller === CLIENT_HOST) {
                this.apkVisit = undefined;
            } else if (this.apkVisit) {
                this.apkVisit.caller = null;
            }
        }
        if ((this.ownSettingsEnabled || this.remoteWindowsEnabled) && !this.windowBridgeTimer) {
            this.windowBridgeTimer = setInterval(() => this.pollWindowBridge(), 50);
        }
    }
    windowWant(target) {
        if (target === 'app-settings' && this.ownSettingsEnabled) {
            return {bundleName: 'com.ohos.settings', abilityName: 'com.ohos.settings.MainAbility',
                uri: 'application_info_entry', parameters: {pushParams: HOST_BUNDLE}};
        }
        if (!this.remoteWindowsEnabled) return;
        if (target === SERVICE_HOST && (HOST_BUNDLE === CLIENT_HOST || APK_HOST.test(HOST_BUNDLE))) {
            const parameters = {'westlake.remoteHostWindows': true};
            if (HOST_BUNDLE !== CLIENT_HOST) parameters['westlake.fromApkWindow'] = true;
            return {bundleName: SERVICE_HOST, abilityName: 'EntryAbility', parameters};
        }
        if (HOST_BUNDLE !== SERVICE_HOST) return;
        if (this.apkVisit) {
            if (this.apkVisit.caller && target === this.apkVisit.caller) {
                return {bundleName: this.apkVisit.caller, abilityName: 'EntryAbility',
                    parameters: {'westlake.remoteHostWindows': true, 'westlake.ownAppSettings': true}};
            }
        } else if (target === CLIENT_HOST) {
            return {bundleName: CLIENT_HOST, abilityName: 'EntryAbility', parameters: {'westlake.remoteHostWindows': true}};
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
        if (!this.windowBridgeTimer || this.windowBridgeBusy) return;
        const directory = this.context.filesDir + '/westlake-host-window';
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
                const age = body ? Date.now() - body.created : NaN;
                const want = body && this.windowWant(body.target);
                if (!want || body.id !== id || typeof body.created !== 'number' ||
                    !Number.isFinite(age) || age < -1000 || age > 1500) {
                    this.windowReply(directory, id, false, 'invalid'); continue;
                }
                if (!this.windowForeground) {
                    this.windowReply(directory, id, false, 'background'); continue;
                }
                this.windowBridgeBusy = true;
                const visit = this.apkVisit;
                Promise.resolve().then(() => this.context.startAbility(want)).then(() => {
                    if (visit && visit === this.apkVisit) visit.caller = null;
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
    onBackground() {
        this.windowForeground = false;
        if (this.apkVisit) this.apkVisit.caller = null;
        this.record('background', {});
    }
    onWindowStageDestroy() {
        this.stage = undefined;
        this.mainWindow = undefined;
        this.mainWindowId = 0;
        this.record('window-destroyed', {});
    }
    onDestroy() {
        if (this.windowBridgeTimer) clearInterval(this.windowBridgeTimer);
        this.windowBridgeTimer = undefined;
        this.apkVisit = undefined;
        this.windowForeground = false;
        this.stage = undefined;
        this.mainWindowId = 0;
        this.record('destroyed', {});
    }
}
globalThis.exports.default = EntryAbility;
