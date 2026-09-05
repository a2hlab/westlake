# Ability-owned OHOS IME candidate

Accepted scope: screen touch, native keyboard visibility, ASCII edits/backspace,
Search action and hide/reopen on the independent control and unchanged Toutiao.
Network controls pass. **Not** acceptance of WebView results, Chinese composition,
IME insets, full Android input compatibility, or native performance parity.

Full report: [OH IME ability repair](/home/dspfac/openharmony/docs/engine/OH-IME-ABILITY-WINDOW-REPAIR-2026-09-04.md).

## Reproduce

Run from this directory. The host HAP is already installed and foreground on
the attached OHOS board; `device.py` pins its HDC serial. Building/signing the
test host again uses `python3 build_host.py` and `python3 sign_host.py`; signing
uses the SDK's public sample keys, not a production identity.

Inspect current PIDs and socket before launching; the launcher refuses a live
candidate socket. It discovers the dedicated host's UID/token/window instead
of hard-coding them. A stale, non-live candidate socket can be removed by the
launcher. Never stop a baseline parent to make room.

```sh
python3 -m unittest -v test_candidate
python3 launch_control.py --candidate candidate04 --app control --run control-new
```

Do not run that launch while candidate04 parent 22507 is still live. To run
Toutiao after a control, archive and stop only that control's recorded parent
and child, then:

```sh
python3 launch_control.py --candidate candidate04 --app toutiao --run toutiao-new
```

Toutiao requires the already-provisioned isolated copy at
`/data/app/el2/100/base/org.westlake.imehost/files/android-toutiao-data`.
The wrapper verifies ownership, creates a private mount namespace and binds
only this copy onto `/data/data/com.ss.android.article.news` inside it. It does
not change the original Android directory's ownership or global mount.
This development harness requires the existing root-enabled board environment.

New source experiments must use a fresh candidate directory:

```sh
CANDIDATE=candidate05 python3 build_candidate.py
CANDIDATE=candidate05 python3 stage_candidate.py
python3 launch_control.py --candidate candidate05 --app control --run control05
```

Build rejects an existing manifest to preserve archived evidence; staging
refuses files mapped by running candidate processes. Each push is SHA-256
checked; the runner independently checks inputs before launch. New builds
require the pinned prior network candidate and the local SDK/toolchain paths
recorded in the scripts. This is not a self-contained distributable SDK.

## Evidence

- `control04-initial.log`: network controls and successful MMI subscription.
- `control04-actions.log`, `control04-edited.tree`, `control04-keyboard.jpeg`:
  real screen taps, normal IMM show, `wle` -> `wl`, Search, hide/reopen.
- `toutiao01-typed.tree`, `toutiao01-typed.jpeg`, `toutiao01-ime-submit.log`:
  real keyboard `openaie` -> `openai`, suggestions and Search submission.
- `toutiao01-results.tree`: the distinct, still-blank WebView results gap.
- `toutiao01-data-isolation.log`, `preserved-baseline.log`,
  `final-device-checks.log`: private data namespace and preserved artifacts.
- `device-commands.jsonl`: raw command journal; some early diagnostic/error
  captures are intentionally retained. Prefer the final candidate04 evidence.

The native input helper is built with device-matching EventHandler/MMI headers
and OHOS `std::__h`. The Z helper uses a single exact nonvirtual symbol alias.
Both interposers are opt-in and preserve the pinned main bridge. The source
integration guards its pinned-symbol alias behind `WESTLAKE_PINNED_INPUT_ENTRY`
so a future aggregate source build does not define `subscribeMmi` twice.

Live handoff: host 8653, parent 22507, Toutiao child 22641. PIDs are observations,
not persistent configuration: verify executable paths before stopping them.
The previous default/review/network parent processes and accepted v142 remain
untouched. No OnePlus actions were taken during this repair.
