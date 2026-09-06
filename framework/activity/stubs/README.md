# framework/activity/stubs

Compile-only stand-ins. **Never shipped** — `build_adapter_runtime` dexes the
classes under `java/` and nothing from here.

`ActivityManagerRouting` extends `ActivityManagerAdapter`, which extends
`IActivityManager.Stub`, which needs the adapter's whole `framework.jar` on the
classpath. That jar is produced by the same pipeline, which is the cold-start
dependency cycle `full-build-gaps.json` records. These five files declare just
the members the routing subclass touches, so it can be compiled and dex'd on its
own while that cycle stands.

Supply the real `framework.jar` on the classpath instead and these become
unnecessary; they are a scaffold for a gap, not part of the design.
