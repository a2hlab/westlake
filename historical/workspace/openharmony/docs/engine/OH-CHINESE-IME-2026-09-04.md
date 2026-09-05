# OHOS Chinese Pinyin IME — device acceptance, 2026-09-04

## Result

Installed and selected `org.westlake.pinyin/ServiceExtAbility` on the attached
OHOS board `5cdbf6af00000000000000000923012c`. Chinese composing text, phrase
candidates, committing, deletion, cancellation and search submission work in
the unchanged Toutiao APK through the existing candidate04 OHOS IME boundary.
An independent native ArkUI TextInput also accepts Chinese composition/commit.
Kika remains installed and can be selected again through the system picker.

This is a basic offline Pinyin keyboard, not a production-complete IME or a
native-performance-parity result. The keyboard itself requests no permissions,
including no Internet or clipboard permission. Toutiao still uses its own
network connection for suggestions/search.

## Implementation

New source and all raw evidence are in
[chinese-ime-20260904](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/README.md).
The decoder and dictionary are unchanged inputs from the Apache-2.0
[OHOS input-method port](https://gitee.com/ohos_port/applications_inputmethod),
pinned to commit `cd27df54150e5d96d86757dc7315ead69d469d9b`. Its AOSP Pinyin
decoder is wrapped by a new bounded N-API interface; the old upstream UI and
N-API wrapper are not used. The new UI is therefore not an unchanged upstream
application. License and attribution are included in the HAP.

- [Controller](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/ServiceExtAbility.js): OHOS input-method extension and fixed keyboard panel, `setPreviewText` / `finishTextPreview`, candidate commits, serial input operations and focus-generation cancellation. Editors without preview support use an IME-side composition plus `insertText` fallback.
- [Keyboard](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/Keyboard.js): QWERTY Pinyin, five visible candidates with paging, Chinese/English and number modes, backspace, cancel, hide and editor action.
- [Decoder boundary](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/pinyin_napi.cpp): maximum 64 Pinyin bytes, 32 output characters, validated arguments and bounded candidate buffers. Dictionary/user files are in the keyboard's own sandbox.
- [Build](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/build.py): SDK API-20 bytecode/native compiler and public development signing keys; normal APL and no requested permissions. This is a development-signed package, not a production signing setup.

The first package exposed a real resource-format mismatch: SDK 6.1 RestoolV2
indices were rejected by the board's resource manager (`Parse ResHeader failed`,
then dictionary error 9001005). Using the workspace Restool 4.105 V1 index
resolved both dictionary loading and the IME label. No OS resource-manager
patch or dictionary-path bypass was applied. Old failed-launch logs are retained
and distinguished by PID 31562; the accepted final IME process is PID 32152.

No Android framework, native bridge, ART, networking implementation or Toutiao
APK was changed for this addition. Candidate04's existing composing callbacks
were sufficient: real preview and finish events execute Android `runOp(4)` and
`runOp(5)` on an `EditableInputConnection` for Toutiao's focused search editor.

## Live acceptance

All text entry below used real screen taps (`uinput -T -c`) on the visible
OHOS keyboard. The Android mailbox was used only to dump view trees, never to
insert text. Native TextInput callbacks and Android view-tree values provide
independent checks in addition to screenshots.

| Check | Observed result | Evidence in artifact directory |
|---|---|---|
| Native preview | `nihao` in the native field, `你好` candidate | `native-nihao-preview.jpeg` |
| Native consecutive commits | `你好`, then `你好中国` | `native-hello-china.log`, `.jpeg` |
| Native committed-character deletion | `你好中国` → `你好中` | `native-delete-chinese.log` |
| Final-build native rerun | `nihao` → `你好` | `native-final-build.log`, `.jpeg` |
| Toutiao composition and commit | `nihao` → `你好` | `toutiao-nihao-preview.tree`, `toutiao-nihao-commit.tree` |
| Toutiao consecutive phrase commits | `你好` → `你好中国` | `toutiao-hello-china.tree`, `.jpeg` |
| Toutiao committed-character deletion | `你好中国` → `你好中` | `toutiao-delete-chinese.tree` |
| Composition backspace and cancel | `你好中nihao` → `你好中niha` → `你好中` | `toutiao-composition-backspace.tree`, `toutiao-composition-cancel.tree` |
| Alternate candidate | second candidate of `shi` commits `时` | `toutiao-shi-candidates.jpeg`, `toutiao-alternate-candidate.tree` |
| Chinese query and suggestions | `xinwen` → `新闻`, Chinese suggestions displayed | `toutiao-news-query.tree`, `.jpeg` |
| Keyboard Search action | query retained as `新闻`, search screen entered, keyboard hidden | `toutiao-news-submit.tree`, `.jpeg`, `toutiao-submit-callbacks.log` |

The search-results body remained blank after submission, consistent with the
previously observed WebView gap. Search submission is accepted; **results loading
is not**. No timing is inferred from these shell-driven taps. Raw device logs
show September 5 while the workspace run date is September 4; clocks were not
changed or treated as synchronized benchmark clocks.

Automated checks passed: three host decoder tests, six controller-contract test
groups, and five captured-evidence/package acceptance tests. Controller mocks
cover rapid serialization, focus changes, cancel, backspace, preview fallback,
candidate commit and editor action. They do not establish device-level timing or
exhaustive cross-editor race correctness.

## Preservation and handoff

Final installed HAP SHA-256:
`7ce3537f212395f89b53cbff0c544530d259903b76d4b8f94448f98badf15dd0`.
Device installed-file hash matches [the manifest](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/out/manifest.json).

[after-install.log](/home/dspfac/bridge-build-arm64/chinese-ime-20260904/after-install.log)
verifies the current IME plus unchanged MD5s:

- Kika HAP: `e84e57746adaa181fe04117d75f3e33c`.
- Toutiao APK: `cc99caaaa3baffbb100118753fa1d130`.
- Candidate04 ART: `c8b5e7352662109ddcb9ca3b97020894`.
- Main bridge: `b58ed9c01a93df2601e9c0671afdf2df`.
- Accepted v142 ART: `20e48b90b6b01468f155ed7c48097e70`.

Host 8653, candidate parent 22507 / Toutiao 22641 and other parents
4778/9623/2744 remain alive. Toutiao continues using the already isolated copied
data directory (inode 50270), not the original directory (inode 49251). No
original app data was cleared, no system service stopped and no baseline runtime
overwritten. The device is left in Toutiao with Pinyin enabled.

To switch back, launch the Westlake Pinyin app, tap **选择输入法 / Choose keyboard**,
and choose `kikakeyboard` in the system picker. Both keyboards remain installed.

## Remaining scope

Full cursor/selection replacement, emoji/grapheme deletion, hardware keyboards,
rotation, accessibility, full candidate paging/partial-phrase coverage and
persistent learning are not comprehensively accepted. The small UI is a bring-up
keyboard, not a replacement for a mature commercial IME. The diagnostic native
test field logs its test text; the keyboard controller logs counts, not text.

The existing Android insets/`adjustResize`, IME enumeration/session facade,
off-main attach snapshot ordering and WebView issues remain. This run does not
rerun networking coverage or the OnePlus 6 performance comparison, and makes no
claim that page loading or responsiveness matches native performance.
