#pragma once

#include <jni.h>

#include <cstdint>

#include "libretro.h"

namespace emuorbit::bridge {

using FileBackedSaveFlush = void (*)();

/** Function table shared by every libretro-compatible backend. */
struct LibretroApi {
    void* libraryHandle = nullptr;
    decltype(&retro_set_environment) setEnvironment = nullptr;
    decltype(&retro_set_video_refresh) setVideoRefresh = nullptr;
    decltype(&retro_set_audio_sample) setAudioSample = nullptr;
    decltype(&retro_set_audio_sample_batch) setAudioSampleBatch = nullptr;
    decltype(&retro_set_input_poll) setInputPoll = nullptr;
    decltype(&retro_set_input_state) setInputState = nullptr;
    decltype(&retro_api_version) apiVersion = nullptr;
    decltype(&retro_init) init = nullptr;
    decltype(&retro_deinit) deinit = nullptr;
    decltype(&retro_load_game) loadGame = nullptr;
    decltype(&retro_unload_game) unloadGame = nullptr;
    decltype(&retro_get_system_av_info) getSystemAvInfo = nullptr;
    decltype(&retro_run) run = nullptr;
    decltype(&retro_reset) reset = nullptr;
    decltype(&retro_get_memory_data) getMemoryData = nullptr;
    decltype(&retro_get_memory_size) getMemorySize = nullptr;
    decltype(&retro_serialize_size) serializeSize = nullptr;
    decltype(&retro_serialize) serialize = nullptr;
    decltype(&retro_unserialize) unserialize = nullptr;
    decltype(&retro_cheat_reset) cheatReset = nullptr;
    decltype(&retro_cheat_set) cheatSet = nullptr;
    // Optional until a backend explicitly opts into controller negotiation.
    decltype(&retro_set_controller_port_device) setControllerPortDevice = nullptr;
    FileBackedSaveFlush flushFileBackedSave = nullptr;
};

/**
 * Neutral loading modes. New emulator engines join the private registry in
 * core_backend.cpp instead of adding product-specific branches to JNI code.
 */
enum class CoreBackendKind : uint8_t {
    BuiltIn,
    ProtectedAsset,
    Snes,
    MegaDrive,
    Nes,
    Sega8Bit,
};

struct CoreBackendLoadResult {
    bool loaded;
    const char* errorMessage;
};

CoreBackendLoadResult loadCoreBackend(
        CoreBackendKind kind,
        JNIEnv* env,
        jobject assetManagerObject,
        LibretroApi& api);

}  // namespace emuorbit::bridge
