"""Compile just the shared TLS boundary, not a drifted full AndroidRuntime.

All six existing socket methods and the new chain method share one registry;
mixing a new chain reader with a different DSO's lifetime registry is unsafe.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
SRC = Path('/home/dspfac/bridge-build/src/framework/android-runtime/src/AndroidRuntime.cpp')
SDK = Path('/home/dspfac/ohos-sdk-6.1/linux/native')

def build(output):
    source = SRC.read_text()
    start = source.index('namespace {', source.index('struct WlSslApi') - 30)
    end = source.index('}  // namespace', source.index('static void WL_TLS_close'))
    body = source[start:end] + '}\n'
    # Opt-in production diagnostics are preserved; no logging switch is enabled.
    includes = '\n'.join('#include <' + h + '>' for h in (
        'jni.h', 'dlfcn.h', 'pthread.h', 'poll.h', 'fcntl.h', 'unistd.h',
        'sys/socket.h', 'cerrno', 'cstdint', 'cstdio', 'cstdlib', 'cstring',
        'map', 'string', 'vector', 'time.h'))
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
    unit = output.parent / 'oh_tls_boundary.generated.cpp'
    unit.write_text(includes + '\n' + body + registration)
    subprocess.run([str(SDK/'llvm/bin/clang++'), '--target=aarch64-linux-ohos',
        '--sysroot='+str(SDK/'sysroot'), '-shared', '-fPIC', '-O2', '-std=c++17',
        '-Wall', '-Wextra', '-Werror', '-Wl,-z,defs',
        '-I/home/dspfac/bridge-build/aosp/libnativehelper/include_jni', str(unit),
        '-ldl', '-o', str(output)], check=True)

if __name__ == '__main__':
    build(ROOT/'liboh_tls_boundary.so')
