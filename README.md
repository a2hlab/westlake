# Westlake: AOSP15 / OHOS6.1 legacy reproduction workstream

Private source repository under `a2hlab`, separate from the organization's
Android16 platform. Canonical bridge sources are under `framework/`.
`historical/` preserves the prior experiment scripts and reports; those scripts
are not a portable full-build entrypoint and may contain epoch-specific PIDs.

Portable targets live in `tools/`. `build_native.py` rebuilds the four public-SDK
networking, startup-order, IME and TLS boundary libraries from source, with no
candidate runtime, frozen object, boot image or generated Ninja prerequisites.
Use the companion `a2hlab/manifest` lock/bootstrap for SDK/source verification.
The tool requires a new output directory and fails on every compile/link error.

No claim of full stack reproducibility or Toutiao/native parity is made merely
by publishing this repo. The full-build and live acceptance gates remain in the
manifest workstream status. Candidate13's real Toutiao IME attach regression
remains unaccepted; candidate05's recorded baseline is preserved separately.

Component license headers and third-party attribution apply. `third_party/jni`
contains the Apache-2.0 AOSP libnativehelper JNI header, unchanged from the
recorded source input. No APK, signing key or private app data is distributed.
