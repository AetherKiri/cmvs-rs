#include "cmvs_runtime_provider.h"
#include "cmvs_host.h"
#include "engine_runtime_provider.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

namespace aetherkiri::cmvs {
#if defined(__ANDROID__)
bool InitializeAndroidContext(void* java_vm, void* application_context) {
    return cmvs_host_initialize_android(java_vm, application_context) == 0;
}
#endif
namespace {
struct Runtime {
    CmvsHost* core = cmvs_host_create();
    engine_runtime_host_v1_t host{};
    std::string writable, font, error;
    uint64_t read_serial = 0;
    bool opened = false, native_audio = true;
    ~Runtime() {
        if (opened) cmvs_host_save_preferences(core);
        cmvs_host_destroy(core);
    }
    engine_result_t result(int code) {
        if (code < 0) {
            const char* why = cmvs_host_error(core);
            error = why && *why ? why : "CMVS operation failed";
            return ENGINE_RESULT_INTERNAL_ERROR;
        }
        error.clear();
        return ENGINE_RESULT_OK;
    }
};
Runtime* cast(void* p) { return static_cast<Runtime*>(p); }
bool magic(const std::filesystem::path& path, const char* expected, size_t length) {
    std::ifstream file(path, std::ios::binary);
    char bytes[4]{};
    return file.read(bytes, length) && std::memcmp(bytes, expected, length) == 0;
}
int32_t Probe(void*, const char* path) {
    if (!path) return 0;
    try {
        auto pack = std::filesystem::path(path) / "data" / "pack";
        return magic(pack / "script.cpz", "CPZ", 3) &&
            (magic(pack / "start.ps3", "PS2A", 4) || magic(pack / "start.ps2", "PS2A", 4)) ? 90 : 0;
    } catch (...) { return 0; }
}
engine_result_t Create(void*, const engine_runtime_host_v1_t* host,
                       const engine_create_desc_t* desc, void** output) {
    if (!output) return ENGINE_RESULT_INVALID_ARGUMENT;
    *output = nullptr;
    if (!host || !desc || host->struct_size < sizeof(*host) || desc->struct_size < sizeof(*desc))
        return ENGINE_RESULT_INVALID_ARGUMENT;
    try {
        auto r = std::make_unique<Runtime>();
        if (!r->core) return ENGINE_RESULT_INTERNAL_ERROR;
        r->host = *host;
        r->writable = desc->writable_path_utf8 ? desc->writable_path_utf8 : "";
        *output = r.release();
        return ENGINE_RESULT_OK;
    } catch (...) { return ENGINE_RESULT_INTERNAL_ERROR; }
}
void Destroy(void* p) { delete cast(p); }
engine_result_t Open(void* p, const char* path, const char* startup) {
    if (!p || !path || !*path) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    if (r.opened) return ENGINE_RESULT_INVALID_STATE;
    if (startup && *startup) {
        r.error = "CMVS launches the game directory; a startup script override is not supported";
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    if (r.writable.empty() || r.font.empty()) {
        r.error = "CMVS requires a writable user directory and default_font before open";
        return ENGINE_RESULT_INVALID_STATE;
    }
    try {
        const auto root = std::filesystem::canonical(path).string();
        // Retain the previous per-game namespace for one-time migration and
        // read-only game media. The host normally writes <game>/savedata.
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char b : root) { hash ^= b; hash *= 1099511628211ull; }
        char name[17]; std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
        auto user = (std::filesystem::path(r.writable) / "cmvs" / name).string();
        const auto result = r.result(cmvs_host_open_game_savedata(r.core, root.c_str(), user.c_str(), r.font.c_str(), r.native_audio));
        r.opened = result == ENGINE_RESULT_OK;
        return result;
    } catch (const std::exception& e) { r.error = e.what(); return ENGINE_RESULT_IO_ERROR; }
}
engine_result_t Tick(void* p, uint32_t delta) {
    if (!p || !cast(p)->opened) return ENGINE_RESULT_INVALID_STATE;
    auto& r = *cast(p);
    int result = cmvs_host_tick(r.core, delta);
    if (result == 2) { r.error = "runtime requested termination"; return ENGINE_RESULT_INVALID_STATE; }
    return r.result(result);
}
engine_result_t Pause(void* p) { return p ? cast(p)->result(cmvs_host_pause(cast(p)->core, 1)) : ENGINE_RESULT_INVALID_ARGUMENT; }
engine_result_t Resume(void* p) { return p ? cast(p)->result(cmvs_host_pause(cast(p)->core, 0)) : ENGINE_RESULT_INVALID_ARGUMENT; }
engine_result_t Option(void* p, const engine_option_t* o) {
    if (!p || !o || !o->key_utf8 || !o->value_utf8) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    const std::string key = o->key_utf8, value = o->value_utf8;
    if (key == "default_font") {
        if (r.font == value) return ENGINE_RESULT_OK;
        if (r.opened) return ENGINE_RESULT_INVALID_STATE;
        r.font = value; return ENGINE_RESULT_OK;
    }
    if (key == "cmvs_native_audio") {
        if (value != "true" && value != "false" && value != "1" && value != "0") return ENGINE_RESULT_INVALID_ARGUMENT;
        bool enabled = value == "true" || value == "1";
        if (r.opened && enabled != r.native_audio) return ENGINE_RESULT_INVALID_STATE;
        r.native_audio = enabled; return ENGINE_RESULT_OK;
    }
    return key.rfind("cmvs_", 0) == 0 ? ENGINE_RESULT_NOT_SUPPORTED : ENGINE_RESULT_OK;
}
engine_result_t Resize(void* p, uint32_t w, uint32_t h) { return p && w && h ? ENGINE_RESULT_OK : ENGINE_RESULT_INVALID_ARGUMENT; }
engine_result_t Frame(void* p, engine_frame_desc_t* f) {
    if (!p || !f || f->struct_size < sizeof(*f)) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    if (!r.opened) return ENGINE_RESULT_INVALID_STATE;
    *f = {}; f->struct_size = sizeof(*f);
    f->width = cmvs_host_width(r.core); f->height = cmvs_host_height(r.core);
    f->stride_bytes = f->width * 4; f->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
    f->frame_serial = cmvs_host_frame_serial(r.core);
    return ENGINE_RESULT_OK;
}
engine_result_t Read(void* p, void* output, size_t size) {
    if (!p || !output) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p); const auto* pixels = cmvs_host_pixels(r.core);
    if (!pixels) return ENGINE_RESULT_INVALID_STATE;
    size_t bytes = size_t(cmvs_host_width(r.core)) * cmvs_host_height(r.core) * 4;
    if (size < bytes) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(output, pixels, bytes); r.read_serial = cmvs_host_frame_serial(r.core);
    return ENGINE_RESULT_OK;
}
int key_code(int key) {
    if (key >= 0x70 && key <= 0x87) return 52 + key - 0x70;
    if (key >= 0x30 && key <= 0x39) return 76 + key - 0x30;
    if (key >= 0x41 && key <= 0x5a) return 86 + key - 0x41;
    switch (key) {
        case 8: return 20; case 9: return 21; case 13: return 22;
        case 16: return 23; case 17: return 24; case 27: return 25; case 32: return 26;
        case 33: return 27; case 34: return 28; case 35: return 29; case 36: return 30;
        case 45: return 31; case 46: return 32;
        case 37: return 35; case 38: return 33; case 39: return 36; case 40: return 34;
        default: return 0;
    }
}
engine_result_t Input(void* p, const engine_input_event_t* e) {
    if (!p || !e || e->struct_size < sizeof(*e)) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    if (uint32_t(e->modifiers) & ENGINE_INPUT_MODIFIER_POINTER_CANCEL) return r.result(cmvs_host_cancel_input(r.core));
    auto pulse = [&](int code) { const auto a = cmvs_host_key(r.core, code, 1); return a < 0 ? a : cmvs_host_key(r.core, code, 0); };
    if (e->type >= ENGINE_INPUT_EVENT_POINTER_DOWN && e->type <= ENGINE_INPUT_EVENT_POINTER_SCROLL) {
        if (!std::isfinite(e->x) || !std::isfinite(e->y) || e->x < INT32_MIN || e->x > INT32_MAX || e->y < INT32_MIN || e->y > INT32_MAX) return ENGINE_RESULT_INVALID_ARGUMENT;
        if (cmvs_host_pointer(r.core, int32_t(e->x), int32_t(e->y)) < 0) return r.result(-1);
        if (e->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) return e->delta_y == 0 ? ENGINE_RESULT_OK : r.result(pulse(e->delta_y > 0 ? 4 : 5));
        if (e->type == ENGINE_INPUT_EVENT_POINTER_MOVE) return ENGINE_RESULT_OK;
        int code = e->button == 0 ? 1 : e->button == 1 ? 3 : e->button == 2 ? 2 : 0;
        return code ? r.result(cmvs_host_key(r.core, code, e->type == ENGINE_INPUT_EVENT_POINTER_DOWN)) : ENGINE_RESULT_OK;
    }
    if (e->type == ENGINE_INPUT_EVENT_KEY_DOWN || e->type == ENGINE_INPUT_EVENT_KEY_UP) {
        int code = key_code(e->key_code);
        return code ? r.result(cmvs_host_key(r.core, code, e->type == ENGINE_INPUT_EVENT_KEY_DOWN)) : ENGINE_RESULT_OK;
    }
    if (e->type == ENGINE_INPUT_EVENT_BACK) return r.result(pulse(3));
    return e->type == ENGINE_INPUT_EVENT_TEXT_INPUT ? ENGINE_RESULT_OK : ENGINE_RESULT_NOT_SUPPORTED;
}
engine_result_t Rendered(void* p, uint32_t* rendered) {
    if (!p || !rendered) return ENGINE_RESULT_INVALID_ARGUMENT;
    *rendered = cmvs_host_frame_serial(cast(p)->core) != cast(p)->read_serial;
    return ENGINE_RESULT_OK;
}
engine_result_t Renderer(void* p, char* out, uint32_t size) {
    constexpr char info[] = "CMVS CPU RGBA8 (Godot presentation)";
    if (!p || !out || size < sizeof(info)) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(out, info, sizeof(info)); return ENGINE_RESULT_OK;
}
engine_result_t TextInput(void* p, uint32_t* flags) {
    if (!p || !flags) return ENGINE_RESULT_INVALID_ARGUMENT;
    *flags = 0; return ENGINE_RESULT_OK;
}
const char* Error(void* p) { return p ? cast(p)->error.c_str() : "Invalid CMVS runtime"; }
const engine_runtime_provider_v1_t& Provider() {
    static const auto provider = [] {
        engine_runtime_provider_v1_t p{}; p.struct_size = sizeof(p); p.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        p.runtime_id_utf8 = "cmvs"; p.display_name_utf8 = "CMVS"; p.priority = 90;
        p.probe = Probe; p.create = Create; p.destroy = Destroy; p.open_game = Open; p.tick = Tick;
        p.pause = Pause; p.resume = Resume; p.set_option = Option; p.set_surface_size = Resize;
        p.get_frame_desc = Frame; p.read_frame_rgba = Read; p.send_input = Input;
        p.get_frame_rendered_flag = Rendered; p.get_renderer_info = Renderer;
#ifdef ENGINE_BACKGROUND_PLAYBACK_QUERY
        // The host backend uses CPU composition and native Core Audio on iOS;
        // it does not depend on the foreground window or Godot rendering.
        p.background_playback = [](void* runtime, uint32_t mode) {
            if (!runtime || mode > ENGINE_BACKGROUND_PLAYBACK_LEAVE) return ENGINE_RESULT_INVALID_ARGUMENT;
            return cast(runtime)->opened ? ENGINE_RESULT_OK : ENGINE_RESULT_INVALID_STATE;
        };
#endif
        p.get_text_input_state = TextInput; p.get_last_error = Error; return p;
    }(); return provider;
}
} // namespace
void RegisterRuntimeProvider() {
    static std::once_flag once;
    std::call_once(once, [] { engine_register_runtime_provider(&Provider()); });
}
} // namespace aetherkiri::cmvs
