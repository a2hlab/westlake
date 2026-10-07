// The host's page, compiled directly with the installed SDK es2abc. Its window is the parent of every
// Android window, so it shows wherever an Android window does not cover it: it is empty and black,
// what Android shows behind an app's windows when no wallpaper is there.
class Index extends ViewPU {
    constructor(parent, params, storage, id = -1, paramsLambda, extraInfo) {
        super(parent, storage, id, extraInfo);
        if (this.finalizeConstruction) this.finalizeConstruction();
    }
    setInitiallyProvidedValue(params) {}
    updateStateVars(params) {}
    purgeVariableDependenciesOnElmtId(id) {}
    aboutToBeDeleted() { SubscriberManager.Get().delete(this.id__()); this.aboutToBeDeletedInternal(); }
    initialRender() {
        this.observeComponentCreation2(() => {
            Column.create(); Column.width('100%'); Column.height('100%'); Column.backgroundColor('#ff000000');
        }, Column);
        Column.pop();
    }
    rerender() { this.updateDirtyElements(); }
    static getEntryName() { return 'Index'; }
}
registerNamedRoute(() => new Index(undefined, {}), '', {
    bundleName: 'org.westlake.imehost', moduleName: 'entry', pagePath: 'pages/Index',
    pageFullPath: 'pages/Index', integratedHsp: 'false', moduleType: 'followWithHap'
});
