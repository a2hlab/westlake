# Offline OHOS Chinese keyboard

Accepted build: `out/pinyin.hap`, bundle `org.westlake.pinyin`, selected on the
OHOS board. Kika remains installed. See the
[device report](/home/dspfac/openharmony/docs/engine/OH-CHINESE-IME-2026-09-04.md)
for verified behavior, hashes and remaining gaps.

## Rebuild and tests

Run from this directory:

```sh
bash build_native.sh
python3 test_decoder.py
node test_controller.js
python3 build.py
python3 test_acceptance.py
```

`upstream` must be a clean checkout of
`https://gitee.com/ohos_port/applications_inputmethod` at
`cd27df54150e5d96d86757dc7315ead69d469d9b`. Engine and dictionary are Apache-2.0;
the new UI/N-API boundary is separate. `build_native.sh` uses SDK 6.1 native
compiler; `build.py` uses API-20 es2abc and **workspace Restool 4.105**, because
this board rejects SDK RestoolV2 resource indices. SDK public sample signing
keys are development-only.

Rebuilding re-signs the HAP and changes its hash. `test_acceptance.py` validates
the recorded device run, including its installed HAP hash; after a rebuild,
repeat live installation/testing and refresh evidence before expecting that
check to pass. It is not an automatic live UI test.

## Installation and use

`device.py` pins the OHOS serial and journals all commands; sends verify SHA-256.
Inspect current bundle/IME state before installation. Installation below updates
only this new keyboard bundle, not Kika or the Android host:

```sh
python3 device.py send out/pinyin.hap /data/local/tmp/asx/chinese-ime-20260904/pinyin-final.hap
python3 device.py shell 'bm install -p /data/local/tmp/asx/chinese-ime-20260904/pinyin-final.hap'
python3 device.py shell 'aa start -b org.westlake.pinyin -a EntryAbility'
```

Tap **选择输入法 / Choose keyboard**, then choose `pinyin` (the system picker may
use the bundle suffix) or `Westlake 拼音`. To revert, choose `kikakeyboard` there.
Tap the native test field for an independent control. The normal keyboard has
no Internet/clipboard permissions; the native diagnostic field logs its text.

Use QWERTY Pinyin and tap a candidate to commit. Space or Enter while composing
selects the first candidate; Enter after committing invokes the editor action.
Cancel discards the current composition, and Backspace edits the Pinyin first
or deletes before the cursor when no composition exists. `中/英` and `123`
switch modes. Hide commits unresolved raw Pinyin as Latin text, then hides.

`tap_keyboard.py nihao --commit` replays physical screen taps only. It assumes
the tested 1200x1920 display, orientation, full keyboard layout and an already
focused editor. Inspect a screenshot before reusing coordinates on another
layout. It is not a latency benchmark or direct text-insertion helper.

## Evidence

`device-commands.jsonl` records command times and output. `.tree` files are
Android view dumps; `.jpeg` files are device screenshots. Native control and
OHOS IME/Android callback logs establish real preview and commit paths.
`test_acceptance.py` checks the recorded editor values, installed HAP, no requested
permissions, unchanged dictionary, preview callbacks and retained baseline hashes.

Early `install.log` / PID 31562 capture the failed RestoolV2 package. `install02.log`
is the resource-format fix. `install03.log` installs the final layout-adjusted
package. Native phrase/deletion tests used install02's identical controller and
decoder; final-build native rerun and all Toutiao tests used install03.

Candidate04 ART/framework and its private copied Toutiao data were reused without
rebuilding or restarting them. Search submission passed, but the known blank
results body did not; see the report before making compatibility/performance claims.
