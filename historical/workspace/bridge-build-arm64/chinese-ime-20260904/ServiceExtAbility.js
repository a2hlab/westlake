// Offline OHOS Pinyin front end; decoder/dictionary are pinned upstream inputs.
// No network, clipboard, or global input-monitor permission is requested.
const Extension = globalThis.requireNapi('InputMethodExtensionAbility');
const engine = globalThis.requireNapi('inputMethodEngine');
const display = globalThis.requireNapi('display');
const fs = globalThis.requireNapi('file.fs');
const hilog = globalThis.requireNapi('hilog');
const decoder = globalThis.requireNapi('westlake_pinyin', true);
const log = text => hilog.info(0, 'WL_PINYIN', '%{public}s', text);
class Controller {
    constructor(context) {
        this.context = context; this.ability = engine.getInputMethodAbility();
        this.serial = Promise.resolve(); this.generation = 0; this.raw = '';
        this.candidates = []; this.preview = false; this.offset = 0; this.chinese = true;
        this.shift = false; this.numbers = false; this.status = 'Loading dictionary';
        AppStorage.setOrCreate('pinyinController', this); this.publish();
    }
    publish() {
        AppStorage.setOrCreate('pinyinState', JSON.stringify({raw: this.raw,
            candidates: this.candidates, offset: this.offset, chinese: this.chinese,
            shift: this.shift, numbers: this.numbers, status: this.status,
            enter: this.attribute ? this.attribute.enterKeyType : 2}));
    }
    async start() {
        this.ability.on('inputStart', (keyboard, client) => {
            this.generation++; this.keyboard = keyboard; this.client = client;
            this.raw = ''; this.candidates = []; this.preview = false; this.offset = 0;
            this.allowPreview = false; this.attribute = undefined;
            if (this.ready) decoder.reset();
            const generation = this.generation;
            this.editorReady = client.getEditorAttribute().then(attribute => {
                if (generation !== this.generation) return;
                this.attribute = attribute;
                this.allowPreview = attribute.isTextPreviewSupported === true;
                this.status = '离线拼音'; this.publish();
                log('inputStart preview=' + this.allowPreview + ' action=' + attribute.enterKeyType);
            }).catch(error => { if (generation === this.generation) this.error(error); });
            this.publish();
        });
        this.ability.on('inputStop', () => {
            this.generation++; this.client = undefined; this.keyboard = undefined;
            this.raw = ''; this.candidates = []; this.preview = false;
            if (this.ready) decoder.reset(); this.publish();
            log('inputStop');
        });
        const path = this.context.filesDir + '/dict_pinyin.dat';
        const bytes = await this.context.resourceManager.getRawFileContent('dict_pinyin.dat');
        const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
        try { fs.writeSync(file.fd, bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength)); }
        finally { fs.closeSync(file); }
        decoder.open(path, this.context.filesDir + '/user.dict');
        this.ready = true; this.status = '离线拼音'; this.publish(); log('decoder ready');
        const screen = display.getDefaultDisplaySync();
        this.panel = await this.ability.createPanel(this.context, {
            type: engine.PanelType.SOFT_KEYBOARD, flag: engine.PanelFlag.FLG_FIXED});
        await this.panel.resize(screen.width, Math.round(screen.height * 0.43));
        await this.panel.setUiContent('pages/Keyboard');
        log('panel ready');
    }
    error(error) {
        if (error.staleInput) return;
        this.status = '输入错误 ' + (error.code || error.message || 'unknown');
        this.publish(); log('error code=' + (error.code || '') + ' message=' + (error.message || error));
    }
    enqueue(fn) {
        const generation = this.generation;
        this.serial = this.serial.then(async () => {
            await this.editorReady;
            if (!this.client || generation !== this.generation || !this.ready) return;
            this.activeGeneration = generation;
            await fn(generation);
        }).catch(error => this.error(error));
    }
    async input(method, ...args) {
        const generation = this.activeGeneration;
        if (!this.client || generation !== this.generation) throw {staleInput: true};
        const result = await this.client[method](...args);
        if (generation !== this.generation) throw {staleInput: true};
        return result;
    }
    async setPreview(text) {
        if (this.allowPreview) {
            await this.input('setPreviewText', text, {start: -1, end: -1});
            this.preview = true;
        }
    }
    async update(result) {
        this.candidates = result.candidates; this.offset = 0;
        const fixed = result.fixed ? (result.candidates[0] || '').slice(0, result.fixed) : '';
        await this.setPreview(fixed + this.raw.slice(result.consumed || 0));
        this.publish();
    }
    async commit(text) {
        if (this.preview) {
            await this.input('setPreviewText', text, {start: -1, end: -1});
            await this.input('finishTextPreview');
        } else {
            await this.input('insertText', text);
        }
        log('commit chars=' + text.length);
        this.raw = ''; this.candidates = []; this.preview = false; this.offset = 0;
        decoder.reset(); this.publish();
    }
    async chooseNow(index) {
        if (index < 0 || index >= this.candidates.length) return;
        const result = decoder.choose(index);
        if (result.done) await this.commit(result.candidates[0]);
        else await this.update(result);
    }
    choose(index) { this.enqueue(() => this.chooseNow(index)); }
    key(value) {
        this.enqueue(async () => {
            if (value === 'hide') {
                if (this.raw) await this.commit(this.raw);
                await this.keyboard.hide(); return;
            }
            if (value === 'cancel') {
                if (this.preview) {
                    await this.input('setPreviewText', '', {start: -1, end: -1});
                    await this.input('finishTextPreview');
                }
                this.raw = ''; this.candidates = []; this.preview = false; decoder.reset(); this.publish(); return;
            }
            if (value === 'more') { this.offset = this.offset + 5 < this.candidates.length ? this.offset + 5 : 0; this.publish(); return; }
            if (value === 'mode' || value === 'numbers') {
                if (this.raw) await this.commit(this.raw);
                if (value === 'mode') this.chinese = !this.chinese;
                else this.numbers = !this.numbers;
                this.publish(); return;
            }
            if (value === 'shift') { this.shift = !this.shift; this.publish(); return; }
            if (value === 'backspace') {
                if (this.raw) {
                    this.raw = this.raw.slice(0, -1);
                    decoder.reset(); await this.update(decoder.search(this.raw));
                    if (!this.raw && this.preview) { await this.input('finishTextPreview'); this.preview = false; }
                } else await this.input('deleteForward', 1);
                return;
            }
            if (value === 'space' || value === 'enter') {
                if (this.raw) {
                    if (this.candidates.length) await this.chooseNow(0);
                    else await this.commit(this.raw);
                    // Enter confirms the composition first; next Enter invokes the editor action.
                } else if (value === 'enter') {
                    await this.input('sendKeyFunction', this.attribute ? this.attribute.enterKeyType : 2);
                    log('editor action sent');
                } else await this.input('insertText', ' ');
                return;
            }
            if (this.chinese && !this.numbers && /^[a-z']$/.test(value)) {
                if (this.raw.length >= 64) return;
                this.raw += value; await this.update(decoder.search(this.raw));
            } else {
                if (this.raw) await this.commit(this.raw);
                await this.input('insertText', this.chinese ? ({',':'，','.':'。'}[value] || value) : value);
            }
        });
    }
    async destroy() {
        this.generation++; this.client = undefined;
        this.ability.off('inputStart'); this.ability.off('inputStop');
        if (this.panel) await this.ability.destroyPanel(this.panel);
    }
}
class ServiceExtAbility extends Extension {
    onCreate() {
        this.controller = new Controller(this.context);
        this.controller.start().catch(error => this.controller.error(error));
    }
    onDestroy() { if (this.controller) this.controller.destroy().catch(error => log(String(error))); }
}
globalThis.exports.default = ServiceExtAbility;
