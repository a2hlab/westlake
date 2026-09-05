#!/usr/bin/env bash
set -euo pipefail
cd /home/dspfac/bridge-build-arm64/ime-network-fix-20260904
sdk=/home/dspfac/android-sdk
bt=$sdk/build-tools/34.0.0
platform=$sdk/platforms/android-34/android.jar
mkdir -p control/build/classes control/build/assets control/build/keys
if [ ! -s control/build/assets/untrusted.pem ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout control/build/keys/root.key \
    -out control/build/assets/root.pem -subj /CN=Westlake-Offline-Test-Root -days 2 \
    -addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign,cRLSign
  openssl req -new -newkey rsa:2048 -nodes -keyout control/build/keys/leaf.key \
    -out control/build/leaf.csr -subj /CN=offline-test.invalid
  openssl x509 -req -in control/build/leaf.csr -CA control/build/assets/root.pem \
    -CAkey control/build/keys/root.key -set_serial 101 -days 1 -out control/build/assets/valid.pem
  openssl x509 -req -in control/build/leaf.csr -CA control/build/assets/root.pem \
    -CAkey control/build/keys/root.key -set_serial 102 \
    -not_before 20200101000000Z -not_after 20200102000000Z -out control/build/assets/expired.pem
  openssl req -x509 -newkey rsa:2048 -nodes -keyout control/build/keys/untrusted.key \
    -out control/build/assets/untrusted.pem -subj /CN=Untrusted-Offline-Test -days 2
fi
javac --release 8 -classpath "$platform" -d control/build/classes control/MainActivity.java
jar cf control/build/classes.jar -C control/build/classes .
"$bt/d8" --min-api 23 --lib "$platform" --output control/build control/build/classes.jar
"$bt/aapt2" link -I "$platform" -A control/build/assets --manifest control/AndroidManifest.xml \
  --min-sdk-version 23 --target-sdk-version 30 -o control.apk
zip -j -q control.apk control/build/classes.dex
"$bt/aapt" dump badging control.apk | head -n 2
