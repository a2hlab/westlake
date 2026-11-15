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

## OH notification client headers (`bridge15-sources.json`)

`framework/core/jni/oh_notification_client.cpp` calls OH's notification client
(`libans_innerkits.z.so`, declared in `oh61-firmware-abi.json`). It compiles with
two include directories of its own (`includes_by_source`), which no other bridge
source sees, because their names (`pixel_map.h`, `want_agent.h`) would shadow
the headers the window and surface sources use:

- `native/include/oh-notification-standin`: declarations only. The
  notification headers hold pixel maps, want agents and button resources
  through `std::shared_ptr` and need no definitions. `wl_notification_helper.h`
  declares the two `NotificationHelper` members the bridge calls, as
  `notification_helper.h` declares them; the real header includes headers
  OpenHarmony generates from IDL.
- `android-source/oh61-notification-headers/include`: unmodified copies of
  `interfaces/inner_api/*.h` and `frameworks/core/common/include/*.h` from
  [notification_distributed_notification_service](https://github.com/openharmony/notification_distributed_notification_service) at tag
  `OpenHarmony-v6.1-Release` (commit `0836199f2bcd106295d125943aaf45c91f454fc3`).
