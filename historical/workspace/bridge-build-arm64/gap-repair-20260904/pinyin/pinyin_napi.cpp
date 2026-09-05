// Bounded N-API interface to the unmodified Apache-2.0 AOSP Pinyin decoder.
// Single ArkUI JS thread owns the decoder; no cloud/network API is present.
#include <napi/native_api.h>
#include "pinyinime.h"
#include <algorithm>
#include <cstring>
#include <string>
using namespace ime_pinyin;
namespace {
bool opened = false;
size_t count = 0;
napi_value fail(napi_env env, const char* message) {
    napi_throw_error(env, nullptr, message); return nullptr;
}
bool textArg(napi_env env, napi_value value, std::string& out, size_t max) {
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok || length > max) return false;
    out.resize(length + 1);
    if (napi_get_value_string_utf8(env, value, out.data(), out.size(), &length) != napi_ok) return false;
    out.resize(length);
    return out.find('\0') == std::string::npos;
}
void prop(napi_env env, napi_value object, const char* key, napi_value value) {
    napi_set_named_property(env, object, key, value);
}
napi_value snapshot(napi_env env) {
    napi_value result, items, value;
    napi_create_object(env, &result); napi_create_array(env, &items);
    for (size_t i = 0; i < std::min(count, size_t(60)); ++i) {
        char16 buffer[128] = {};
        if (!im_get_candidate(i, buffer, 128)) break;
        size_t length = 0; while (length < 127 && buffer[length]) ++length;
        napi_create_string_utf16(env, reinterpret_cast<char16_t*>(buffer), length, &value);
        napi_set_element(env, items, i, value);
    }
    prop(env, result, "candidates", items);
    const uint16* starts = nullptr;
    const size_t syllables = im_get_spl_start_pos(starts);
    const size_t fixed = im_get_fixed_len();
    const size_t consumed = starts && fixed <= syllables ? starts[fixed] : 0;
    napi_create_uint32(env, fixed, &value); prop(env, result, "fixed", value);
    napi_create_uint32(env, consumed, &value); prop(env, result, "consumed", value);
    size_t length = 0; const char* raw = im_get_sps_str(&length);
    napi_create_string_utf8(env, raw ? raw : "", raw ? length : 0, &value); prop(env, result, "raw", value);
    napi_get_boolean(env, count == 1 && fixed > 0 && fixed == syllables && consumed == length, &value);
    prop(env, result, "done", value);
    return result;
}
napi_value open(napi_env env, napi_callback_info info) {
    size_t argc = 2; napi_value args[2]; std::string dict, user;
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc != 2 || !textArg(env, args[0], dict, 4095) || !textArg(env, args[1], user, 4095))
        return fail(env, "open requires bounded dictionary and user paths");
    if (opened) im_close_decoder();
    opened = im_open_decoder(dict.c_str(), user.c_str());
    if (!opened) return fail(env, "Pinyin dictionary could not be opened");
    im_set_max_lens(64, 32); count = 0;
    napi_value value; napi_get_boolean(env, true, &value); return value;
}
napi_value search(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value arg; std::string raw;
    napi_get_cb_info(env, info, &argc, &arg, nullptr, nullptr);
    if (!opened || argc != 1 || !textArg(env, arg, raw, 64)) return fail(env, "invalid Pinyin search");
    for (char c : raw) if ((c < 'a' || c > 'z') && c != '\'') return fail(env, "Pinyin accepts a-z and apostrophe");
    // The upstream decoder retains compatible fixed prefixes on incremental search.
    count = im_search(raw.c_str(), raw.size());
    return snapshot(env);
}
napi_value choose(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value arg; uint32_t index = 0;
    napi_get_cb_info(env, info, &argc, &arg, nullptr, nullptr);
    if (!opened || argc != 1 || napi_get_value_uint32(env, arg, &index) != napi_ok || index >= count)
        return fail(env, "candidate index out of range");
    count = im_choose(index); return snapshot(env);
}
napi_value reset(napi_env env, napi_callback_info) {
    if (opened) im_reset_search(); count = 0;
    napi_value value; napi_get_undefined(env, &value); return value;
}
napi_value flush(napi_env env, napi_callback_info) {
    if (opened) im_flush_cache();
    napi_value value; napi_get_undefined(env, &value); return value;
}
void cleanup(void*) {
    if (opened) { im_flush_cache(); im_close_decoder(); opened = false; }
    count = 0;
}
napi_value close(napi_env env, napi_callback_info) {
    cleanup(nullptr);
    napi_value value; napi_get_undefined(env, &value); return value;
}
napi_value init(napi_env env, napi_value exports) {
    napi_property_descriptor methods[] = {
        {"open", nullptr, open, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"search", nullptr, search, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"choose", nullptr, choose, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"reset", nullptr, reset, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"flush", nullptr, flush, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"close", nullptr, close, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    napi_define_properties(env, exports, sizeof(methods)/sizeof(methods[0]), methods);
    napi_add_env_cleanup_hook(env, cleanup, nullptr);
    return exports;
}
napi_module module = {1, 0, nullptr, init, "westlake_pinyin", nullptr, {nullptr}};
}
extern "C" __attribute__((constructor)) void registerPinyin() { napi_module_register(&module); }
