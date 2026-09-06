# Window power-policy source headers

This is the 75-file transitive OH source-header closure observed while compiling
the candidate26 window helper. `inventory.json` pins every file and the include
search order. Original paths and license notices are retained; compiler/SDK
headers are supplied by the manifest's separately locked toolchains.

The closure covers the IPC/session API and actual bridge class layout. It does
not provide the OH system services, firmware, linker providers, or a complete
OH source manifest. `tools/build_window_policy.py` can compile the object with
repository source plus those toolchains. Linking additionally requires the two
exact board DSOs in `tests/parity/window-providers.lock.json`. Their identities
are inputs, not proof that an arbitrary device has the matching platform ABI.
