// Offline controller-contract tests; device runs separately validate the real APIs.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(__dirname + '/ServiceExtAbility.js', 'utf8');
async function setup(previewSupported = true) {
    const listeners = {}, storage = new Map(), calls = [];
    const panel = {resize:async()=>{}, setUiContent:async()=>{}};
    let raw = '';
    const dictionary = {nihao:'你好', zhongguo:'中国'};
    const modules = {
        InputMethodExtensionAbility: class {},
        inputMethodEngine: {getInputMethodAbility:()=>({on:(n,cb)=>listeners[n]=cb,
            off:n=>delete listeners[n], createPanel:async()=>panel}), PanelType:{}, PanelFlag:{}},
        display:{getDefaultDisplaySync:()=>({width:1200,height:1920})},
        'file.fs':{openSync:()=>({fd:1}),writeSync:()=>{},closeSync:()=>{},OpenMode:{}},
        hilog:{info:()=>{}},
        westlake_pinyin:{open:()=>{},reset:()=>{raw='';},search:value=>{
            raw=value; return {candidates:value ? [dictionary[value] || '你'] : [],fixed:0,consumed:0};
        }, choose:()=>({done:true,candidates:[dictionary[raw] || '你']})}
    };
    const context = {exports:{},requireNapi:name=>modules[name],AppStorage:{
        setOrCreate:(k,v)=>storage.set(k,v), get:k=>storage.get(k)}};
    vm.createContext(context); vm.runInContext(source+'\nexports.Controller = Controller;',context);
    const c = new context.exports.Controller({filesDir:'/test',resourceManager:{getRawFileContent:async()=>new Uint8Array(1)}});
    await c.start();
    function client(label, supported) {
        const object = {getEditorAttribute:async()=>({isTextPreviewSupported:supported,enterKeyType:3})};
        for (const method of ['setPreviewText','finishTextPreview','insertText','deleteForward','sendKeyFunction']) {
            object[method]=async(...args)=>{calls.push([label,method,...args]);};
        }
        return object;
    }
    const a=client('a',previewSupported);
    const focus = target => listeners.inputStart({hide:async()=>calls.push(['hide'])},target);
    focus(a); await c.editorReady;
    async function type(text) { for(const key of text)c.key(key); await c.serial; }
    return {c,calls,a,client,focus,type};
}
async function main() {
    let t=await setup(); await t.type('nihao'); t.c.choose(0); await t.c.serial;
    assert.equal(t.calls.filter(x=>x[1]==='setPreviewText').at(-1)[2],'你好');
    assert.equal(t.calls.at(-1)[1],'finishTextPreview'); assert.equal(t.c.raw,'');
    t.c.key('enter'); await t.c.serial; assert.deepEqual(t.calls.at(-1),['a','sendKeyFunction',3]);
    console.log('PASS composition, candidate commit, editor action');

    t=await setup(false); await t.type('nihao'); assert.equal(t.calls.length,0);
    t.c.choose(0); await t.c.serial; assert.deepEqual(t.calls,[['a','insertText','你好']]);
    console.log('PASS no-preview editor fallback');

    t=await setup(); await t.type('ni'); t.c.key('backspace'); await t.c.serial;
    assert.equal(t.c.raw,'n'); t.c.key('backspace'); await t.c.serial;
    assert.equal(t.c.preview,false); assert.equal(t.calls.at(-1)[1],'finishTextPreview');
    t.c.key('backspace'); await t.c.serial; assert.deepEqual(t.calls.at(-1),['a','deleteForward',1]);
    console.log('PASS composition and committed-text backspace');

    t=await setup(); await t.type('nihao'); t.c.key('cancel'); await t.c.serial;
    assert.equal(t.calls.at(-2)[2],''); assert.equal(t.calls.at(-1)[1],'finishTextPreview');
    assert.equal(t.c.raw,''); console.log('PASS cancel composition');

    t=await setup(); let release;
    t.a.setPreviewText=(...args)=>{t.calls.push(['a','setPreviewText',...args]);return new Promise(r=>release=r);};
    t.c.key('n'); await new Promise(r=>setImmediate(r));
    t.c.key('i'); t.focus(t.client('b',true)); release(); await t.c.serial;
    assert.equal(t.calls.length,1); assert.equal(t.c.preview,false); assert.equal(t.c.raw,'');
    console.log('PASS focus change cancels in-flight and queued edits');

    t=await setup(); t.c.key('n'); t.c.key('i'); t.c.key('h'); t.c.key('a'); t.c.key('o');
    t.c.choose(0); await t.c.serial;
    assert.deepEqual(t.calls.filter(x=>x[1]==='setPreviewText').map(x=>x[2]),['n','ni','nih','niha','nihao','你好']);
    console.log('PASS rapid input is serialized');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
