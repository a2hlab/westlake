"""Byte-preserving overlays on candidate04; never rebuild unrelated runtimes."""
from pathlib import Path
import os, re, subprocess, shutil, zipfile, hashlib, json, copy
ROOT=Path(__file__).resolve().parent; ARM=ROOT.parent
BRIDGE=Path('/home/dspfac/bridge-build'); SRC=BRIDGE/'src/framework'
BASE=ARM/'ime-ability-window-20260904/candidate04'
NAME=os.environ.get('CANDIDATE','candidate01'); assert re.fullmatch('candidate[0-9]+',NAME)
OUT=ROOT/NAME; assert not (OUT/'manifest.json').exists()
OUT.mkdir(exist_ok=True)
def run(*a,**kw):subprocess.run([str(x) for x in a],check=True,**kw)
SMALI=(BRIDGE/'framework-patch-tools-rescue/cp.txt').read_text().strip()+':'+str(ARM/'fullrun-20260824-final/fw-inputasync-build-7.nVPsud/smali-3.0.3.jar')+':'+str(ARM/'tools/smali-2.5.2/*')
def decode(p,out):run('java','-cp',SMALI,'com.android.tools.smali.baksmali.Main','disassemble','-o',out,p)
def assemble(tree,p):run('java','-cp',SMALI,'com.android.tools.smali.smali.Main','assemble','-a','30','-o',p,tree)
for part in ('fw','runtime'):
    (OUT/part).mkdir(exist_ok=True)
    for p in (BASE/part).iterdir():
        if p.is_file():shutil.copy2(p,OUT/part/p.name)
classes=OUT/'classes';classes.mkdir(exist_ok=True)
sources=[SRC/'window/java'/ (name+'.java') for name in ('OhImeBridge','OhImeMetadata','OhInputMethodSession','InputMethodManagerAdapter','OhInsetsBridge','OhWebViewBridge','OHTouchInjector','SessionStageBridge','WindowCallbackBridge')]
sources+=list((SRC/'appspawn-x/java/adapter/security').glob('*.java'))
sources.append(SRC/'appspawn-x/java/adapter/compat/WestlakeSecureRandomSpi.java')
sources += [SRC/'package-manager/java'/name for name in ('PermissionManagerAdapter.java', 'PermissionMapper.java')]
run('javac','--release','8','-parameters','-classpath',str(ARM/'bcp-build/cp/framework-classes.jar')+':/home/dspfac/android-sdk/platforms/android-34/android.jar','-d',classes,*sources)
run('jar','cf',OUT/'overlay.jar','-C',classes,'.');(OUT/'dex').mkdir(exist_ok=True)
run('/home/dspfac/android-sdk/build-tools/34.0.0/d8','--min-api','30','--output',OUT/'dex',OUT/'overlay.jar')
decode(OUT/'dex/classes.dex',OUT/'overlay')
# Only intentional replacements, not every class javac compiled as a dependency.
selected={str(p.relative_to(OUT/'overlay')):p for p in (OUT/'overlay').rglob('*.smali')
    if p.name.startswith(('OhImeBridge','OhImeMetadata','OhInputMethodSession','InputMethodManagerAdapter','OhInsetsBridge','OhWebViewBridge','OHTouchInjector','SessionStageBridge','WindowCallbackBridge','OhTrustBridge','OhPeerCertificates','WestlakeSecureRandomSpi','PermissionManagerAdapter','PermissionMapper'))}
targets=[p[:-6] for p in selected]+['adapter/window/WindowSessionAdapter','android/webkit/WebView','adapter/core/OHServiceManager','adapter/compat/WestlakeSSLSocket']
changed={}
for jar in (BASE/'fw').glob('*.jar'):
    replacements={}
    with zipfile.ZipFile(jar) as z:
        for entry in z.namelist():
            if not re.fullmatch('classes[0-9]*.dex',entry):continue
            data=z.read(entry)
            if not any(('L'+name+';').encode() in data for name in targets):continue
            stem=jar.stem+'-'+entry;tree=OUT/stem
            original=OUT/stem.replace('.dex','-original.dex');original.write_bytes(data);decode(original,tree)
            edits=[]
            for name,source in selected.items():
                dest=tree/name
                if dest.exists():shutil.copy2(source,dest);edits.append(name)
            if jar.name=='oh-adapter-framework.jar' and entry=='classes.dex':
                for name,source in selected.items():
                    if name.startswith(('adapter/window/OhImeBridge','adapter/window/OhImeMetadata','adapter/window/OhInputMethodSession','adapter/window/OhInsetsBridge','adapter/window/OhWebViewBridge','adapter/packagemanager/PermissionManagerAdapter','adapter/security/OhPeerCertificates')):
                        (tree/name).parent.mkdir(parents=True,exist_ok=True)
                        shutil.copy2(source,tree/name);edits.append(name)
            if jar.name=='adapter-runtime-bcp.jar' and entry=='classes.dex':
                for name,source in selected.items():
                    if name.startswith('adapter/compat/WestlakeSecureRandomSpi'):
                        (tree/name).parent.mkdir(parents=True,exist_ok=True)
                        shutil.copy2(source,tree/name);edits.append(name)
            session=tree/'adapter/window/WindowSessionAdapter.smali'
            manager=tree/'adapter/core/OHServiceManager.smali'
            handler=tree/'com/android/internal/os/AppSpawnXInit$2.smali'
            if handler.exists():
                text=handler.read_text()
                assert 'val$hotMethods:Ljava/util/Set;' in text
                needle='.method public invoke(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;\n    .registers 9'
                assert text.count(needle)==1
                text=text.replace(needle,needle+'''
    invoke-static {p2, p3}, Ladapter/packagemanager/PermissionManagerAdapter;->proxyPermissionResult(Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Integer;
    move-result-object v0
    if-eqz v0, :gap_original_proxy
    return-object v0
    :gap_original_proxy
''')
                handler.write_text(text);edits.append('fail-closed-permission-proxy-only')
            if manager.exists():
                text=manager.read_text()
                needle='.method private static lookupAdapter(Ljava/lang/String;)Landroid/os/IBinder;\n    .registers 4'
                assert text.count(needle)==1
                text=text.replace(needle,needle+'''
    const-string v0, "permissionmgr"
    invoke-virtual {v0, p0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v0
    if-eqz v0, :gap_original_lookup
    invoke-static {}, Ladapter/packagemanager/PermissionManagerAdapter;->getInstance()Ladapter/packagemanager/PermissionManagerAdapter;
    move-result-object v0
    invoke-virtual {v0}, Ladapter/packagemanager/PermissionManagerAdapter;->asBinder()Landroid/os/IBinder;
    move-result-object v0
    return-object v0
    :gap_original_lookup
''')
                manager.write_text(text);edits.append('permission-service-route-only')
            ssl=tree/'adapter/compat/WestlakeSSLSocket.smali'
            if ssl.exists():
                text=ssl.read_text()
                needle='.method private static native nativePeerCert(J)[B'
                assert text.count(needle)==1
                text=text.replace(needle,'.method private static native nativePeerChain(J)[B\n.end method\n\n'+needle)
                text,n=re.subn(r'(invoke-static \{p0, p1\}, Ladapter/compat/WestlakeSSLSocket;->)nativePeerCert(\(J\)\[B)',r'\1nativePeerChain\2',text)
                assert n==1
                ssl.write_text(text);edits.append('tls-chain-native')
                peer=tree/'adapter/compat/WestlakeSSLSocket$Session.smali'
                text=peer.read_text()
                begin=text.index('.method public getPeerCertificates()');end=text.index('.end method',begin)+len('.end method')
                method='''.method public getPeerCertificates()[Ljava/security/cert/Certificate;
    .registers 5
    iget-object v0, p0, Ladapter/compat/WestlakeSSLSocket$Session;->peer:[Ljava/security/cert/Certificate;
    if-nez v0, :gap_cached_chain
    iget-object v1, p0, Ladapter/compat/WestlakeSSLSocket$Session;->s:Ladapter/compat/WestlakeSSLSocket;
    invoke-static {v1}, Ladapter/compat/WestlakeSSLSocket;->access$000(Ladapter/compat/WestlakeSSLSocket;)J
    move-result-wide v1
    invoke-static {v1, v2}, Ladapter/compat/WestlakeSSLSocket;->access$100(J)[B
    move-result-object v0
    invoke-static {v0}, Ladapter/security/OhPeerCertificates;->decode([B)[Ljava/security/cert/Certificate;
    move-result-object v0
    iput-object v0, p0, Ladapter/compat/WestlakeSSLSocket$Session;->peer:[Ljava/security/cert/Certificate;
    :gap_cached_chain
    invoke-virtual {v0}, [Ljava/security/cert/Certificate;->clone()Ljava/lang/Object;
    move-result-object v0
    check-cast v0, [Ljava/security/cert/Certificate;
    return-object v0
.end method'''
                peer.write_text(text[:begin]+method+text[end:]);edits.append('tls-standard-full-chain')
            if session.exists():
                text=session.read_text()
                needle='.method public remove(Landroid/view/IWindow;)V\n    .registers 5'
                assert text.count(needle)==1
                text=text.replace(needle,needle+'\n    invoke-static {p1}, Ladapter/window/OhInsetsBridge;->remove(Landroid/view/IWindow;)V')
                # Preserve all pinned window/surface behavior; replace only
                # empty server-insets assignments, using method p1=IWindow.
                pattern=r'    invoke-virtual \{(v\d+|p\d+), (v\d+)\}, Landroid/view/InsetsState;->set\(Landroid/view/InsetsState;\)V'
                text,n=re.subn(pattern,lambda m:'    move-object/from16 '+m[2]+', p1\n    invoke-static {'+m[2]+', '+m[1]+'}, Ladapter/window/OhInsetsBridge;->copyForWindow(Landroid/view/IWindow;Landroid/view/InsetsState;)V',text)
                assert n==3,(stem,n)
                # Reverse-push bootstrap has its own empty state.
                methods=text.split('.method ')
                for i,method in enumerate(methods):
                    if 'relayout(' not in method.split('\n')[0]:continue
                    # Pinned D8 register allocation: v16/v17 are dead until
                    # the immediately following resized argument setup.
                    needle='    invoke-direct/range {v20 .. v20}, Landroid/view/InsetsState;-><init>()V'
                    assert method.count(needle)==1
                    assert '    move-object/from16 v16, p1\n\n    move-object/from16 v17, v0' in method
                    method=method.replace(needle,needle+'\n    move-object/from16 v16, p1\n    move-object/from16 v17, v20\n    invoke-static/range {v16 .. v17}, Ladapter/window/OhInsetsBridge;->copyForWindow(Landroid/view/IWindow;Landroid/view/InsetsState;)V')
                    methods[i]=method
                session.write_text('.method '.join(methods));edits.append('window-insets-only')
            web=tree/'android/webkit/WebView.smali'
            if web.exists():
                text=web.read_text();signature='.method protected constructor <init>(Landroid/content/Context;Landroid/util/AttributeSet;IILjava/util/Map;Z)V'
                begin=text.index(signature);end=text.index('.end method',begin)
                body=text[begin:end];assert body.count('    return-void')==1
                body=body.replace('    return-void','    invoke-static {p0}, Ladapter/window/OhWebViewBridge;->initialized(Landroid/view/View;)V\n\n    return-void')
                web.write_text(text[:begin]+body+text[end:]);edits.append('webview-opt-in-software')
            if edits:
                patched=OUT/(stem+'-patched.dex');assemble(tree,patched);replacements[entry]=patched.read_bytes();changed[stem]=edits
        if replacements:
            with zipfile.ZipFile(OUT/'fw'/jar.name,'w',zipfile.ZIP_DEFLATED) as out:
                for info in z.infolist():out.writestr(copy.copy(info),replacements.get(info.filename,z.read(info.filename)))
            with zipfile.ZipFile(OUT/'fw'/jar.name) as out:
                assert z.namelist()==out.namelist()
                for entry in z.namelist():
                    if entry not in replacements:assert z.read(entry)==out.read(entry)
env=os.environ.copy();env.update(WESTLAKE_USE_BC_DIGESTS='1',FW_OVERRIDE=str(OUT/'fw'),OUT_OVERRIDE=str(OUT/'boot'),
    DEVICE_FW_DIR_OVERRIDE='/data/local/tmp/asx/gap-repair-20260904/'+NAME+'/fw',
    EXPECTED_BCP_MD5_OVERRIDE=hashlib.md5((OUT/'fw/adapter-runtime-bcp.jar').read_bytes()).hexdigest())
run('bash',ARM/'build_fwbootimg_v63_westlake_provider_first_20260904.sh',env=env)
sdk=Path('/home/dspfac/ohos-sdk-6.1/linux/native')
common=[sdk/'llvm/bin/clang++','--target=aarch64-linux-ohos','--sysroot='+str(sdk/'sysroot'),'-shared','-fPIC','-O2','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(BRIDGE/'aosp/libnativehelper/include_jni'),'-Wl,-z,defs']
run(*common,SRC/'window/jni/oh_ime_helper_capi.cpp','-lohinputmethod','-ldl','-o',OUT/'runtime/liboh_ime_helper_capi.so')
run(*common,SRC/'android-runtime/src/oh_network_jni.cpp','-o',OUT/'runtime/liboh_network_jni.so')
run(*common,SRC/'android-runtime/src/oh_webview_startup_order.cpp','-ldl','-o',OUT/'runtime/liboh_webview_startup_order.so')
platform_common=common.copy()
platform_common[0]=Path('/home/dspfac/openharmony/prebuilts/clang/ohos/linux-x86_64/15.0.4/llvm/bin/clang++')
run(*platform_common,SRC/'package-manager/jni/oh_permission_queries.cpp','-L'+str(ARM/'out/board_libs'),'-lability_access_control','-ldl','-o',OUT/'runtime/liboh_permission_queries.so')
run(*platform_common,SRC/'android-runtime/src/oh_egl_boundary.cpp','-L'+str(ARM/'out/board_libs'),'-ldl','-o',OUT/'runtime/liboh_egl_boundary.so')
from build_ime_metadata import build as build_metadata
build_metadata(OUT/'runtime/liboh_ime_metadata.so')
build_metadata(OUT/'runtime/liboh_ime_geometry.so', geometry=True)
from build_tls_helper import build as build_tls
build_tls(OUT/'runtime/liboh_tls_boundary.so')
for name,source in [('libhwui.so','baseline-libhwui.so'),('liboh_hwui_functor.so','liboh_hwui_functor.so')]:
    shutil.copy2(ROOT/source,OUT/'runtime'/name)
from patch_egl_optional import build as patch_egl
patch_egl(OUT/'runtime/libhwui.so')
manifest={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for part in ('fw','runtime','boot') for p in (OUT/part).iterdir() if p.is_file()}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(OUT/'changes.json').write_text(json.dumps(changed,indent=2)+'\n')
print('GAP_CANDIDATE_BUILD_OK',NAME)
