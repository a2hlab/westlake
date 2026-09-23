#!/usr/bin/env python3
"""Fresh source-only public-SDK boundary build; never uses a candidate DSO/JAR."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tls_unit():
    source = (ROOT / 'framework/android-runtime/src/AndroidRuntime.cpp').read_text()
    start = source.index('namespace {', source.index('struct WlSslApi') - 30)
    end = source.index('}  // namespace', source.index('static void WL_TLS_close'))
    body = source[start:end] + '}\n'
    headers = ['jni.h', 'dlfcn.h', 'pthread.h', 'poll.h', 'fcntl.h', 'unistd.h',
               'sys/socket.h', 'cerrno', 'cstdint', 'cstdio', 'cstdlib', 'cstring',
               'map', 'string', 'vector', 'time.h']
    registration = r'''
extern "C" int westlake_tls_child_register(JNIEnv* env) {
    jclass cls = env->FindClass("adapter/compat/WestlakeSSLSocket");
    if (!cls) return 0;
    JNINativeMethod methods[] = {
#define M(name, sig, fn) {const_cast<char*>(name), const_cast<char*>(sig), reinterpret_cast<void*>(fn)}
        M("nativeHandshake", "(ILjava/lang/String;I)J", WL_TLS_handshake),
        M("nativeRead", "(JI[BIII)I", WL_TLS_read),
        M("nativeWrite", "(JI[BIII)I", WL_TLS_write),
        M("nativePeerCert", "(J)[B", WL_TLS_peerCert),
        M("nativePeerChain", "(J)[B", WL_TLS_peerChain),
        M("nativeInfo", "(JI)Ljava/lang/String;", WL_TLS_info),
        M("nativeClose", "(J)V", WL_TLS_close),
#undef M
    };
    int ok = env->RegisterNatives(cls, methods, sizeof(methods)/sizeof(methods[0])) == JNI_OK;
    env->DeleteLocalRef(cls);
    fprintf(stderr, "[OH_TLS_CHAIN] unified TLS registry installed=%d\n", ok);
    return ok;
}
'''
    return '\n'.join('#include <' + h + '>' for h in headers) + '\n' + body + registration


def build(sdk, output):
    sdk = sdk.resolve()
    output = output.absolute()
    if output.exists():
        raise ValueError('Build output must be a NEW directory: ' + str(output))
    compiler = sdk / 'llvm/bin/clang++'
    readelf = sdk / 'llvm/bin/llvm-readelf'
    if not compiler.is_file() or not (sdk / 'sysroot/usr/include').is_dir():
        raise ValueError('Incomplete OH native SDK')
    output.mkdir(parents=True)
    generated = output / 'oh_tls_boundary.generated.cpp'
    generated.write_text(tls_unit())
    # Remap source/build locations so __FILE__, diagnostics and debug records do
    # not make outputs depend on where a clean checkout was placed.
    flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
             '-shared', '-fPIC', '-O2', '-std=c++17', '-Wall', '-Wextra', '-Werror',
             '-Wl,-z,defs', '-Wl,--build-id=sha1', '-Wdate-time',
             '-ffile-prefix-map=' + str(ROOT) + '=/src/westlake',
             '-ffile-prefix-map=' + str(output) + '=/build/native',
             '-ffile-prefix-map=' + str(sdk) + '=/toolchain/native',
             '-I' + str(ROOT / 'third_party/jni')]
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C')
    entries = [
        ('liboh_network_jni.so', ROOT / 'framework/android-runtime/src/oh_network_jni.cpp', []),
        ('liboh_webview_startup_order.so', ROOT / 'framework/android-runtime/src/oh_webview_startup_order.cpp', ['-ldl']),
        ('liboh_ime_helper_capi.so', ROOT / 'framework/window/jni/oh_ime_helper_capi.cpp', ['-lohinputmethod', '-lnative_window_manager', '-ldl']),
        ('liboh_tls_boundary.so', generated, ['-ldl']),
        ('liboh_popen_boundary.so', ROOT / 'framework/android-runtime/src/oh_popen_boundary.cpp', ['-ldl']),
        ('liboh_process_cpu_time.so', ROOT / 'framework/android-runtime/src/oh_process_cpu_time.cpp', ['-DOH_PROCESS_CPU_OVERLAY', '-ldl']),
    ]
    artifacts = {}
    for name, source, libraries in entries:
        command = [str(compiler), *flags, '-Wl,-soname,' + name,
                   str(source), *libraries, '-o', str(output / name)]
        subprocess.run(command, check=True, env=env)
        dynamic = subprocess.check_output([str(readelf), '-d', str(output / name)], text=True)
        symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--wide', str(output / name)], text=True)
        (output / (name + '.dynamic.txt')).write_text(dynamic)
        (output / (name + '.symbols.txt')).write_text(symbols)
        artifacts[name] = {'sha256': sha(output / name), 'bytes': (output / name).stat().st_size}
        print('BUILT', name, artifacts[name]['sha256'], flush=True)
    (output / 'artifacts.json').write_text(json.dumps(artifacts, sort_keys=True, indent=2) + '\n')
    return artifacts


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build(args.sdk, args.out)
