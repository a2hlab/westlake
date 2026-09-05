#
# libwlsqlite.so -- SQLite engine plus the android.database JNI boundary.
#
# The adapter's framework.jar carries AOSP's android.database.sqlite Java layer,
# but nothing implements its natives, so the first openOrCreateDatabase() dies
# with UnsatisfiedLinkError and takes Application start-up with it. This target
# supplies those natives: the SQLite engine itself plus AOSP's
# android_database_* JNI, compiled against the small compatibility layer in
# compat/ that stands in for the platform headers this tree does not have.
#
# Two toolchains are supported, and which one you want is not a matter of taste:
#
#   WL_SQLITE_ABI=bionic  (default)
#       For loading into the adapter's *app* process. That process is served by
#       the Android-side linker namespace, so the library must be bionic-linked.
#       Point CMAKE_TOOLCHAIN_FILE at an Android NDK's android.toolchain.cmake.
#
#   WL_SQLITE_ABI=ohos
#       For building against the pinned OpenHarmony native SDK (musl). This is
#       what the manifest `native` profile can verify offline, and it proves the
#       source closes -- every symbol resolves, no platform header is missing.
#       It is NOT the artifact to deploy into the app process.
#
# Both paths take every input from this source tree or from a cache variable.
# There are no absolute paths here by design: see the blueprint's §5.3 gate.
#
# Usage (OHOS verification build):
#   cmake -S framework/sqlite -B <build> \
#         -DWL_SQLITE_ABI=ohos -DOH_NATIVE_SDK=<ohos-sdk/native>
#   cmake --build <build>
#
# Usage (deployable bionic build):
#   cmake -S framework/sqlite -B <build> \
#         -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30
#   cmake --build <build>
#
cmake_minimum_required(VERSION 3.16)
project(wlsqlite C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(WL_SQLITE_ABI "bionic" CACHE STRING "Target C library: bionic (deployable) or ohos (verification)")
set_property(CACHE WL_SQLITE_ABI PROPERTY STRINGS bionic ohos)
set(OH_NATIVE_SDK "" CACHE PATH "OpenHarmony native SDK root (only for WL_SQLITE_ABI=ohos)")

# Third-party SQLite lives in the repository, not in a download step.
set(WL_SQLITE_AMALGAMATION "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sqlite"
    CACHE PATH "Directory holding the vendored sqlite3.c/sqlite3.h amalgamation")
if(NOT EXISTS "${WL_SQLITE_AMALGAMATION}/sqlite3.c")
    message(FATAL_ERROR
        "sqlite3.c not found under ${WL_SQLITE_AMALGAMATION}. "
        "It is vendored at third_party/sqlite; see its UPSTREAM.json.")
endif()

if(WL_SQLITE_ABI STREQUAL "ohos")
    if(NOT OH_NATIVE_SDK)
        message(FATAL_ERROR "WL_SQLITE_ABI=ohos requires -DOH_NATIVE_SDK=<ohos-sdk/native>")
    endif()
    add_compile_options(--target=aarch64-linux-ohos "--sysroot=${OH_NATIVE_SDK}/sysroot")
    add_link_options(--target=aarch64-linux-ohos "--sysroot=${OH_NATIVE_SDK}/sysroot")
endif()

#
# SQLite configuration.
#
# Mostly AOSP's own set (external/sqlite/dist/Android.bp), because the Java layer
# driving this is AOSP's SQLiteConnection.java out of the adapter's framework.jar
# and it assumes those defaults. SQLITE_TEMP_STORE=3 in particular: there is no
# writable /tmp in this sandbox for a file-backed temp store.
#
# THREADSAFE deliberately differs from AOSP. AOSP builds with =2 (multi-thread)
# and relies on SQLiteConnectionPool to guarantee one thread per connection. We
# use =1 (serialized) so SQLite locks internally regardless, because the Java
# side here is the adapter's framework rather than stock AOSP, and a threading
# assumption that turned out not to hold would surface as intermittent database
# corruption -- among the worst failure modes to diagnose on a remote board. The
# extra mutex traffic is irrelevant next to a runtime forced to -Xint.
#
set(WL_SQLITE_DEFINES
    SQLITE_THREADSAFE=1
    SQLITE_TEMP_STORE=3
    SQLITE_DEFAULT_FILE_PERMISSIONS=0600
    SQLITE_DEFAULT_AUTOVACUUM=1
    SQLITE_DEFAULT_JOURNAL_SIZE_LIMIT=1048576
    SQLITE_ENABLE_MEMORY_MANAGEMENT=1
    SQLITE_SECURE_DELETE
    SQLITE_ENABLE_FTS3
    SQLITE_ENABLE_FTS3_BACKWARDS
    SQLITE_ENABLE_FTS4
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_BUILTIN_TEST
    SQLITE_OMIT_COMPILEOPTION_DIAGS
    SQLITE_HAVE_ISNAN
    HAVE_USLEEP=1
    SQLITE_OS_UNIX=1
)

add_library(wlsqlite SHARED
    "${WL_SQLITE_AMALGAMATION}/sqlite3.c"
    wl_compat.cpp
    wl_jni_onload.cpp
    wl_CursorWindow.cpp
    wl_sqlite3_android.cpp
    android_database_SQLiteCommon.cpp
    android_database_SQLiteConnection.cpp
    android_database_CursorWindow.cpp
)
set_target_properties(wlsqlite PROPERTIES OUTPUT_NAME wlsqlite)

target_compile_definitions(wlsqlite PRIVATE ${WL_SQLITE_DEFINES})
target_include_directories(wlsqlite PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    "${WL_SQLITE_AMALGAMATION}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/jni"
)

# JNI_OnLoad is the only export; everything else stays internal. This library is
# loaded into a process that already has its own idea of what android.database
# means, and leaking a second set of sqlite3_* symbols into the global namespace
# is exactly the collision that defeated the earlier ALooper injections.
target_compile_options(wlsqlite PRIVATE -fvisibility=hidden -O2 -Wall)
target_compile_options(wlsqlite PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>)

# The AOSP sources predate this toolchain's default warning set and are not
# being reformatted here; silence only what they actually trip, so a genuine
# mistake in the compat layer still shows up.
target_compile_options(wlsqlite PRIVATE $<$<COMPILE_LANGUAGE:CXX>:
    -fno-rtti
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-label
    -Wno-unused-function -Wno-writable-strings -Wno-format-security>)

#
# Link rules.
#
# -static-libstdc++: the NDK's libc++_shared.so is a redistributable that would
# have to be shipped with the app, and resolving it inside the sealed child
# namespace would depend on the APK happening to carry a compatible copy. That
# is the same class of namespace dependency that defeated every ALooper
# injection attempt. Linking libc++ statically is only hazardous when C++
# objects cross the library boundary, and none do here: JNI_OnLoad is the sole
# export and everything else is hidden.
#
# The version script is the authoritative fix -- -fvisibility=hidden governs our
# own translation units only, and the static libc++ archive was compiled with
# default visibility, so without it ~1500 std:: symbols leak out of this .so.
# --exclude-libs belts the same braces at archive granularity.
#
# --no-undefined: an unresolved symbol here means a missing compat shim, and we
# want to hear about it at link time rather than at first database open on a
# board we are not allowed to attach to.
#
# No LTO: compat/binder/Parcel.h relies on the Parcel method definitions being
# invisible to wl_CursorWindow.cpp's translation unit, which is what keeps the
# deliberately-null Parcel* inert. LTO would merge those units and let the
# compiler assume `this` is non-null.
#
target_link_options(wlsqlite PRIVATE
    -static-libstdc++
    "-Wl,-soname,libwlsqlite.so"
    "-Wl,--no-undefined"
    "-Wl,--exclude-libs,ALL"
    "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/wlsqlite.map"
    "-Wl,--build-id=sha1"
)
set_target_properties(wlsqlite PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wlsqlite.map")

# Dependencies are kept to libc/libm/libdl deliberately. liblog.so was never
# needed -- compat/utils/Log.h writes to stderr rather than calling
# __android_log_print -- so linking it would have been pure risk.
target_link_libraries(wlsqlite PRIVATE m)
