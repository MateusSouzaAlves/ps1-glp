#include "input_callbacks.h"

#include <algorithm>
#include <atomic>
#include <limits>

#include "frontend_callbacks.h"

namespace emuorbit::bridge {
namespace {

constexpr unsigned kMicrophoneSampleRate = 44'100U;

FrontendMicHandle* openFrontendMicrophone(const FrontendMicParams* params) {
    NativeSession* session = activeFrontendSession();
    if (session == nullptr || !session->nintendoDs || session->microphoneHandle != nullptr) {
        return nullptr;
    }
    auto* handle = new FrontendMicHandle{
            session,
            params != nullptr && params->rate > 0 ? params->rate : kMicrophoneSampleRate,
            false};
    session->microphoneReadIndex = 0;
    session->microphoneWriteIndex = 0;
    session->microphoneSampleCount = 0;
    session->microphoneHandle = handle;
    return handle;
}

bool getFrontendMicrophoneParams(
        const FrontendMicHandle* microphone,
        FrontendMicParams* params) {
    if (microphone == nullptr || microphone->session == nullptr || params == nullptr) {
        return false;
    }
    params->rate = microphone->rate;
    return true;
}

bool setFrontendMicrophoneState(FrontendMicHandle* microphone, bool active) {
    if (microphone == nullptr || microphone->session == nullptr) {
        return false;
    }
    microphone->active = active;
    if (!active) {
        microphone->session->microphoneReadIndex = 0;
        microphone->session->microphoneWriteIndex = 0;
        microphone->session->microphoneSampleCount = 0;
    }
    return true;
}

bool getFrontendMicrophoneState(const FrontendMicHandle* microphone) {
    return microphone != nullptr && microphone->session != nullptr && microphone->active;
}

int readFrontendMicrophone(
        FrontendMicHandle* microphone,
        int16_t* samples,
        size_t count) {
    if (microphone == nullptr || microphone->session == nullptr
            || !microphone->active || samples == nullptr
            || count > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    NativeSession& session = *microphone->session;
    const size_t available = std::min(count, session.microphoneSampleCount);
    for (size_t index = 0; index < available; ++index) {
        samples[index] = session.microphoneRing[session.microphoneReadIndex];
        session.microphoneReadIndex =
                (session.microphoneReadIndex + 1U) % kMicrophoneRingCapacity;
    }
    session.microphoneSampleCount -= available;
    std::fill(samples + available, samples + count, 0);
#if defined(EMUORBIT_TEST_PROBE)
    session.microphoneSamplesConsumed += available;
#endif
    return static_cast<int>(count);
}

}  // namespace

void inputPollCallback() {
}

int16_t inputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id) {
    NativeSession* session = activeFrontendSession();
    if (port != 0 || session == nullptr) {
        return 0;
    }
    if (device == RETRO_DEVICE_ANALOG) {
        return session->analogInputEnabled ? session->analogInput.get(index, id) : 0;
    }
    if (index != 0) {
        return 0;
    }
    if (device == RETRO_DEVICE_POINTER) {
        switch (id) {
        case RETRO_DEVICE_ID_POINTER_X:
            return session->pointerX.load(std::memory_order_relaxed);
        case RETRO_DEVICE_ID_POINTER_Y:
            return session->pointerY.load(std::memory_order_relaxed);
        case RETRO_DEVICE_ID_POINTER_PRESSED:
            return session->pointerPressed.load(std::memory_order_relaxed) ? 1 : 0;
        default:
            return 0;
        }
    }
    if (device != RETRO_DEVICE_JOYPAD) {
        return 0;
    }
    const uint16_t mask = session->inputMask.load(std::memory_order_relaxed);
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return static_cast<int16_t>(mask);
    }
    if (id >= 16) {
        return 0;
    }
    return static_cast<int16_t>((mask >> id) & 1U);
}

uint16_t maskForJavaButton(const NativeSession& session, int button) {
    unsigned retroButton;
    switch (button) {
    case 0:
        retroButton = RETRO_DEVICE_ID_JOYPAD_UP;
        break;
    case 1:
        retroButton = RETRO_DEVICE_ID_JOYPAD_DOWN;
        break;
    case 2:
        retroButton = RETRO_DEVICE_ID_JOYPAD_LEFT;
        break;
    case 3:
        retroButton = RETRO_DEVICE_ID_JOYPAD_RIGHT;
        break;
    case 4:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_B
                : RETRO_DEVICE_ID_JOYPAD_A;
        break;
    case 5:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_A
                : RETRO_DEVICE_ID_JOYPAD_B;
        break;
    case 6:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_R
                : RETRO_DEVICE_ID_JOYPAD_L;
        break;
    case 7:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_L
                : RETRO_DEVICE_ID_JOYPAD_R;
        break;
    case 8:
        retroButton = RETRO_DEVICE_ID_JOYPAD_START;
        break;
    case 9:
        retroButton = RETRO_DEVICE_ID_JOYPAD_SELECT;
        break;
    case 10:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_Y
                : RETRO_DEVICE_ID_JOYPAD_X;
        break;
    case 11:
        retroButton = session.megaDrive
                ? RETRO_DEVICE_ID_JOYPAD_X
                : RETRO_DEVICE_ID_JOYPAD_Y;
        break;
    default:
        return 0;
    }
    return static_cast<uint16_t>(1U << retroButton);
}

void setNintendoDsPointer(
        NativeSession& session,
        int frameX,
        int frameY,
        bool pressed) {
    const int width = static_cast<int>(std::max(1U, session.videoWidth));
    const int height = static_cast<int>(std::max(1U, session.videoHeight));
    const int clampedX = std::clamp(frameX, 0, width - 1);
    const int clampedY = std::clamp(frameY, 0, height - 1);
    const int normalizedX = width <= 1
            ? 0
            : -32768 + ((clampedX * 65535) / (width - 1));
    const int normalizedY = height <= 1
            ? 0
            : -32768 + ((clampedY * 65535) / (height - 1));
    session.pointerX.store(static_cast<int16_t>(normalizedX), std::memory_order_relaxed);
    session.pointerY.store(static_cast<int16_t>(normalizedY), std::memory_order_relaxed);
    session.pointerPressed.store(pressed, std::memory_order_relaxed);
}

bool configureFrontendMicrophoneInterface(NativeSession& session, void* data) {
    if (data == nullptr || !session.nintendoDs) {
        return false;
    }
    auto* microphoneInterface = static_cast<FrontendMicInterface*>(data);
    if (microphoneInterface->interfaceVersion != 1U) {
        return false;
    }
    microphoneInterface->interfaceVersion = 1U;
    microphoneInterface->open = openFrontendMicrophone;
    microphoneInterface->close = closeFrontendMicrophone;
    microphoneInterface->getParams = getFrontendMicrophoneParams;
    microphoneInterface->setState = setFrontendMicrophoneState;
    microphoneInterface->getState = getFrontendMicrophoneState;
    microphoneInterface->read = readFrontendMicrophone;
    return true;
}

void closeFrontendMicrophone(FrontendMicHandle* microphone) {
    if (microphone == nullptr) {
        return;
    }
    if (microphone->session != nullptr
            && microphone->session->microphoneHandle == microphone) {
        microphone->session->microphoneHandle = nullptr;
        microphone->session->microphoneSampleCount = 0;
    }
    microphone->session = nullptr;
    delete microphone;
}

bool canPushFrontendMicrophoneSamples(const NativeSession& session) {
    return session.nintendoDs
            && session.dsMicrophoneSource.load(std::memory_order_relaxed) == 3
            && session.microphoneHandle != nullptr
            && session.microphoneHandle->active;
}

size_t pushFrontendMicrophoneSamples(
        NativeSession& session,
        const int16_t* samples,
        size_t count) {
    if (!canPushFrontendMicrophoneSamples(session) || samples == nullptr) {
        return 0;
    }
    const size_t copyCount = std::min(count, kMicrophoneRingCapacity);
    for (size_t index = 0; index < copyCount; ++index) {
        if (session.microphoneSampleCount == kMicrophoneRingCapacity) {
            session.microphoneReadIndex =
                    (session.microphoneReadIndex + 1U) % kMicrophoneRingCapacity;
        } else {
            ++session.microphoneSampleCount;
        }
        session.microphoneRing[session.microphoneWriteIndex] = samples[index];
        session.microphoneWriteIndex =
                (session.microphoneWriteIndex + 1U) % kMicrophoneRingCapacity;
    }
    return copyCount;
}

}  // namespace emuorbit::bridge
