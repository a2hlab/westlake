# Native networking from descriptor-loaded APK code

`network_apk_caller_test.c` loads the real network adapter and a caller DSO. It
checks the host `addrinfo` layout for native callers, Bionic layout for an APK
loaded through `/proc/self/fd/N`, native layout after that descriptor is reused,
and the existing explicit-library allowlist. Each path resolves only the
numeric loopback address and frees the result through the same adapter. No
external DNS, account or Google service is needed.

Run from the Westlake repository, using a new output directory:

```sh
out=/absolute/path/in/your/workspace/network-apk-test
mkdir -p "$out"
cc -shared -fPIC -O2 -Wall -Wextra -Werror \
  framework/appspawn-x/bionic_compat/src/android_native_network_compat.c \
  -ldl -pthread -o "$out/network.so"
cc -shared -fPIC -O2 -Wall -Wextra -Werror \
  tests/parity/native/network_apk_caller.c -o "$out/native.so"
cp "$out/native.so" "$out/code.apk"
cc -O2 -Wall -Wextra -Werror tests/parity/native/network_apk_caller_test.c \
  -ldl -o "$out/test"
"$out/test" "$out/network.so" "$out/native.so" "$out/code.apk"
```

The `.apk` test file deliberately contains the caller ELF directly. This isolates
loader-label classification from ZIP loading and signer verification; it does
not test APK authenticity or security isolation. The original Gmail experiment
separately supplies a verified GMS ZIP member loaded through an APK descriptor.
Do not use a host output directory under `/data/data`, `/data/user` or
`/data/user_de`: those paths intentionally classify app-written libraries as
Android callers. The expected result is `NETWORK_APK_CALLER_PASS 24` (one extra
check may run if the descriptor must be reassigned with `dup2`).

The same sources build with the OH SDK compiler and run on the board. Before the
fix, the test fails check 12 when the APK caller interprets the returned address
using Bionic's member order. The shared `bionic_apk_path_test.c` additionally
covers malformed labels, closed descriptors, descriptor reuse and errno
preservation. The classification selects ABI translation; package and signer
authorization remains in the independent spawner and Binder checks.
