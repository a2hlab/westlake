#!/bin/bash
# build_apache_http_legacy.sh <aosp external/apache-http> <framework-runtime build> <out-dir>
#                             [conscrypt build]
# Builds org.apache.http.legacy.jar, Android's <uses-library android:name="org.apache.http.legacy">,
# from AOSP source: compiled against the Westlake framework classes (its android/ half uses hidden
# APIs such as conscrypt, so the conscrypt extension build's classes join the classpath), dexed for
# the runtime data's framework/ directory, which AppSchedulerBridge resolves <uses-library> names
# against.
set -e
SRC=$1; FW=$2; OUT=$3; CONSCRYPT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
D8=${D8:-$(ls -d ${ANDROID_SDK_ROOT:-$HOME/android-sdk}/build-tools/*/d8 | sort -V | tail -1)}
CP="$FW/framework.classes.jar:$FW/core-compile-only.jar"
# android/net/http's CertificateChainValidator and HttpsConnection use com.android.org.conscrypt
# (the repackaged conscrypt), which is an extension jar rather than part of the framework classes.
[ -n "$CONSCRYPT" ] && CP="$CP:$CONSCRYPT/conscrypt.classes.jar"
mkdir -p "$OUT/classes" "$OUT/dex"
find "$SRC/src" "$SRC/android/src" -name '*.java' > "$OUT/sources.txt"
javac -nowarn -source 8 -target 8 -Xlint:none -encoding UTF-8 \
  -cp "$CP" -d "$OUT/classes" @"$OUT/sources.txt"
python3 "$HERE/strip_method_parameters.py" "$OUT/classes"
(cd "$OUT/classes" && jar cf ../classes.jar .)
"$D8" --min-api 28 --release --output "$OUT/dex" "$OUT/classes.jar" \
  $(printf -- '--lib %s ' $(echo "$CP" | tr ':' ' '))
# A fixed timestamp on the entry, so two builds of the same source give the same jar byte for byte
# (zip keeps the file's mtime; -X drops only the extra fields).
touch -d '1980-01-01 00:00:00Z' "$OUT/dex/classes.dex"
(cd "$OUT/dex" && zip -q -X ../org.apache.http.legacy.jar classes.dex)
# artifacts.json for the deploy's --uses-library-build, which stages the jar as framework/<name>.jar.
python3 - "$OUT" "$SRC" <<'PY'
import hashlib, json, pathlib, subprocess, sys
out, source = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
jar = out / 'org.apache.http.legacy.jar'
data = jar.read_bytes()
revision = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'],
                          capture_output=True, text=True).stdout.strip()
(out / 'artifacts.json').write_text(json.dumps({
    'schema': 'a2hlab-source-uses-library/v1',
    'scope': "AOSP external/apache-http as Android's org.apache.http.legacy shared library",
    'source_revision': revision, 'errors': [],
    'artifacts': {jar.name: {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}},
}, indent=1, sort_keys=True) + '\n')
print('built', jar.name, len(data), 'bytes')
PY
sha256sum "$OUT/org.apache.http.legacy.jar"
