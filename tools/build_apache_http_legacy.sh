#!/bin/bash
# build_apache_http_legacy.sh <aosp external/apache-http> <framework-runtime build> <out-dir>
# Builds org.apache.http.legacy.jar, Android's <uses-library android:name="org.apache.http.legacy">,
# from AOSP source: compiled against the Westlake framework classes (its android/ half uses hidden
# APIs such as conscrypt), dexed for the runtime data's framework/ directory, which
# AppSchedulerBridge resolves <uses-library> names against.
set -e
SRC=$1; FW=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
D8=${D8:-$(ls -d ${ANDROID_SDK_ROOT:-$HOME/android-sdk}/build-tools/*/d8 | sort -V | tail -1)}
mkdir -p "$OUT/classes" "$OUT/dex"
find "$SRC/src" "$SRC/android/src" -name '*.java' > "$OUT/sources.txt"
javac -nowarn -source 8 -target 8 -Xlint:none -encoding UTF-8 \
  -cp "$FW/framework.classes.jar:$FW/core-compile-only.jar" -d "$OUT/classes" @"$OUT/sources.txt"
python3 "$HERE/strip_method_parameters.py" "$OUT/classes"
(cd "$OUT/classes" && jar cf ../classes.jar .)
"$D8" --min-api 28 --release --output "$OUT/dex" "$OUT/classes.jar" \
  --lib "$FW/framework.classes.jar" --lib "$FW/core-compile-only.jar"
(cd "$OUT/dex" && zip -q -X ../org.apache.http.legacy.jar classes.dex)
sha256sum "$OUT/org.apache.http.legacy.jar"
