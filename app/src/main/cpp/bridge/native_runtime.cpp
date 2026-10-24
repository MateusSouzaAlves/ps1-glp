#include "native_runtime.h"

#include <android/log.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <string>

#include "frontend_callbacks.h"
#include "input_callbacks.h"

namespace emuorbit::bridge {
namespace {

constexpr char kLogTag[] = "EmuOrbitNative";

std::mutex gCoreMutex;
NativeSession* gActiveSession = nullptr;
thread_local std::string gLastError;

}  // namespace

std::mutex& nativeCoreMutex() {
    return gCoreMutex;
}

NativeSession* activeNativeSession() {
    return gActiveSession;
}

void setActiveNativeSession(NativeSession* session) {
    gActiveSession = session;
}

void clearNativeError() {
    gLastError.clear();
}

const char* lastNativeError() {
    return gLastError.c_str();
}

void setNativeError(const char* message) {
    gLastError = message == nullptr ? "Unknown native emulator error" : message;
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", gLastError.c_str());
}

NativeSession* nativeSessionFromHandle(jlong handle) {
    auto* session = reinterpret_cast<NativeSession*>(static_cast<intptr_t>(handle));
    if (session == nullptr || session != gActiveSession) {
        setNativeError("Invalid or closed native emulator session");
        return nullptr;
    }
    return session;
}

bool selectSessionController(NativeSession& session, unsigned device) {
    if (!session.analogInputEnabled || !session.gameLoaded ||
            session.api.setControllerPortDevice == nullptr ||
            std::find(session.controllerDevices.begin(), session.controllerDevices.end(), device) == session.controllerDevices.end()) {
        return false;
    }
    session.analogInput.clear();
    session.inputMask.store(0, std::memory_order_relaxed);
    session.api.setControllerPortDevice(0, device);
    return true;
}

void closeNativeCore(NativeSession& session) {
    if (session.gameLoaded) {
        session.api.unloadGame();
        session.gameLoaded = false;
    }
    if (session.coreInitialized) {
        session.api.deinit();
        session.coreInitialized = false;
    }
    // Core workers must already be joined by deinit. Drop optional callback
    // pointers and held inputs before unloading their backing library.
    session.discControl.reset();
    session.controllerDevices.clear();
    session.analogInput.clear();
    session.inputMask.store(0, std::memory_order_relaxed);
    session.pointerPressed.store(false, std::memory_order_relaxed);
    session.videoTarget = nullptr;
    session.audioTarget = nullptr;
    if (session.microphoneHandle != nullptr) {
        closeFrontendMicrophone(session.microphoneHandle);
    }
    if (session.mappedRom != nullptr) {
        munmap(session.mappedRom, session.mappedRomSize);
        session.mappedRom = nullptr;
        session.mappedRomSize = 0;
    }
    if (session.mappedRomFile >= 0) {
        close(session.mappedRomFile);
        session.mappedRomFile = -1;
    }
    if (session.api.libraryHandle != nullptr) {
        dlclose(session.api.libraryHandle);
        session.api.libraryHandle = nullptr;
    }
}

void destroyNativeSession(jlong handle) {
    std::unique_ptr<NativeSession> session;
    {
        std::lock_guard<std::mutex> lock(gCoreMutex);
        NativeSession* active = nativeSessionFromHandle(handle);
        if (active == nullptr) {
            return;
        }
        closeNativeCore(*active);
        unbindFrontendCallbacks(active);
        gActiveSession = nullptr;
        session.reset(active);
    }
}

}  // namespace emuorbit::bridge
