#include "frontend_callbacks.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <limits>

#include "core_options.h"
#include "input_callbacks.h"

namespace emuorbit::bridge {
namespace {

constexpr char kLogTag[] = "EmuOrbitNative";
constexpr unsigned kMaximumVideoWidth = 1030;
constexpr unsigned kMaximumVideoHeight = 576;
constexpr size_t kBytesPerRgb565Pixel = 2;
constexpr size_t kBytesPerStereoAudioFrame = sizeof(int16_t) * 2;
constexpr unsigned kGetMicrophoneInterfaceCommand =
        75U | RETRO_ENVIRONMENT_EXPERIMENTAL;

std::atomic<NativeSession*> gCallbackSession{nullptr};
std::atomic<FrontendErrorHandler> gErrorHandler{nullptr};

void reportError(const char* message) {
    FrontendErrorHandler handler = gErrorHandler.load(std::memory_order_acquire);
    if (handler != nullptr) {
        handler(message);
    }
}

void coreLog(enum retro_log_level level, const char* format, ...) {
#if defined(EMUORBIT_RELEASE)
    if (level != RETRO_LOG_ERROR && level != RETRO_LOG_WARN) {
        return;
    }
#endif
    int androidPriority = ANDROID_LOG_INFO;
    if (level == RETRO_LOG_ERROR) {
        androidPriority = ANDROID_LOG_ERROR;
    } else if (level == RETRO_LOG_WARN) {
        androidPriority = ANDROID_LOG_WARN;
    } else if (level == RETRO_LOG_DEBUG) {
        androidPriority = ANDROID_LOG_DEBUG;
    }

    va_list arguments;
    va_start(arguments, format);
    __android_log_vprint(androidPriority, kLogTag, format, arguments);
    va_end(arguments);
}

}  // namespace

void bindFrontendCallbacks(NativeSession* session, FrontendErrorHandler errorHandler) {
    gErrorHandler.store(errorHandler, std::memory_order_release);
    gCallbackSession.store(session, std::memory_order_release);
}

void unbindFrontendCallbacks(NativeSession* session) {
    NativeSession* expected = session;
    gCallbackSession.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel);
    if (gCallbackSession.load(std::memory_order_acquire) == nullptr) {
        gErrorHandler.store(nullptr, std::memory_order_release);
    }
}

NativeSession* activeFrontendSession() {
    return gCallbackSession.load(std::memory_order_acquire);
}

bool environmentCallback(unsigned command, void* data) {
    NativeSession* session = activeFrontendSession();
    switch (command) {
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        if (session == nullptr || !session->discMediaEnabled || data == nullptr) return false;
        *static_cast<unsigned*>(data) = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        return session != nullptr && session->discMediaEnabled &&
                session->discControl.registerLegacy(static_cast<const retro_disk_control_callback*>(data));
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        return session != nullptr && session->discMediaEnabled &&
                session->discControl.registerExtended(static_cast<const retro_disk_control_ext_callback*>(data));
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
        if (session == nullptr || !session->analogInputEnabled) return true; // Preserve existing adapters.
        if (data == nullptr) return false;
        const auto* info = static_cast<const retro_controller_info*>(data);
        if (info->num_types > 64 || (info->num_types != 0 && info->types == nullptr)) return false;
        session->controllerDevices.clear();
        for (unsigned i = 0; i < info->num_types; ++i) session->controllerDevices.push_back(info->types[i].id);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        if (data == nullptr) {
            return false;
        }
        const auto format = *static_cast<const enum retro_pixel_format*>(data);
        if (format != RETRO_PIXEL_FORMAT_RGB565
                && format != RETRO_PIXEL_FORMAT_XRGB8888) {
            return false;
        }
        if (session != nullptr) {
            session->pixelFormat = format;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (data == nullptr || session == nullptr) {
            return false;
        }
        *static_cast<const char**>(data) = session->systemDirectory.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (data == nullptr || session == nullptr) {
            return false;
        }
        *static_cast<const char**>(data) = session->saveDirectory.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        if (data == nullptr || session == nullptr) {
            return false;
        }
        *static_cast<const char**>(data) = session->systemDirectory.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (data == nullptr) {
            return false;
        }
        static_cast<struct retro_log_callback*>(data)->log = coreLog;
        return true;
    case kGetMicrophoneInterfaceCommand:
        return session != nullptr
                && configureFrontendMicrophoneInterface(*session, data);
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (data != nullptr) {
            *static_cast<bool*>(data) = true;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (data == nullptr) {
            return false;
        }
        *static_cast<unsigned*>(data) = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return session != nullptr
                && registerCoreOptionsV2(
                        *session,
                        static_cast<const retro_core_options_v2*>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        return session != nullptr && data != nullptr && registerCoreOptionsV2(
                *session, static_cast<const retro_core_options_v2_intl*>(data)->us);
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return session != nullptr
                && readCoreOption(*session, static_cast<retro_variable*>(data));
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        return session != nullptr
                && writeCoreOption(*session, static_cast<const retro_variable*>(data));
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (data != nullptr) {
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_PORTUGUESE_BRAZIL;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data != nullptr) {
            *static_cast<bool*>(data) = session != nullptr
                    && consumeCoreOptionsUpdated(*session);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        if (data == nullptr || session == nullptr) {
            return false;
        }
        const auto* geometry = static_cast<const struct retro_game_geometry*>(data);
        if (session->discMediaEnabled && (geometry->base_width == 0 || geometry->base_height == 0 ||
                geometry->base_width > kMaximumVideoWidth || geometry->base_height > kMaximumVideoHeight)) {
            return false;
        }
        session->videoWidth = geometry->base_width;
        session->videoHeight = geometry->base_height;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        if (data == nullptr || session == nullptr) {
            return false;
        }
        const auto* avInfo = static_cast<const struct retro_system_av_info*>(data);
        if (session->discMediaEnabled && (avInfo->geometry.base_width == 0 || avInfo->geometry.base_height == 0 ||
                avInfo->geometry.base_width > kMaximumVideoWidth ||
                avInfo->geometry.base_height > kMaximumVideoHeight ||
                !std::isfinite(avInfo->timing.fps) || avInfo->timing.fps <= 0.0 ||
                !std::isfinite(avInfo->timing.sample_rate) || avInfo->timing.sample_rate <= 0.0)) {
            return false;
        }
        session->videoWidth = avInfo->geometry.base_width;
        session->videoHeight = avInfo->geometry.base_height;
        if (avInfo->timing.fps > 0.0) {
            session->framesPerSecond = avInfo->timing.fps;
        }
        if (avInfo->timing.sample_rate > 0.0) {
            session->sampleRate = avInfo->timing.sample_rate;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
#if defined(EMUORBIT_TEST_PROBE)
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        if (session == nullptr || data == nullptr) {
            return false;
        }
        const auto* memoryMap = static_cast<const retro_memory_map*>(data);
        if (memoryMap->descriptors == nullptr || memoryMap->num_descriptors == 0) {
            session->debugMemoryMaps.clear();
            return true;
        }
        session->debugMemoryMaps.assign(
                memoryMap->descriptors,
                memoryMap->descriptors + memoryMap->num_descriptors);
        return true;
    }
#else
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return true;
#endif
    case RETRO_ENVIRONMENT_SHUTDOWN:
        if (session == nullptr) {
            return false;
        }
        session->coreShutdownRequested.store(true, std::memory_order_release);
        return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:
        return false;
    default:
        return false;
    }
}

void videoCallback(const void* data, unsigned width, unsigned height, size_t pitch) {
    NativeSession* session = activeFrontendSession();
    if (session == nullptr || session->videoTarget == nullptr || data == nullptr
            || data == RETRO_HW_FRAME_BUFFER_VALID) {
        return;
    }

    if (width == 0 || height == 0 || (session->discMediaEnabled &&
            (width > kMaximumVideoWidth || height > kMaximumVideoHeight))) {
        reportError("Invalid software video frame geometry");
        return;
    }
    // Keep legacy adapters' viewport behavior; strict rejection is opt-in for
    // new disc sessions. Both paths still guard pitch arithmetic and buffers.
    width = std::min(width, kMaximumVideoWidth);
    height = std::min(height, kMaximumVideoHeight);
    if (pitch > std::numeric_limits<size_t>::max() / height) {
        reportError("Invalid software video frame pitch");
        return;
    }
    const size_t targetRowBytes = static_cast<size_t>(width) * kBytesPerRgb565Pixel;
    const size_t sourcePixelBytes = session->pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888
            ? sizeof(uint32_t)
            : kBytesPerRgb565Pixel;
    const size_t sourceRowBytes = static_cast<size_t>(width) * sourcePixelBytes;
    const size_t requiredBytes = targetRowBytes * height;
    if (requiredBytes > session->videoCapacity || pitch < sourceRowBytes) {
        reportError("Video frame exceeded the configured RGB565 buffer");
        return;
    }

    const auto* source = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < height; ++row) {
        uint8_t* targetRow = session->videoTarget + (row * targetRowBytes);
        const uint8_t* sourceRow = source + (row * pitch);
        if (session->pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
            auto* targetPixels = reinterpret_cast<uint16_t*>(targetRow);
            const auto* sourcePixels = reinterpret_cast<const uint32_t*>(sourceRow);
            for (unsigned column = 0; column < width; ++column) {
                const uint32_t pixel = sourcePixels[column];
                const uint16_t red = static_cast<uint16_t>((pixel >> 19U) & 0x1FU);
                const uint16_t green = static_cast<uint16_t>((pixel >> 10U) & 0x3FU);
                const uint16_t blue = static_cast<uint16_t>((pixel >> 3U) & 0x1FU);
                targetPixels[column] = static_cast<uint16_t>((red << 11U)
                        | (green << 5U) | blue);
            }
        } else {
            std::memcpy(targetRow, sourceRow, targetRowBytes);
        }
    }
    session->videoWidth = width;
    session->videoHeight = height;
}

size_t audioCallback(const int16_t* data, size_t frames) {
    NativeSession* session = activeFrontendSession();
    if (session == nullptr || session->audioTarget == nullptr || data == nullptr) {
        return frames;
    }

    const size_t availableFrames = session->audioCapacityFrames > session->audioFrames
            ? session->audioCapacityFrames - session->audioFrames
            : 0;
    const size_t copiedFrames = std::min(frames, availableFrames);
    if (copiedFrames > 0) {
        std::memcpy(session->audioTarget + (session->audioFrames * 2),
                    data,
                    copiedFrames * kBytesPerStereoAudioFrame);
        session->audioFrames += copiedFrames;
    }
    return frames;
}

}  // namespace emuorbit::bridge
