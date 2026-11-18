#!/system/bin/sh
# Run on an authorized development board as root. No Android APK/account data is used.
set -eu
if [ "$#" -ne 3 ]; then
    echo 'usage: repeated-overlay.sh NAMESPACE_BINARY HOST_FILES_DIRECTORY HOST_UID' >&2
    exit 2
fi
launcher=$1
host_files=$2
host_uid=$3
case "$host_files" in /data/app/el2/100/base/*/files) ;; *) exit 2 ;; esac
case "$host_uid" in ''|*[!0-9]*) exit 2 ;; esac
[ "$host_uid" -ge 20000000 ]
[ "$(id -u)" -eq 0 ]
policy=/system/etc/selinux/targeted/contexts/sehap_contexts
[ -s "$policy" ]
expected=$(sha256sum "$policy" | cut -d ' ' -f 1)
fixture=$(mktemp -d "$host_files/a2hlab-source-overlay-test-XXXXXX")
# Each invocation below exits before cleanup, releasing its private mounts.
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/private-tmp/asx" "$fixture/app-data" "$fixture/i18n/etc/icu" \
    "$fixture/etc/selinux/targeted/contexts" "$fixture/fonts" "$fixture/android-app"
# Reproduce the empty mount target left on disk by the original launcher.
placeholder="$fixture/etc/selinux/targeted/contexts/sehap_contexts"
: > "$placeholder"
echo override > "$fixture/etc/selinux/westlake-test.conf"
echo font-fixture > "$fixture/fonts/westlake-test.font"
echo apk-fixture > "$fixture/android-app/base.apk"
chown -R "$host_uid:$host_uid" "$fixture"
install=/data/app/~~AAAAAAAAAAAAAAAAAAAAAA==/org.westlake.fixture-BBBBBBBBBBBBBBBBBBBBBB==
for iteration in 1 2 3; do
    WESTLAKE_ANDROID_APP_DIR="$install" WL_OVERLAY_TEST_POLICY="$expected" \
        "$launcher" "$fixture" "$host_uid" /system/bin/sh -c '
            set -eu
            actual=$(sha256sum /system/etc/selinux/targeted/contexts/sehap_contexts | cut -d " " -f 1)
            [ "$actual" = "$WL_OVERLAY_TEST_POLICY" ]
            [ "$(cat /system/etc/selinux/westlake-test.conf)" = override ]
            [ "$(cat /system/fonts/westlake-test.font)" = font-fixture ]
            [ "$(cat "$WESTLAKE_ANDROID_APP_DIR/base.apk")" = apk-fixture ]
        '
    [ -f "$placeholder" ]
    [ ! -s "$placeholder" ]
    [ "$(find "$fixture/etc" -type f | wc -l)" -eq 2 ]
    [ "$(find "$fixture/fonts" -type f | wc -l)" -eq 1 ]
    [ "$(sha256sum "$policy" | cut -d ' ' -f 1)" = "$expected" ]
    echo "SOURCE_NAMESPACE_REPEAT_PASS $iteration"
done
