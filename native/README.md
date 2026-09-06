# Android support sources for OpenHarmony

`tools/build_android_native.py` builds the translation units declared in
`libraries.json`. Use the manifest's `native-support` profile to verify all
source and toolchain locks before building. The direct tool is also available
for development and host-tool libraries (`--target host --library libandroidfw.so`).

The OHOS target uses the platform libc++ ABI (`std::__h`), musl and the running
Linux/OHOS system. Android shared memory maps to Linux memfd with shared mappings
and irreversible future-write seals. No Bionic/glibc impersonation macro is used.
The upstream POSIX logging/property/trace variants are selected explicitly;
system property propagation and tracing into OHOS services are separate bridge work.

Source-built outputs include logging, base/cutils/utils, JNI helpers, zlib, ZIP,
Expat, PNG, FreeType, ICU 75, HarfBuzz, Minikin and common Android asset/resource
handling. ICU embeds its versioned Unicode locale data asset from the locked ICU
repository. `libandroidfw` currently builds its common/tool source set;
CursorWindow parcel serialization and Android backup integration still require
the bridge Parcel provider. This library alone is not the full framework.

Conscrypt's JNI uses the matching Android 15 BoringSSL source snapshot. The
portable crypto implementation is built into private static archives and linked
into `libjavacrypto.so` with archive symbols hidden. It does not depend on a
possibly incompatible OpenSSL library from the host OS. This is not a FIPS
validation or a performance-parity claim.

Compilation and linking fail on errors. Inputs, consumed project headers,
explicit objects, compiler/runtime hashes, commands and outputs are recorded.
`-z defs` is required for shared libraries. There is no object-directory scan,
ignored compile failure, undefined-symbol fallback or device-binary input.

`native/tests/support_probe.cpp` exercises shared-memory behavior across a fork,
write seals, ZIP extraction/error paths, XML namespaces/malformed rejection,
reference PNG pixels, Unicode word boundaries, Chinese font rasterization and
shaping, and the unchanged Toutiao APK's resource table. The manifest's
`probe_android_native.py` compiles it and hash-checks all transfers into a fresh
device stage. Passing this probe is distinct from launching Toutiao's UI.
