#!/bin/bash
# build_location_provider.sh <aosp frameworks/base> <framework-runtime build> <out-dir> <androidx-annotation sources>
# Builds com.android.location.provider.jar, Android's <uses-library> of that name, from AOSP source,
# dexed for the runtime data's framework/ directory, which AppSchedulerBridge resolves <uses-library>
# names against. Play services' location services extend its classes and can't be created without it.
set -e
SRC=$1; FW=$2; OUT=$3; ANNOTATION=$4
HERE=$(cd "$(dirname "$0")" && pwd)
D8=${D8:-$(ls -d ${ANDROID_SDK_ROOT:-$HOME/android-sdk}/build-tools/*/d8 | sort -V | tail -1)}
CP="$FW/framework.classes.jar:$FW/core-compile-only.jar"
NAME=com.android.location.provider.jar
mkdir -p "$OUT/classes" "$OUT/dex"
find "$SRC/location/lib/java" -name '*.java' > "$OUT/sources.txt"
javac -nowarn -source 11 -target 11 -Xlint:none -encoding UTF-8 -cp "$CP" -sourcepath "$ANNOTATION" -implicit:none -d "$OUT/classes" @"$OUT/sources.txt"
python3 "$HERE/strip_method_parameters.py" "$OUT/classes"
(cd "$OUT/classes" && jar cf ../classes.jar .)
"$D8" --min-api 28 --release --output "$OUT/dex" "$OUT/classes.jar" $(printf -- '--lib %s ' $(echo "$CP" | tr ':' ' '))
touch -d '1980-01-01 00:00:00Z' "$OUT/dex/classes.dex"
(cd "$OUT/dex" && zip -q -X "../$NAME" classes.dex)
python3 - "$OUT" "$NAME" <<'PY'
import hashlib, json, pathlib, sys
out, name = pathlib.Path(sys.argv[1]), sys.argv[2]
data = (out / name).read_bytes()
(out / 'artifacts.json').write_text(json.dumps({
    'schema': 'a2hlab-source-uses-library/v1',
    'scope': "AOSP frameworks/base location/lib as Android's com.android.location.provider shared library",
    'errors': [],
    'artifacts': {name: {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}},
}, indent=1, sort_keys=True) + '\n')
print('built', name, len(data), 'bytes')
PY
