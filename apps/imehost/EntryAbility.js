// Standalone stage-mode host. Never load ART or a second OHOS runtime here.
const UIAbility = globalThis.requireNapi('app.ability.UIAbility');
const hilog = globalThis.requireNapi('hilog');
const fs = globalThis.requireNapi('file.fs');
const TAG = 'WL_IME_HOST';
class EntryAbility extends UIAbility {
    record(event, details) {
        const value = JSON.stringify({event: event, time: Date.now(), details: details});
        hilog.info(0, TAG, '%{public}s', value);
        try {
            const file = fs.openSync(this.context.filesDir + '/window-state.json',
                fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
            try { fs.writeSync(file.fd, value); } finally { fs.closeSync(file); }
        } catch (error) {
            hilog.error(0, TAG, 'record failed %{public}s', String(error));
        }
    }
    onCreate() { this.record('created', {}); }
    onWindowStageCreate(stage) {
        this.stage = stage;
        stage.on('windowStageEvent', event => this.record('stage-event', {
            event: event, window: stage.getMainWindowSync().getWindowProperties()
        }));
        stage.loadContent('pages/Index', error => {
            if (error && error.code) { this.record('load-error', error); return; }
            this.mainWindow = stage.getMainWindowSync();
            this.mainWindow.setWindowBackgroundColor('#fff4f6f8');
            this.record('window-ready', this.mainWindow.getWindowProperties());
        });
    }
    onForeground() { this.record('foreground', {}); }
    onBackground() { this.record('background', {}); }
    onWindowStageDestroy() { this.record('window-destroyed', {}); this.stage = undefined; }
    onDestroy() { this.record('destroyed', {}); }
}
globalThis.exports.default = EntryAbility;
