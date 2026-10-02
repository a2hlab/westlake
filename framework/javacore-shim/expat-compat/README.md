# expat-compat

Stand-in headers that let AOSP's unmodified
`libcore/luni/src/main/native/org_apache_harmony_xml_ExpatParser.cpp` build against the OH
sysroot. The staged runtime ships `org.apache.harmony.xml.ExpatParser` (SAX) but not its natives.
Each header supplies only what that file uses:

- `android-base/stringprintf.h`: `android::base::StringPrintf`.
- `unicode/char16ptr.h`, `unicode/ustring.h`: `icu::Char16Ptr` and `u_strFromUTF8WithSub`
  (a local UTF-8 decoder with U+FFFD substitution, so the library needs no ICU).
- `android/log.h`: `__android_log_print` and `__android_log_assert` on stderr, for JNIHelp.h.

libcore's own `JniConstants.h` and `JniException.h` are used as-is; `../expat_natives.cpp` defines
the two functions they declare.
