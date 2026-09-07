#!/usr/bin/env python3
"""Build oh-adapter-runtime.jar from source. No decompiled dex, no base jar.

Same contract as build_native.py: a NEW output directory, every error fatal,
deterministic output, recorded SHA256.

What changed relative to the frontline builder
----------------------------------------------
That one compiled the routing class and then merged the result into a shipped
classes.dex whose string table had been byte-patched to rename
ActivityManagerAdapter to ActivityManagerRouting. It worked, and it meant the
jar could never be built from source alone: every rebuild started from a binary
nobody could regenerate. This builder compiles every class it ships.

The rename is unnecessary here for the same reason the byte patch was necessary
there: westlake owns ActivityManagerRouting as source, so AppSpawnXInit can name
it directly.

Toolchain
---------
javac comes from JAVA_HOME or PATH. A dexer does not ship with a JDK and is not
in the locked toolchain set, so the operator supplies one: --r8 <r8.jar> (run
through java, which is how Google publishes it) or --d8 <executable>. Without
either this fails closed rather than emitting a jar with no dex in it. The
dexer's SHA256 and self-reported version go into build.json, because it is a
build input whose identity is not otherwise pinned anywhere.

About --framework
-----------------
It exists, and today nothing can satisfy it. The adapter's framework.jar is a
*dex* archive -- classes.dex, classes2.dex, classes3.dex -- and javac cannot
compile against dex. That is not an inconvenience to work around; it is exactly
the cold-start dependency cycle full-build-gaps.json records, seen from the
compile side.

So the compile-only stand-ins under framework/activity/stubs are not a shortcut
taken for speed, they are the only available path, and they are the path the
frontline validated on hardware. Five platform types are involved, and bytecode
references them by name and descriptor, so a stand-in that declares the same
shape produces the same constant-pool entries as the real class would. build.json
records which classpath was used so nobody has to infer it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

# Exactly what ships inside the app-side runtime jar, named file by file.
#
# A source *root* would be wrong twice over. framework/activity/java also holds
# ActivityManagerAdapter and fourteen siblings that extend IActivityManager.Stub
# and need the real framework.jar, and framework/security/java also holds
# westlake.tls, which belongs to wl-tls-min.jar and is loaded through a separate
# DexClassLoader at runtime. Shipping either set here would be wrong, and an
# explicit list is also the "duplicate-free class ownership inventory"
# full-build-gaps.json asks for: this is the answer to "what is in that jar".
JAR_SOURCES = [
    # The IActivityManager the adapter binds.
    'framework/activity/java/ActivityManagerRouting.java',
    # The JSSE front door and its loader. The provider they gate lives in
    # wl-tls-min.jar and is deliberately not compiled in here.
    'framework/security/java/WlTlsGate.java',
    'framework/security/java/WlTlsBootstrap.java',
    # okhttp's Android10Platform calls android.net.ssl.SSLSockets on SDK >= 29
    # without probing for it; this adapter's framework.jar has no android.net.ssl
    # at all, so the class has to ride along in the app-side jar.
    'framework/security/java/android/net/ssl/SSLSockets.java',
    # Network degradation policy and the HPACK decoder it classifies with. Pure
    # policy plus a decoder -- no I/O and no platform types, so they compile
    # against the JDK alone.
    'framework/net/java/adapter/net/WlDegrade.java',
    'framework/net/java/adapter/net/WlH2Path.java',
]

SOURCE_ROOTS = [ROOT / 'framework/activity/java', ROOT / 'framework/security/java',
                ROOT / 'framework/net/java']

# Type-check-only stand-ins for platform classes this tree does not carry. Never
# dexed: the class list handed to d8 is built from SOURCE_ROOTS alone.
STUB_ROOT = ROOT / 'framework/activity/stubs'

# Fixed timestamp for every zip entry. Zip stores local time with no zone, so
# without this the jar's bytes depend on when and where it was built.
ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def java_tool(name, explicit=None):
    if explicit:
        tool = Path(explicit)
        if not tool.is_file():
            raise ValueError('No such tool: ' + str(tool))
        return tool
    home = os.environ.get('JAVA_HOME')
    if home:
        candidate = Path(home) / 'bin' / name
        if candidate.is_file():
            return candidate
    found = shutil.which(name)
    if not found:
        raise ValueError('%s not found (set JAVA_HOME or pass it explicitly)' % name)
    return Path(found)


def sources():
    found = []
    for relative in JAR_SOURCES:
        path = ROOT / relative
        if not path.is_file():
            raise ValueError('Declared jar source is missing: ' + relative)
        found.append(path)
    return found


def archive(path, entries):
    """Write a deterministic jar: sorted names, fixed timestamps, no extras."""
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as jar:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, ZIP_EPOCH)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            jar.writestr(info, entries[name])


def build(output, d8=None, r8=None, framework=None, javac=None, classes_only=False,
          release='11', baseline_dex=None):
    output = output.absolute()
    if output.exists():
        raise ValueError('Build output must be a NEW directory: ' + str(output))
    if not classes_only and not (d8 or r8):
        raise ValueError('a dexer is required to produce a jar: pass --r8 <r8.jar> or '
                         '--d8 <executable>, or use --classes-only')
    if d8 and r8:
        raise ValueError('Choose one dexer: --r8 or --d8, not both')

    compiler = java_tool('javac', javac)
    dexer = None
    if r8:
        dexer = Path(r8)
        if not dexer.is_file():
            raise ValueError('No such r8 jar: ' + str(dexer))
    elif d8:
        dexer = Path(d8)
        if not dexer.is_file():
            raise ValueError('No such d8: ' + str(dexer))

    output.mkdir(parents=True)
    classes = output / 'classes'
    classes.mkdir()

    java_sources = sources()
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C')

    argfile = output / 'sources.txt'
    argfile.write_text('\n'.join(str(p) for p in java_sources) + '\n')

    if framework:
        compile_classpath = str(framework)
    else:
        # Build the stand-ins into their own directory and put *that* on the
        # classpath. Compiling them alongside the shipped sources would emit
        # their .class files into the jar's output tree, where they would shadow
        # the real platform classes at runtime -- the leak check below exists
        # because that is exactly what happened the first time.
        stub_classes = output / 'stub-classes'
        stub_classes.mkdir()
        stub_sources = sorted(STUB_ROOT.rglob('*.java'))
        if not stub_sources:
            raise ValueError('No compile-only stubs under ' + str(STUB_ROOT))
        subprocess.run([str(compiler), '--release', release, '-nowarn',
                        '-d', str(stub_classes), *[str(s) for s in stub_sources]],
                       check=True, env=env)
        compile_classpath = str(stub_classes)

    subprocess.run([str(compiler), '--release', release, '-nowarn', '-encoding', 'UTF-8',
                    '-classpath', compile_classpath, '-d', str(classes),
                    '@' + str(argfile)], check=True, env=env)

    emitted = sorted(p for p in classes.rglob('*.class'))
    # A stand-in that reached the output would end up in the jar and shadow the
    # real platform class at runtime. Refuse rather than ship it.
    stub_names = {p.relative_to(STUB_ROOT).with_suffix('.class').as_posix()
                  for p in STUB_ROOT.rglob('*.java')} if STUB_ROOT.is_dir() else set()
    leaked = sorted(p.relative_to(classes).as_posix() for p in emitted
                    if p.relative_to(classes).as_posix() in stub_names)
    if leaked:
        raise ValueError('Compile-only stubs reached the output: ' + ', '.join(leaked))

    record = {
        'sources': len(java_sources),
        'classes': len(emitted),
        'classpath': str(framework) if framework else 'compile-only stubs',
        'compiled_against_real_framework': bool(framework),
    }
    if framework:
        record['framework_sha256'] = sha(Path(framework))
    print('COMPILED', len(java_sources), 'sources ->', len(emitted), 'classes', flush=True)

    if classes_only:
        (output / 'build.json').write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
        return record

    dex_dir = output / 'dex'
    dex_dir.mkdir()
    class_list = output / 'classes.txt'
    class_list.write_text('\n'.join(str(p) for p in emitted) + '\n')

    # Transitional fusion.
    #
    # The source inventory above does not cover everything the deployed runtime
    # provides. AppSpawnXInit, AppSchedulerBridge and the rest need platform
    # classes javac cannot see -- the adapter's framework.jar is dex-only -- and
    # PmProxy has no source anywhere. Until that closes, a jar built from source
    # alone is missing the child bootstrap entry point, so it cannot replace the
    # deployed runtime; check_runtime_replacement.py refuses exactly that swap.
    #
    # A baseline dex supplies those classes. This is a step back from "no
    # prebuilt dex" and is meant to be temporary. Two things keep it honest: the
    # overlap check below, and build.json listing which classes came from source
    # and which did not, so nobody has to infer the split later.
    baseline_inputs = []
    if baseline_dex:
        baseline = Path(baseline_dex)
        if not baseline.is_file():
            raise ValueError('No such baseline dex: ' + str(baseline))
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from dex_class_inventory import descriptor_to_binary, read_reference
        baseline_classes = {descriptor_to_binary(d) for d in read_reference(baseline)}
        from_source = {q.relative_to(classes).with_suffix('').as_posix().replace('/', '.')
                       for q in emitted}
        # d8 rejects a duplicate definition with a message that does not say
        # which input each copy came from. Catch it here, where we can say.
        clash = sorted(baseline_classes & from_source)
        if clash:
            raise ValueError('Baseline dex and compiled sources both define: '
                             + ', '.join(clash[:8]))
        baseline_inputs = [str(baseline)]
        record['baseline_dex'] = {'path': str(baseline), 'sha256': sha(baseline),
                                  'classes': sorted(baseline_classes)}
        record['classes_from_source'] = sorted(from_source)
        record['fully_source_built'] = False
        print('FUSED baseline', baseline.name, len(baseline_classes), 'classes', flush=True)
    else:
        record['fully_source_built'] = True
    if r8:
        java = java_tool('java')
        invoke = [str(java), '-cp', str(dexer), 'com.android.tools.r8.D8']
    else:
        invoke = [str(dexer)]
    subprocess.run(invoke + ['--release', '--min-api', '30', '--output', str(dex_dir),
                             '@' + str(class_list)] + baseline_inputs,
                   check=True, env=env)

    # The dexer is a build input whose identity nothing else pins. Record it.
    # `invoke` is the bare command with no build arguments appended, so asking it
    # for its version cannot accidentally re-run a dex.
    try:
        version = subprocess.check_output(invoke + ['--version'], text=True,
                                          stderr=subprocess.STDOUT).strip()
    except Exception as error:
        version = '<unavailable: %s>' % error
    record['dexer'] = {'path': str(dexer), 'sha256': sha(dexer), 'version': version}

    dexes = sorted(p for p in dex_dir.glob('classes*.dex'))
    if not dexes:
        raise ValueError('d8 produced no dex')

    jar = output / 'oh-adapter-runtime.jar'
    entries = {p.name: p.read_bytes() for p in dexes}
    # A minimal manifest, written by us so its bytes are ours and not the
    # jar tool's idea of the current date.
    entries['META-INF/MANIFEST.MF'] = b'Manifest-Version: 1.0\r\n\r\n'
    archive(jar, entries)

    record.update({'sha256': sha(jar), 'bytes': jar.stat().st_size,
                   'dex': [p.name for p in dexes]})
    (output / 'build.json').write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    print('BUILT oh-adapter-runtime.jar', record['sha256'], flush=True)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--d8', help='Path to a d8 executable (not in the locked toolchain set)')
    parser.add_argument('--r8', help='Path to r8.jar, run through java (Google publishes D8 this way)')
    parser.add_argument('--baseline-dex',
                        help='Dex supplying the platform classes not yet buildable from '
                             'source (transitional; fully recorded in build.json)')
    parser.add_argument('--framework', help='Adapter framework.jar to compile against')
    parser.add_argument('--javac', help='Override the javac used')
    parser.add_argument('--release', default='11', help='javac --release (default 11)')
    parser.add_argument('--classes-only', action='store_true',
                        help='Compile and type-check only; needs neither d8 nor framework.jar')
    args = parser.parse_args()
    build(args.out, d8=args.d8, r8=args.r8, framework=args.framework, javac=args.javac,
          classes_only=args.classes_only, release=args.release,
          baseline_dex=args.baseline_dex)
