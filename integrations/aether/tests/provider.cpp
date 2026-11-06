#include "cmvs_runtime_provider.h"
#include "engine_runtime_provider.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
static const engine_runtime_provider_v1_t* provider;
extern "C" engine_result_t engine_register_runtime_provider(const engine_runtime_provider_v1_t* p) { provider = p; return ENGINE_RESULT_OK; }
static void require(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; std::exit(1); } }
int main(int argc, char** argv) {
    using namespace std::filesystem;
    aetherkiri::cmvs::RegisterRuntimeProvider();
    require(provider && std::strcmp(provider->runtime_id_utf8, "cmvs") == 0, "provider registration");
    require(provider->probe(nullptr, nullptr) == 0, "null probe");
    auto fixture = temp_directory_path() / ("cmvs-provider-" + std::to_string(std::rand()));
    require(create_directory(fixture), "new fixture directory");
    create_directories(fixture / "data/pack");
    std::ofstream(fixture / "data/pack/start.ps3") << "PS2A";
    std::ofstream(fixture / "data/pack/script.cpz") << "CPZ6";
    require(provider->probe(nullptr, fixture.c_str()) > 0, "container probe");
    std::ofstream(fixture / "data/pack/script.cpz") << "XXXX";
    require(provider->probe(nullptr, fixture.c_str()) == 0, "reject wrong container");
    engine_runtime_host_v1_t host{}; host.struct_size = sizeof(host); host.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
    engine_create_desc_t desc{}; desc.struct_size = sizeof(desc); desc.api_version = ENGINE_API_VERSION;
    auto user = (fixture / "user").string(); desc.writable_path_utf8 = user.c_str();
    void* runtime = nullptr;
    require(provider->create(nullptr, &host, &desc, &runtime) == 0 && runtime, "create");
    engine_frame_desc_t frame{}; frame.struct_size = sizeof(frame);
    require(provider->get_frame_desc(runtime, &frame) == ENGINE_RESULT_INVALID_STATE, "unopened frame");
    require(provider->send_input(runtime, nullptr) == ENGINE_RESULT_INVALID_ARGUMENT, "null input");
    require(provider->set_surface_size(runtime, 0, 1) == ENGINE_RESULT_INVALID_ARGUMENT, "invalid surface");
    engine_option_t invalid{}; invalid.key_utf8 = "cmvs_unknown"; invalid.value_utf8 = "1";
    require(provider->set_option(runtime, &invalid) == ENGINE_RESULT_NOT_SUPPORTED, "unknown provider option");
    if (argc == 4) {
        engine_option_t font{}; font.key_utf8 = "default_font"; font.value_utf8 = argv[2];
        require(provider->set_option(runtime, &font) == 0, "font option");
        engine_option_t audio{}; audio.key_utf8 = "cmvs_native_audio"; audio.value_utf8 = "0";
        require(provider->set_option(runtime, &audio) == 0, "deterministic audio clock");
        const int opened = provider->open_game(runtime, argv[1], "");
        if (opened != 0) std::cerr << provider->get_last_error(runtime) << '\n';
        require(opened == 0, "game open");
        auto advance = [&](uint64_t frames) {
            require(provider->get_frame_desc(runtime, &frame) == 0, "frame descriptor");
            const auto end = frame.frame_serial + frames;
            for (uint64_t slices = 0; frame.frame_serial < end && slices < frames * 100; ++slices) {
                int result = provider->tick(runtime, 16);
                if (result != 0) std::cerr << provider->get_last_error(runtime) << '\n';
                require(result == 0, "game tick");
                require(provider->get_frame_desc(runtime, &frame) == 0, "frame descriptor");
            }
            require(frame.frame_serial >= end, "frame budget");
        };
        advance(700);
        engine_input_event_t click{}; click.struct_size = sizeof(click);
        click.type = ENGINE_INPUT_EVENT_POINTER_DOWN; click.button = 0; click.x = 640; click.y = 280;
        require(provider->send_input(runtime, &click) == 0, "primary down");
        click.type = ENGINE_INPUT_EVENT_POINTER_UP;
        require(provider->send_input(runtime, &click) == 0, "primary up before next tick");
        advance(1800);
        for (int i = 0; i < 3; ++i) {
            engine_input_event_t key{}; key.struct_size = sizeof(key); key.key_code = 13;
            key.type = ENGINE_INPUT_EVENT_KEY_DOWN; require(provider->send_input(runtime, &key) == 0, "enter down");
            key.type = ENGINE_INPUT_EVENT_KEY_UP; require(provider->send_input(runtime, &key) == 0, "enter up");
            advance(90);
        }
        std::vector<unsigned char> rgba(size_t(frame.width)*frame.height*4);
        require(provider->read_frame_rgba(runtime, rgba.data(), rgba.size()-1) == ENGINE_RESULT_INVALID_ARGUMENT, "short frame buffer");
        require(provider->read_frame_rgba(runtime, rgba.data(), rgba.size()) == 0, "frame read");
        std::ofstream out(argv[3], std::ios::binary); out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        for (size_t i=0; i<rgba.size(); i+=4) out.write(reinterpret_cast<char*>(rgba.data()+i), 3);
        require(provider->pause(runtime) == 0 && provider->resume(runtime) == 0, "pause resume");
        std::cout << "CMVS_GAME_OK frames=" << frame.frame_serial << '\n';
    }
    provider->destroy(runtime);
    remove_all(fixture);
    std::cout << "CMVS_PROVIDER_OK\n";
}
