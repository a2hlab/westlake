// ArkUI partial-update keyboard. Labels are driven by the controller's state.
class Keyboard extends ViewPU {
    constructor(parent, params, storage, id = -1, paramsLambda, extraInfo) {
        super(parent, storage, id, extraInfo);
        this.__state = this.createStorageLink('pinyinState', '{}', 'state');
        if (this.finalizeConstruction) this.finalizeConstruction();
    }
    get state() { return this.__state.get(); }
    set state(value) { this.__state.set(value); }
    data() { return JSON.parse(this.state); }
    controller() { return AppStorage.get('pinyinController'); }
    setInitiallyProvidedValue() {}
    updateStateVars() {}
    purgeVariableDependenciesOnElmtId(id) { this.__state.purgeDependencyOnElmtId(id); }
    aboutToBeDeleted() { this.__state.aboutToBeDeleted(); SubscriberManager.Get().delete(this.id__()); this.aboutToBeDeletedInternal(); }
    key(label, value, width, color = '#ffffff') {
        this.observeComponentCreation2(() => {
            Button.createWithLabel(typeof label === 'function' ? label() : label,
                {type: ButtonType.Normal, stateEffect: true});
            Button.width(width); Button.height(53); Button.margin({left: 2, right: 2, top: 3, bottom: 3});
            Button.fontSize(23); Button.fontColor('#182333'); Button.backgroundColor(color);
            Button.padding({left: 1, right: 1});
            Button.borderRadius(6);
            Button.onClick(() => { const c = this.controller(); if (c) c.key(typeof value === 'function' ? value() : value); });
        }, Button);
        Button.pop();
    }
    row(keys) {
        this.observeComponentCreation2(() => { Row.create(); Row.width('100%'); Row.justifyContent(FlexAlign.Center); }, Row);
        keys.forEach((letter, i) => this.key(() => {
            const s = this.data();
            if (s.numbers) return ({q:'1',w:'2',e:'3',r:'4',t:'5',y:'6',u:'7',i:'8',o:'9',p:'0',
                a:'@',s:'#',d:'$',f:'%',g:'&',h:'*',j:'-',k:'+',l:'=',z:'(',x:')',c:'!',v:'?',b:':',n:';',m:'/'})[letter];
            return !s.chinese && s.shift ? letter.toUpperCase() : letter;
        }, () => {
            const s = this.data();
            if (s.numbers) return ({q:'1',w:'2',e:'3',r:'4',t:'5',y:'6',u:'7',i:'8',o:'9',p:'0',
                a:'@',s:'#',d:'$',f:'%',g:'&',h:'*',j:'-',k:'+',l:'=',z:'(',x:')',c:'!',v:'?',b:':',n:';',m:'/'})[letter];
            return !s.chinese && s.shift ? letter.toUpperCase() : letter;
        }, '9%'));
        Row.pop();
    }
    initialRender() {
        this.observeComponentCreation2(() => {
            Column.create(); Column.width('100%'); Column.height('100%');
            Column.backgroundColor('#dce2e9'); Column.padding({left: 4, right: 4});
        }, Column);
        this.observeComponentCreation2(() => { Row.create(); Row.width('100%'); Row.height(35); }, Row);
        this.observeComponentCreation2(() => {
            const s = this.data(); Text.create(s.raw || (s.chinese ? '中文 · ' : 'English · ') + (s.status || ''));
            Text.fontSize(19); Text.fontColor('#183b6d'); Text.layoutWeight(1); Text.maxLines(1);
        }, Text); Text.pop();
        this.observeComponentCreation2(() => {
            Button.createWithLabel('取消'); Button.width(55); Button.height(30);
            Button.onClick(() => this.controller().key('cancel'));
        }, Button); Button.pop();
        this.observeComponentCreation2(() => {
            Button.createWithLabel('⌄'); Button.width(42); Button.height(30); Button.margin({left: 4});
            Button.onClick(() => this.controller().key('hide'));
        }, Button); Button.pop(); Row.pop();
        this.observeComponentCreation2(() => { Row.create(); Row.width('100%'); Row.height(48); }, Row);
        for (let i = 0; i < 5; i++) {
            this.observeComponentCreation2(() => {
                const s = this.data(); Button.createWithLabel((s.candidates || [])[s.offset + i] || '', {type: ButtonType.Normal});
                Button.layoutWeight(1); Button.height(43); Button.fontSize(23); Button.fontColor('#103561');
                Button.backgroundColor('#f5f8fc'); Button.margin(1);
                Button.padding({left: 1, right: 1});
                Button.onClick(() => { const s = this.data(); this.controller().choose(s.offset + i); });
            }, Button); Button.pop();
        }
        this.observeComponentCreation2(() => {
            Button.createWithLabel('>'); Button.width(30); Button.height(43); Button.padding(0);
            Button.onClick(() => this.controller().key('more'));
        }, Button); Button.pop(); Row.pop();
        this.row('qwertyuiop'.split('')); this.row('asdfghjkl'.split('')); this.row('zxcvbnm'.split(''));
        this.observeComponentCreation2(() => { Row.create(); Row.width('100%'); Row.justifyContent(FlexAlign.Center); }, Row);
        this.key(() => this.data().numbers ? 'ABC' : '123', 'numbers', '11%', '#c3ccd8');
        this.key(() => this.data().chinese ? '中/英' : 'EN/中', 'mode', '15%', '#c3ccd8');
        this.key("'", "'", '8%'); this.key('空格', 'space', '22%');
        this.key('，', ',', '8%'); this.key('⌫', 'backspace', '12%', '#c3ccd8');
        this.key(() => this.data().enter === 3 ? '搜索' : '确认', 'enter', '15%', '#a7d0ff');
        Row.pop();
        this.observeComponentCreation2(() => {
            Text.create('Westlake Pinyin · 离线词库'); Text.fontSize(11); Text.fontColor('#627184'); Text.margin({top: 8});
        }, Text); Text.pop(); Column.pop();
    }
    rerender() { this.updateDirtyElements(); }
    static getEntryName() { return 'Keyboard'; }
}
registerNamedRoute(() => new Keyboard(undefined, {}), '', {
    bundleName:'org.westlake.pinyin', moduleName:'entry', pagePath:'pages/Keyboard',
    pageFullPath:'pages/Keyboard', integratedHsp:'false', moduleType:'followWithHap'});
