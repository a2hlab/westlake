/*
 * Android NDK AssetManager companion for validated legacy bridge builds.
 *
 * libandroid.so owns the public AAssetManager/AAsset C ABI.  The framework
 * bridge owns the Java AssetManager, AssetManager2 and Asset C++ objects.  A
 * legacy bridge already exports the two conversion helpers below but predates
 * the narrow wl_AAsset* forwarding ABI.  This DSO supplies only that ABI and
 * is loaded lazily by libandroid_webview_shim.c after the framework bridge is
 * initialized.  It contains no app-specific logic.
 */

#include <inttypes.h>
#include <sys/types.h>

#include <memory>
#include <new>
#include <utility>

#include "android_runtime/android_util_AssetManager.h"
#include "androidfw/Asset.h"
#include "androidfw/AssetDir.h"
#include "androidfw/AssetManager2.h"
#include "androidfw/MutexGuard.h"
#include "jni.h"

namespace android {

struct WlNdkAssetDir {
  std::unique_ptr<AssetDir> directory;
  size_t next_index = 0;
};

extern "C" __attribute__((visibility("default"))) void* wl_AAssetManager_fromJava(
    JNIEnv* env, jobject java_asset_manager) {
  if (env == nullptr || java_asset_manager == nullptr) return nullptr;
  return NdkAssetManagerForJavaObject(env, java_asset_manager);
}

extern "C" __attribute__((visibility("default"))) void* wl_AAssetManager_open(
    void* manager, const char* filename, int mode) {
  if (manager == nullptr || filename == nullptr ||
      mode < Asset::ACCESS_UNKNOWN || mode > Asset::ACCESS_BUFFER) {
    return nullptr;
  }
  Guarded<AssetManager2>* guarded = AssetManagerForNdkAssetManager(
      reinterpret_cast<AAssetManager*>(manager));
  if (guarded == nullptr) return nullptr;
  ScopedLock<AssetManager2> asset_manager(*guarded);
  std::unique_ptr<Asset> asset = asset_manager->Open(
      filename, static_cast<Asset::AccessMode>(mode));
  return asset.release();
}

extern "C" __attribute__((visibility("default"))) void* wl_AAssetManager_openDir(
    void* manager, const char* directory_name) {
  if (manager == nullptr || directory_name == nullptr) return nullptr;
  Guarded<AssetManager2>* guarded = AssetManagerForNdkAssetManager(
      reinterpret_cast<AAssetManager*>(manager));
  if (guarded == nullptr) return nullptr;
  ScopedLock<AssetManager2> asset_manager(*guarded);
  std::unique_ptr<AssetDir> directory = asset_manager->OpenDir(directory_name);
  if (directory == nullptr) return nullptr;
  return new (std::nothrow) WlNdkAssetDir{std::move(directory), 0};
}

extern "C" __attribute__((visibility("default"))) const char*
wl_AAssetDir_getNextFileName(void* directory_handle) {
  WlNdkAssetDir* state = reinterpret_cast<WlNdkAssetDir*>(directory_handle);
  if (state == nullptr || state->directory == nullptr ||
      state->next_index >= state->directory->getFileCount()) {
    return nullptr;
  }
  return state->directory->getFileName(state->next_index++).c_str();
}

extern "C" __attribute__((visibility("default"))) void wl_AAssetDir_rewind(
    void* directory_handle) {
  WlNdkAssetDir* state = reinterpret_cast<WlNdkAssetDir*>(directory_handle);
  if (state != nullptr) state->next_index = 0;
}

extern "C" __attribute__((visibility("default"))) void wl_AAssetDir_close(
    void* directory_handle) {
  delete reinterpret_cast<WlNdkAssetDir*>(directory_handle);
}

extern "C" __attribute__((visibility("default"))) int wl_AAsset_read(
    void* asset_handle, void* buffer, size_t count) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  if (asset == nullptr || (buffer == nullptr && count != 0)) return -1;
  const ssize_t result = asset->read(buffer, count);
  return result > INT32_MAX ? INT32_MAX : static_cast<int>(result);
}

extern "C" __attribute__((visibility("default"))) int64_t wl_AAsset_seek64(
    void* asset_handle, int64_t offset, int whence) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  return asset == nullptr ? -1 : static_cast<int64_t>(asset->seek(offset, whence));
}

extern "C" __attribute__((visibility("default"))) void wl_AAsset_close(
    void* asset_handle) {
  delete reinterpret_cast<Asset*>(asset_handle);
}

extern "C" __attribute__((visibility("default"))) const void* wl_AAsset_getBuffer(
    void* asset_handle) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  return asset == nullptr ? nullptr : asset->getBuffer(false /* aligned */);
}

extern "C" __attribute__((visibility("default"))) int64_t wl_AAsset_getLength64(
    void* asset_handle) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  return asset == nullptr ? 0 : static_cast<int64_t>(asset->getLength());
}

extern "C" __attribute__((visibility("default"))) int64_t
wl_AAsset_getRemainingLength64(void* asset_handle) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  return asset == nullptr ? 0 : static_cast<int64_t>(asset->getRemainingLength());
}

extern "C" __attribute__((visibility("default"))) int wl_AAsset_openFileDescriptor64(
    void* asset_handle, int64_t* out_start, int64_t* out_length) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  if (asset == nullptr || out_start == nullptr || out_length == nullptr) return -1;
  off64_t start = 0;
  off64_t length = 0;
  const int fd = asset->openFileDescriptor(&start, &length);
  if (fd >= 0) {
    *out_start = static_cast<int64_t>(start);
    *out_length = static_cast<int64_t>(length);
  }
  return fd;
}

extern "C" __attribute__((visibility("default"))) int wl_AAsset_isAllocated(
    void* asset_handle) {
  Asset* asset = reinterpret_cast<Asset*>(asset_handle);
  return asset != nullptr && asset->isAllocated() ? 1 : 0;
}

}  // namespace android
