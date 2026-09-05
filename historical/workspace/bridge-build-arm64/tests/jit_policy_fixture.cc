// Host contract fixture. The three policy/sampling function bodies are imported
// verbatim from runtime/jit/jit.cc by test_jit_review_regressions.py. Runtime and
// queue doubles model admission/rejection, not ART compilation or GC safety.
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#define DCHECK(x) assert(x)
namespace mirror { struct Class {}; }
namespace dex { constexpr uint32_t kDexNoIndex = UINT32_MAX; }
struct ClassRef { mirror::Class* value; mirror::Class* Ptr() { return value; } };
struct ArtMethod {
  bool app = false, native = false, safe = false;
  uint32_t index = 1;
  mirror::Class klass;
  bool IsRuntimeMethod() { return false; }
  void* GetClassLoader() { return app ? this : nullptr; }
  bool IsNative() { return native; }
  ClassRef GetDeclaringClass() { return {&klass}; }
  uint32_t GetDexMethodIndex() { return index; }
};
struct Thread {};
bool app_only = true, boot_parser = true, boot_native = true;
bool WestlakeJitAppOnly() { return app_only; }
bool WestlakeJitBootParser() { return boot_parser; }
bool WestlakeJitBootNativeStubs() { return boot_native; }
bool WestlakeIsSafeBootNativeStub(ArtMethod* m) { return m->native && m->safe; }
bool WestlakeIsSafeBootJitMethod(ArtMethod* m) { return !m->native && m->safe; }
struct Jit {
  void* thread_pool_ = this;
  bool first_use = false, enabled = true, stopped = false;
  unsigned attempts = 0, accepted = 0;
  bool JitAtFirstUse() { return first_use; }
  bool UseJitCompilation() { return enabled; }
  static bool IsMethodCompilationAllowed(ArtMethod*);
  static bool IsOsrAllowed(ArtMethod*);
  bool MaybeEnqueueWestlakeSafeBootCompilation(ArtMethod*, Thread*);
  void MaybeEnqueueCompilation(ArtMethod*, Thread*) {
    ++attempts;
    if (!stopped) ++accepted;
  }
};
#include "policy_bodies.inc"
int main() {
  ArtMethod app, boot, native;
  app.app = true;
  native.native = true;
  for (bool restricted : {false, true}) {
    app_only = restricted;
    assert(Jit::IsMethodCompilationAllowed(&app));
    assert(Jit::IsOsrAllowed(&app));
    for (bool safe : {false, true}) {
      boot.safe = native.safe = safe;
      for (bool allow : {false, true}) {
        boot_parser = boot_native = allow;
        assert(Jit::IsMethodCompilationAllowed(&boot) == (!restricted || (safe && allow)));
        assert(Jit::IsMethodCompilationAllowed(&native) == (!restricted || (safe && allow)));
        assert(Jit::IsOsrAllowed(&boot) == !restricted);
      }
    }
  }
  app_only = boot_parser = boot_native = true;
  native.safe = true;
  Jit jit;
  Thread thread;
  // The early attempt is rejected. Every later sample must reach normal ART
  // hotness sampling, not be swallowed forever as in the old requested table.
  jit.stopped = true;
  assert(jit.MaybeEnqueueWestlakeSafeBootCompilation(&native, &thread));
  assert(jit.attempts == 1 && jit.accepted == 0);
  jit.stopped = false;
  for (unsigned i = 0; i < 20; ++i) {
    assert(!jit.MaybeEnqueueWestlakeSafeBootCompilation(&native, &thread));
  }
  // A simulated compile failure or invalidation likewise must not suppress
  // subsequent ordinary samples. The queue itself remains upstream-owned.
  jit.MaybeEnqueueCompilation(&native, &thread);
  assert(jit.accepted == 1);
  assert(!jit.MaybeEnqueueWestlakeSafeBootCompilation(&native, &thread));
  native.index = 2;  // Same address, new method identity: recompute admission.
  native.safe = false;
  assert(jit.MaybeEnqueueWestlakeSafeBootCompilation(&native, &thread));
  assert(jit.attempts == 2);
  jit.thread_pool_ = nullptr;
  assert(!jit.MaybeEnqueueWestlakeSafeBootCompilation(&native, &thread));
}
