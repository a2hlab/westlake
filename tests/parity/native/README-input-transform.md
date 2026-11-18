# Private MotionEvent transform contract

`input_transform_test.cpp` checks the native input model used by Westlake's JNI
binding. It covers local versus raw coordinates, transform composition, history,
multiple pointers, copy behavior, cursor coordinates and source-specific rules.
The former empty `transform()` implementation fails the first translation check.

From the Westlake checkout, with `ANDROID_SOURCE` pointing to the source tree
containing `frameworks-native` and `JDK` pointing to a JDK:

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  '-D__INTRODUCED_IN(x)=' '-D__DEPRECATED_IN(x)=' -D_Nullable= -D_Nonnull= \
  -I framework/android-runtime/include \
  -I "$ANDROID_SOURCE/frameworks-native/include" \
  -I "$JDK/include" -I "$JDK/include/linux" \
  tests/parity/native/input_transform_test.cpp \
  framework/android-runtime/src/android_input_Input_aosp.cpp \
  -o /tmp/input-transform-test
/tmp/input-transform-test
```

The recorded host run passes 40 checks. This does not establish Android input
parity or application/backend acceptance. Transformed JNI batching still converts
incoming coordinates using translation offsets only. Surface-rotation reporting,
Parcel input serialization and several other input APIs remain incomplete.

The private `MotionEvent` layout changes with this implementation. Rebuild every
consumer of `framework/android-runtime/include/input/Input.h` together. The
candidate-67 native build audits compiler dependency files and rebuilds both
consumers; unchanged native objects and link inputs are hash-verified. Do not
replace only the implementation object while retaining an old JNI object that
allocates the prior object size.

See [two-path validation](../../../docs/google-two-path-full-validation.md) for
the original APK, diagnostic, native artifact and board evidence.
