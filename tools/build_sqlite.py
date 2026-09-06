#!/usr/bin/env python3
"""Fresh source-only libwlsqlite.so build; never uses a candidate DSO or board copy.

Same contract as build_native.py: a NEW output directory, every compile and link
error fatal, deterministic flags, and a recorded SHA256 for everything produced.

Two ABIs, and the choice is not cosmetic:

  --sdk <ohos-sdk/native>   Build against the pinned OpenHarmony native SDK
                            (musl). This is what the manifest workspace can
                            verify entirely offline, and passing it proves the
                            source closes: every symbol resolves and no platform
                            header is missing. It is a verification artifact.

  --ndk <ndk-root>          Build against an Android NDK (bionic). This is the
                            artifact that can actually be loaded into the
                            adapter's app process, because that process is
                            served by the Android-side linker namespace. An NDK
                            is not part of the content-locked toolchain set, so
                            the operator supplies it and its identity is
                            recorded in the build record rather than assumed.

The SQLite option set is read out of framework/sqlite/CMakeLists.txt rather than
duplicated here. There is exactly one place to change a build option, and a
CMake/NDK build and this driver cannot silently drift apart.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SQLITE = ROOT / 'framework/sqlite'
AMALGAMATION = ROOT / 'third_party/sqlite'

# Sources, in the order the frontline build used. sqlite3.c is compiled as C;
# everything else is the C++ JNI boundary.
C_SOURCES = [AMALGAMATION / 'sqlite3.c']
CXX_SOURCES = [SQLITE / name for name in (
    'wl_compat.cpp',
    'wl_jni_onload.cpp',
    'wl_CursorWindow.cpp',
    'wl_sqlite3_android.cpp',
    'android_database_SQLiteCommon.cpp',
    'android_database_SQLiteConnection.cpp',
    'android_database_CursorWindow.cpp',
)]

# See CMakeLists.txt for why each of these is silenced; they are the warnings the
# AOSP sources actually trip on a clang this much newer than they are.
CXX_QUIET = ['-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-label',
             '-Wno-unused-function', '-Wno-writable-strings', '-Wno-format-security']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sqlite_defines():
    """The SQLITE_* option set, parsed from the CMakeLists that owns it."""
    text = (SQLITE / 'CMakeLists.txt').read_text()
    match = re.search(r'set\(WL_SQLITE_DEFINES\n(.*?)\n\)', text, re.S)
    if not match:
        raise ValueError('WL_SQLITE_DEFINES block not found in framework/sqlite/CMakeLists.txt')
    defines = [line.strip() for line in match.group(1).splitlines() if line.strip()]
    if not defines:
        raise ValueError('WL_SQLITE_DEFINES is empty')
    return ['-D' + d for d in defines]


def toolchain(sdk, ndk, api):
    """Return (cc, cxx, extra_flags, record) for the requested ABI."""
    if bool(sdk) == bool(ndk):
        raise ValueError('Choose exactly one of --sdk (ohos/musl) or --ndk (bionic)')
    if sdk:
        sdk = sdk.resolve()
        cc, cxx = sdk / 'llvm/bin/clang', sdk / 'llvm/bin/clang++'
        if not cxx.is_file() or not (sdk / 'sysroot/usr/include').is_dir():
            raise ValueError('Incomplete OH native SDK: ' + str(sdk))
        flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot')]
        return cc, cxx, flags, {'abi': 'ohos-musl', 'root': str(sdk), 'deployable': False}
    ndk = ndk.resolve()
    prefix = ndk / 'toolchains/llvm/prebuilt'
    hosts = sorted(p for p in prefix.glob('*') if (p / 'bin').is_dir()) if prefix.is_dir() else []
    if not hosts:
        raise ValueError('No prebuilt host toolchain under ' + str(prefix))
    binaries = hosts[0] / 'bin'
    cc = binaries / ('aarch64-linux-android%d-clang' % api)
    cxx = binaries / ('aarch64-linux-android%d-clang++' % api)
    if not cc.is_file() or not cxx.is_file():
        raise ValueError('No aarch64 API %d cross compiler under %s' % (api, binaries))
    return cc, cxx, [], {'abi': 'android-bionic', 'root': str(ndk), 'api': api, 'deployable': True}


def build(output, sdk=None, ndk=None, api=30):
    output = output.absolute()
    if output.exists():
        raise ValueError('Build output must be a NEW directory: ' + str(output))
    if not (AMALGAMATION / 'sqlite3.c').is_file():
        raise ValueError('Vendored SQLite amalgamation missing: ' + str(AMALGAMATION))
    cc, cxx, target, record = toolchain(sdk, ndk, api)
    output.mkdir(parents=True)
    objects = output / 'obj'
    objects.mkdir()

    defines = sqlite_defines()
    includes = ['-I' + str(p) for p in (SQLITE / 'compat', SQLITE, SQLITE / 'inc',
                                        AMALGAMATION, ROOT / 'third_party/jni')]
    # Remap source and build locations so __FILE__, diagnostics and debug records
    # do not depend on where a clean checkout was placed.
    common = target + ['-fPIC', '-O2', '-fvisibility=hidden', '-Wall', '-Wdate-time',
                       '-ffile-prefix-map=' + str(ROOT) + '=/src/westlake',
                       '-ffile-prefix-map=' + str(output) + '=/build/sqlite']
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C')

    produced = []
    for source in C_SOURCES + CXX_SOURCES:
        is_cxx = source.suffix == '.cpp'
        obj = objects / (source.stem + '.o')
        language = (['-std=c++17', '-fno-rtti', '-fvisibility-inlines-hidden'] + CXX_QUIET
                    if is_cxx else ['-std=gnu11'])
        command = [str(cxx if is_cxx else cc), *common, *defines, *language,
                   *(includes if is_cxx else ['-I' + str(AMALGAMATION)]),
                   '-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True, env=env)
        produced.append(obj)
        print('CC', source.name, flush=True)

    library = output / 'libwlsqlite.so'
    # See CMakeLists.txt for the reasoning behind each of these link options.
    subprocess.run([str(cxx), *target, '-shared', '-fPIC', '-static-libstdc++',
                    '-Wl,-soname,libwlsqlite.so', '-Wl,--no-undefined',
                    '-Wl,--exclude-libs,ALL', '-Wl,--build-id=sha1',
                    '-Wl,--version-script,' + str(SQLITE / 'wlsqlite.map'),
                    *[str(o) for o in produced], '-lm', '-o', str(library)],
                   check=True, env=env)

    readelf = (sdk / 'llvm/bin/llvm-readelf') if sdk else (Path(str(cc)).parent / 'llvm-readelf')
    exported = dynamic = ''
    if Path(readelf).is_file():
        dynamic = subprocess.check_output([str(readelf), '-d', str(library)], text=True)
        symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--wide', str(library)],
                                          text=True)
        (output / 'libwlsqlite.so.dynamic.txt').write_text(dynamic)
        (output / 'libwlsqlite.so.symbols.txt').write_text(symbols)
        # Everything is -fvisibility=hidden behind a version script, so exactly
        # one defined symbol should be visible. Anything else means the script
        # or the visibility flags stopped working.
        exported = [line.split()[-1] for line in symbols.splitlines()
                    if ' FUNC ' in line and ' UND ' not in line]
        if exported != ['JNI_OnLoad']:
            raise ValueError('Unexpected exported symbols: ' + repr(exported))

    record.update({'sha256': sha(library), 'bytes': library.stat().st_size,
                   'exports': exported or ['<readelf unavailable>'],
                   'sqlite_defines': defines, 'objects': len(produced)})
    (output / 'artifacts.json').write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    print('BUILT libwlsqlite.so', record['sha256'], flush=True)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sdk', type=Path, help='OpenHarmony native SDK root (musl verification)')
    parser.add_argument('--ndk', type=Path, help='Android NDK root (bionic, deployable)')
    parser.add_argument('--api', type=int, default=30, help='Android API level for --ndk')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build(args.out, sdk=args.sdk, ndk=args.ndk, api=args.api)
