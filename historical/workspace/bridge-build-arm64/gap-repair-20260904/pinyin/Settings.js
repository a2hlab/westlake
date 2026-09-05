class Settings extends ViewPU {
    constructor(parent, params, storage, id = -1, paramsLambda, extraInfo) {
        super(parent, storage, id, extraInfo);
        if (this.finalizeConstruction) this.finalizeConstruction();
    }
    setInitiallyProvidedValue() {} updateStateVars() {} purgeVariableDependenciesOnElmtId() {}
    aboutToBeDeleted() { SubscriberManager.Get().delete(this.id__()); this.aboutToBeDeletedInternal(); }
    initialRender() {
        this.observeComponentCreation2(() => { Column.create({space:20}); Column.width('100%'); Column.height('100%'); Column.padding(25); }, Column);
        this.observeComponentCreation2(() => { Text.create('Westlake Pinyin · 中文输入法'); Text.fontSize(25); }, Text); Text.pop();
        this.observeComponentCreation2(() => { Text.create('离线拼音，点击系统选择器启用。Kika 保留，可随时切回。'); Text.fontSize(18); }, Text); Text.pop();
        this.observeComponentCreation2(() => {
            Button.createWithLabel('选择输入法 / Choose keyboard'); Button.height(55);
            Button.onClick(() => globalThis.requireNapi('inputMethod').getInputMethodSetting().displayOptionalInputMethod()
                .catch(error => console.error('WL_PINYIN selector ' + JSON.stringify(error))));
        }, Button); Button.pop();
        this.observeComponentCreation2(() => {
            TextInput.create({placeholder:'原生 OHOS 中文输入测试'}); TextInput.width('100%'); TextInput.height(65); TextInput.fontSize(24);
            TextInput.onChange(value => globalThis.requireNapi('hilog').info(0,'WL_PINYIN_CONTROL','native text=%{public}s',value));
        }, TextInput); Column.pop();
    }
    rerender() { this.updateDirtyElements(); }
    static getEntryName() { return 'Settings'; }
}
registerNamedRoute(() => new Settings(undefined, {}), '', {
    bundleName:'org.westlake.pinyin', moduleName:'entry', pagePath:'pages/Settings',
    pageFullPath:'pages/Settings', integratedHsp:'false', moduleType:'followWithHap'});
