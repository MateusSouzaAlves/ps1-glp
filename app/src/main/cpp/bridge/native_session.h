#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core_backend.h"
#include "analog_input.h"
#include "disc_control.h"
#include "libretro.h"

namespace emuorbit::bridge {

inline constexpr size_t kMicrophoneRingCapacity = 44'100U * 2U;

struct NativeSession;

struct FrontendMicParams {
    unsigned rate;
};

struct FrontendMicHandle {
    NativeSession* session;
    unsigned rate;
    bool active;
};

struct FrontendMicInterface {
    unsigned interfaceVersion;
    FrontendMicHandle* (*open)(const FrontendMicParams* params);
    void (*close)(FrontendMicHandle* microphone);
    bool (*getParams)(const FrontendMicHandle* microphone, FrontendMicParams* params);
    bool (*setState)(FrontendMicHandle* microphone, bool active);
    bool (*getState)(const FrontendMicHandle* microphone);
    int (*read)(FrontendMicHandle* microphone, int16_t* samples, size_t count);
};

/** Mutable state owned by one Java NativeEmulatorEngine session. */
struct NativeSession {
    LibretroApi api;
    // Opt-in contracts only. Existing cartridge/DS adapters keep both disabled.
    bool discMediaEnabled = false;
    bool analogInputEnabled = false;
    DiscControl discControl;
    AnalogInput analogInput;
    std::vector<unsigned> controllerDevices;
    std::atomic<uint16_t> inputMask{0};
    std::atomic<int16_t> pointerX{0};
    std::atomic<int16_t> pointerY{0};
    std::atomic<bool> pointerPressed{false};
    std::atomic<bool> verticalDsLayout{false};
    std::atomic<int> dsLayout{2};
    std::atomic<int> dsScreenGap{0};
    std::atomic<int> dsFocusRatio{2};
    std::atomic<int> dsMicrophoneSource{0};
    std::atomic<bool> dsShowTouchCursor{false};
    std::atomic<bool> coreOptionsUpdated{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> coreShutdownRequested{false};
    std::string systemDirectory;
    std::string saveDirectory;
    std::string gamePath;
    std::unordered_map<std::string, std::string> coreOptions;
    uint8_t* videoTarget = nullptr;
    size_t videoCapacity = 0;
    int16_t* audioTarget = nullptr;
    size_t audioCapacityFrames = 0;
    unsigned videoWidth = 0;
    unsigned videoHeight = 0;
    size_t audioFrames = 0;
    double framesPerSecond = 60.0;
    double sampleRate = 32768.0;
    enum retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_RGB565;
    bool nintendoDs = false;
    // BlastEm exposes the Genesis buttons through libretro's generic
    // RetroPad order. Keep the session-level mapping explicit so Android's
    // visible A/B/X/Y/C/Z labels reach the matching console input.
    bool megaDrive = false;
    bool coreInitialized = false;
    bool gameLoaded = false;
    void* mappedRom = nullptr;
    size_t mappedRomSize = 0;
    int mappedRomFile = -1;
    std::array<int16_t, kMicrophoneRingCapacity> microphoneRing{};
    size_t microphoneReadIndex = 0;
    size_t microphoneWriteIndex = 0;
    size_t microphoneSampleCount = 0;
    FrontendMicHandle* microphoneHandle = nullptr;
#if defined(EMUORBIT_TEST_PROBE)
    uint64_t microphoneSamplesConsumed = 0;
    bool classicAudioFilterConfigured = false;
    std::vector<retro_memory_descriptor> debugMemoryMaps;
#endif
};

}  // namespace emuorbit::bridge
