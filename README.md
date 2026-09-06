# Westlake: AOSP15 / OHOS6.1 legacy reproduction workstream

Private source repository under `a2hlab`, separate from the organization's
Android16 platform. Canonical bridge sources are under `framework/`.
`historical/` preserves the prior experiment scripts and reports; those scripts
are not a portable full-build entrypoint and may contain epoch-specific PIDs.

Portable targets live in `tools/`. `build_native.py` rebuilds six public-SDK
networking, startup-order, IME, TLS, process-stream and CPU-time boundary libraries from source, with no
candidate runtime, frozen object, boot image or generated Ninja prerequisites.
Use the companion `a2hlab/manifest` lock/bootstrap for SDK/source verification.
The tool requires a new output directory and fails on every compile/link error.

The [Android-reference reproduction guide](docs/parity/REPRODUCE.md) covers the
generic JIT/cache correction, native worker fix, CPU-time API and window power
policy. It includes deterministic control APK builds, stock Android/OH native
probes, the exact window header closure and benchmark sample verification.
Historical candidate13 failures remain in the earlier evidence; candidate26/v27
passed the isolated device checks described in the follow-up report.

Full source-stack reproduction and general native performance parity remain
open in the manifest workstream. The repository-built components and the
historical device-validated payload have separately recorded provenance.

Component license headers and third-party attribution apply. `third_party/jni`
contains the Apache-2.0 AOSP libnativehelper JNI header, unchanged from the
recorded source input. No APK, signing key or private app data is distributed.
