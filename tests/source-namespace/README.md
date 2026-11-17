# Repeated namespace launch regression

Compile `native/source_app_namespace.c` with the OH SDK and deploy it together with
`repeated-overlay.sh` to an authorized development board. Run the script as root, passing the
launcher binary, an installed test HAP's physical `files` directory and its actual UID.

The fixture uses no APK or account data. It launches the same fixture three times, verifies OH's
SELinux configuration, overlapping runtime overrides, fonts and the Android install-path payload,
and checks that neither input trees nor host policy changed. It removes its fixture after all
private namespaces exit. The pre-fix launcher fails repeated launches because persistent empty
mount targets shadow firmware files, and its Android install-path cover is not reusable.

For existing affected app runtimes, restage runtime inputs from verified artifacts and carry only
package data. Do not copy old `etc`, `fonts` or mount-target directories as runtime inputs.
