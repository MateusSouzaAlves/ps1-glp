#include <jni.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include "native_runtime.h"
#include "frontend_callbacks.h"
#include "input_callbacks.h"

using namespace emuorbit::bridge;

namespace {
constexpr size_t kVideoBytes = 1030U * 576U * 2U;
constexpr size_t kAudioBytes = 8192U * 2U * sizeof(int16_t);
constexpr size_t kMemoryCardBytes = 128U * 1024U;
constexpr size_t kMaxStateBytes = 8U * 1024U * 1024U;
void fail(JNIEnv* env, const char* message) {
    if (!env->ExceptionCheck()) {
        jclass type = env->FindClass("java/io/IOException");
        if (type != nullptr) { env->ThrowNew(type, message); env->DeleteLocalRef(type); }
    }
}
bool string(JNIEnv* env, jstring value, std::string& result) {
    if (value == nullptr) { fail(env, "Missing PS1 session value"); return false; }
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (text == nullptr) return false;
    result = text;
    env->ReleaseStringUTFChars(value, text);
    if (result.empty() || result.size() > 4096) { fail(env, "Invalid PS1 session value"); return false; }
    return true;
}
struct Opening {
    std::unique_ptr<NativeSession> session = std::make_unique<NativeSession>();
    ~Opening() {
        if (session) {
            closeNativeCore(*session);
            unbindFrontendCallbacks(session.get());
            if (activeNativeSession() == session.get()) setActiveNativeSession(nullptr);
        }
    }
};
bool loadApi(NativeSession& session) {
#define API(field, symbol) \
    session.api.field = reinterpret_cast<decltype(session.api.field)>(dlsym(session.api.libraryHandle, #symbol)); \
    if (session.api.field == nullptr) return false
    API(setEnvironment, retro_set_environment);
    API(setVideoRefresh, retro_set_video_refresh);
    API(setAudioSampleBatch, retro_set_audio_sample_batch);
    API(setInputPoll, retro_set_input_poll);
    API(setInputState, retro_set_input_state);
    API(apiVersion, retro_api_version);
    API(init, retro_init); API(deinit, retro_deinit);
    API(loadGame, retro_load_game); API(unloadGame, retro_unload_game);
    API(getSystemAvInfo, retro_get_system_av_info);
    API(run, retro_run);
    API(setControllerPortDevice, retro_set_controller_port_device);
    API(getMemoryData, retro_get_memory_data);
    API(getMemorySize, retro_get_memory_size);
    API(serializeSize, retro_serialize_size);
    API(serialize, retro_serialize);
    API(unserialize, retro_unserialize);
#undef API
    return session.api.apiVersion() == RETRO_API_VERSION;
}
NativeSession* checked(JNIEnv* env, jlong handle) {
    NativeSession* session = nativeSessionFromHandle(handle);
    if (session == nullptr) fail(env, "PS1 native session is closed");
    return session;
}
void* memoryCard(JNIEnv* env, NativeSession& session) {
    const size_t bytes = session.api.getMemorySize(RETRO_MEMORY_SAVE_RAM);
    void* card = session.api.getMemoryData(RETRO_MEMORY_SAVE_RAM);
    if (bytes != kMemoryCardBytes || card == nullptr) {
        fail(env, "PS1 card1 memory is unavailable or has unexpected size"); return nullptr;
    }
    return card;
}
bool discState(JNIEnv* env, NativeSession& session, jint (&state)[3]) {
    if (!session.discControl.available()) {
        fail(env, "PS1 disc control is unavailable"); return false;
    }
    const unsigned count = session.discControl.count();
    const unsigned index = session.discControl.index();
    if (count == 0 || count > 8 || index >= count) {
        fail(env, "PS1 core reported an invalid loaded disc state"); return false;
    }
    state[0] = static_cast<jint>(count);
    state[1] = static_cast<jint>(index);
    state[2] = session.discControl.ejected() ? 1 : 0;
    return true;
}
jintArray newDiscState(JNIEnv* env, const jint (&state)[3]) {
    jintArray result = env->NewIntArray(3);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 3, state);
    return result;
}
bool restoreClosedDisc(NativeSession& session, unsigned previous) {
    // Callback success alone is not enough: a third-party core may mutate
    // state before returning false.  Re-open if necessary, select the known
    // old item and require a closed, confirmed result before letting a failed
    // transaction return to Java.
    if (!session.discControl.ejected() && !session.discControl.setEjected(true)) return false;
    if (!session.discControl.select(previous)) return false;
    return session.discControl.setEjected(false) && !session.discControl.ejected()
            && session.discControl.index() == previous;
}
size_t stateBytes(JNIEnv* env, NativeSession& session) {
    const size_t size = session.api.serializeSize();
    if (size == 0 || size > kMaxStateBytes) {
        fail(env, "PS1 state size is unavailable or exceeds its private limit"); return 0;
    }
    return size;
}
} // namespace

#define JNI(name) Java_com_mateussouza_emuorbit_advance_emulator_playstation_PlayStationNativeSession_##name
extern "C" JNIEXPORT jint JNICALL JNI(nativeStateSize)(JNIEnv* env, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    auto* session = checked(env, handle);
    return session == nullptr ? 0 : static_cast<jint>(stateBytes(env, *session));
}
extern "C" JNIEXPORT jbyteArray JNICALL JNI(nativeCaptureState)(JNIEnv* env, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    auto* session = checked(env, handle);
    if (session == nullptr) return nullptr;
    const size_t size = stateBytes(env, *session);
    if (size == 0) return nullptr;
    try {
        std::vector<uint8_t> bytes(size);
        if (!session->api.serialize(bytes.data(), size)) { fail(env, "PS1 core refused state capture"); return nullptr; }
        jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
        if (result != nullptr) env->SetByteArrayRegion(result, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(bytes.data()));
        return result;
    } catch (const std::bad_alloc&) { fail(env, "PS1 state capture allocation failed"); return nullptr; }
}
extern "C" JNIEXPORT void JNICALL JNI(nativeRestoreState)(JNIEnv* env, jclass, jlong handle, jbyteArray payload) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    auto* session = checked(env, handle);
    if (session == nullptr) return;
    const size_t size = stateBytes(env, *session);
    // The pinned core ignores the size argument internally. Exact length is a
    // mandatory frontend guard, not merely a hint sent to retro_unserialize.
    if (size == 0 || payload == nullptr || env->GetArrayLength(payload) != static_cast<jsize>(size)) {
        fail(env, "PS1 state payload does not match the core's exact size"); return;
    }
    try {
        std::vector<uint8_t> bytes(size);
        env->GetByteArrayRegion(payload, 0, static_cast<jsize>(size), reinterpret_cast<jbyte*>(bytes.data()));
        if (env->ExceptionCheck()) return;
        if (!session->api.unserialize(bytes.data(), size)) fail(env, "PS1 core refused state restore; session must stop");
    } catch (const std::bad_alloc&) { fail(env, "PS1 state restore allocation failed"); }
}
extern "C" JNIEXPORT jlong JNICALL JNI(nativeOpen)(JNIEnv* env, jclass, jstring core,
        jstring game, jstring initialPath, jint initialIndex, jint expectedDiscCount,
        jstring system, jstring saves, jobjectArray keys, jobjectArray values) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    if (activeNativeSession() != nullptr) { fail(env, "Another PS1 session is active"); return 0; }
    clearNativeError();
    Opening opening;
    auto& session = *opening.session;
    std::string library, preparedInitial;
    if (!string(env, core, library) || !string(env, game, session.gamePath)
            || !string(env, initialPath, preparedInitial)
            || !string(env, system, session.systemDirectory) || !string(env, saves, session.saveDirectory)) return 0;
    if (expectedDiscCount < 1 || expectedDiscCount > 8 || initialIndex < 0 || initialIndex >= expectedDiscCount) {
        fail(env, "Invalid PS1 prepared disc mapping"); return 0;
    }
    if (keys == nullptr || values == nullptr || env->GetArrayLength(keys) != env->GetArrayLength(values)
            || env->GetArrayLength(keys) != 5) { fail(env, "Invalid PS1 boot option contract"); return 0; }
    session.api.libraryHandle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (session.api.libraryHandle == nullptr || !loadApi(session)) {
        fail(env, "PS1 core unavailable or incompatible"); return 0;
    }
    session.discMediaEnabled = true;
    session.analogInputEnabled = true;
    setActiveNativeSession(&session);
    bindFrontendCallbacks(&session, setNativeError);
    session.api.setEnvironment(environmentCallback);
    std::unordered_set<std::string> submitted;
    for (jsize i = 0; i < env->GetArrayLength(keys); ++i) {
        auto key = static_cast<jstring>(env->GetObjectArrayElement(keys, i));
        auto value = static_cast<jstring>(env->GetObjectArrayElement(values, i));
        std::string name, selected;
        bool valid = !env->ExceptionCheck() && string(env, key, name) && string(env, value, selected);
        if (key) env->DeleteLocalRef(key);
        if (value) env->DeleteLocalRef(value);
        if (!valid) return 0;
        if ((name != "pcsx_rearmed_bios" && name != "pcsx_rearmed_region"
                && name != "pcsx_rearmed_memcard1" && name != "pcsx_rearmed_memcard2"
                && name != "pcsx_rearmed_drc") || !submitted.insert(name).second) {
            fail(env, "Unexpected or duplicate PS1 boot option"); return 0;
        }
        if (session.coreOptions.find(name) == session.coreOptions.end()) {
            fail(env, "PS1 core does not advertise a required boot option"); return 0;
        }
        session.coreOptions[name] = selected;
    }
    if ((session.coreOptions["pcsx_rearmed_bios"] != "HLE" && session.coreOptions["pcsx_rearmed_bios"] != "auto")
            || (session.coreOptions["pcsx_rearmed_region"] != "auto"
                && session.coreOptions["pcsx_rearmed_region"] != "NTSC"
                && session.coreOptions["pcsx_rearmed_region"] != "PAL")
            || session.coreOptions["pcsx_rearmed_memcard1"] != "libretro"
            || session.coreOptions["pcsx_rearmed_memcard2"] != "none"
            || session.coreOptions["pcsx_rearmed_drc"] != "enabled") {
        fail(env, "Unsupported PS1 boot options"); return 0;
    }
    session.api.setVideoRefresh(videoCallback);
    session.api.setAudioSampleBatch(audioCallback);
    session.api.setInputPoll(inputPollCallback);
    session.api.setInputState(inputStateCallback);
    session.api.init(); session.coreInitialized = true;
    if (!session.discControl.setInitial(static_cast<unsigned>(initialIndex), preparedInitial.c_str())) {
        fail(env, "PS1 core refused the prepared initial disc"); return 0;
    }
    retro_game_info media{session.gamePath.c_str(), nullptr, 0, nullptr};
    if (!session.api.loadGame(&media)) { fail(env, "PS1 core could not load the prepared disc"); return 0; }
    session.gameLoaded = true;
    if (!selectSessionController(session, RETRO_DEVICE_JOYPAD) || !session.discControl.available()) {
        fail(env, "PS1 core input or disc interface is unavailable"); return 0;
    }
    jint loaded[3]{};
    if (!discState(env, session, loaded)) return 0;
    if (loaded[0] != expectedDiscCount || loaded[1] != initialIndex || loaded[2] != 0) {
        fail(env, "PS1 core did not confirm the prepared initial disc mapping"); return 0;
    }
    retro_system_av_info av{};
    session.api.getSystemAvInfo(&av);
    if (av.geometry.base_width == 0 || av.geometry.base_width > 1030
            || av.geometry.base_height == 0 || av.geometry.base_height > 576
            || !std::isfinite(av.timing.fps) || av.timing.fps <= 0 || av.timing.fps > 240
            || !std::isfinite(av.timing.sample_rate) || av.timing.sample_rate <= 0 || av.timing.sample_rate > 192000
            || lastNativeError()[0] != '\0') {
        fail(env, "Invalid PS1 video/audio initialization"); return 0;
    }
    session.framesPerSecond = av.timing.fps;
    session.sampleRate = av.timing.sample_rate;
    session.videoWidth = av.geometry.base_width;
    session.videoHeight = av.geometry.base_height;
    return reinterpret_cast<jlong>(opening.session.release());
}

extern "C" JNIEXPORT jdoubleArray JNICALL JNI(nativeTiming)(JNIEnv* env, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return nullptr;
    jdouble values[]{session->framesPerSecond, session->sampleRate};
    jdoubleArray result = env->NewDoubleArray(2);
    if (result != nullptr) env->SetDoubleArrayRegion(result, 0, 2, values);
    return result;
}

extern "C" JNIEXPORT void JNICALL JNI(nativeRun)(JNIEnv* env, jclass, jlong handle,
        jobject video, jobject audio, jintArray info) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return;
    void* pixels = video ? env->GetDirectBufferAddress(video) : nullptr;
    void* samples = audio ? env->GetDirectBufferAddress(audio) : nullptr;
    if (!pixels || !samples || env->GetDirectBufferCapacity(video) < static_cast<jlong>(kVideoBytes)
            || env->GetDirectBufferCapacity(audio) < static_cast<jlong>(kAudioBytes)
            || !info || env->GetArrayLength(info) != 3) {
        fail(env, "Invalid PS1 direct frame buffers"); return;
    }
    const auto videoAddress = reinterpret_cast<uintptr_t>(pixels);
    const auto audioAddress = reinterpret_cast<uintptr_t>(samples);
    if (videoAddress % alignof(uint16_t) != 0 || audioAddress % alignof(int16_t) != 0
            || (videoAddress <= audioAddress ? audioAddress - videoAddress < kVideoBytes
                                            : videoAddress - audioAddress < kAudioBytes)) {
        fail(env, "PS1 frame buffers are unaligned or overlap"); return;
    }
    clearNativeError();
    session->videoTarget = static_cast<uint8_t*>(pixels);
    session->videoCapacity = kVideoBytes;
    session->audioTarget = static_cast<int16_t*>(samples);
    session->audioCapacityFrames = kAudioBytes / (sizeof(int16_t) * 2);
    session->audioFrames = 0;
    session->api.run();
    // Never retain Java buffer addresses past this JNI invocation.
    session->videoTarget = nullptr; session->videoCapacity = 0;
    session->audioTarget = nullptr; session->audioCapacityFrames = 0;
    if (lastNativeError()[0] != '\0' || session->coreShutdownRequested.load()) {
        fail(env, "PS1 core stopped or produced an invalid frame"); return;
    }
    jint values[]{static_cast<jint>(session->videoWidth), static_cast<jint>(session->videoHeight),
                  static_cast<jint>(session->audioFrames)};
    env->SetIntArrayRegion(info, 0, 3, values);
}

extern "C" JNIEXPORT void JNICALL JNI(nativeButtons)(JNIEnv* env, jclass, jlong handle, jint mask) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (session) {
        session->inputMask.store(static_cast<uint16_t>(mask));
        if (mask == 0) session->analogInput.clear();
    }
}
extern "C" JNIEXPORT void JNICALL JNI(nativeController)(JNIEnv* env, jclass, jlong handle, jint device) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return;
    if ((device != RETRO_DEVICE_JOYPAD && device != RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1))
            || !selectSessionController(*session, static_cast<unsigned>(device))) {
        fail(env, "PS1 core does not advertise the selected controller");
    }
}
extern "C" JNIEXPORT void JNICALL JNI(nativeInput)(JNIEnv* env, jclass, jlong handle,
        jint mask, jint leftX, jint leftY, jint rightX, jint rightY) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return;
    if ((mask & ~0xffff) != 0 || leftX < -32768 || leftX > 32767 || leftY < -32768 || leftY > 32767
            || rightX < -32768 || rightX > 32767 || rightY < -32768 || rightY > 32767) {
        fail(env, "Invalid PS1 input snapshot"); return;
    }
    // Same lock and owner as retro_run: a frame never sees half a new snapshot.
    // A zero digital mask must not erase a held analog stick.
    session->inputMask.store(static_cast<uint16_t>(mask));
    session->analogInput.set(0, 0, leftX); session->analogInput.set(0, 1, leftY);
    session->analogInput.set(1, 0, rightX); session->analogInput.set(1, 1, rightY);
}
extern "C" JNIEXPORT void JNICALL JNI(nativeClose)(JNIEnv*, jclass, jlong handle) {
    destroyNativeSession(handle);
}

extern "C" JNIEXPORT jbyteArray JNICALL JNI(nativeReadMemoryCard)(JNIEnv* env, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return nullptr;
    void* card = memoryCard(env, *session);
    if (!card) return nullptr;
    // Copy only the exact save RAM; never expose native addresses to Java.
    jbyteArray snapshot = env->NewByteArray(static_cast<jsize>(kMemoryCardBytes));
    if (snapshot) env->SetByteArrayRegion(snapshot, 0, static_cast<jsize>(kMemoryCardBytes), static_cast<jbyte*>(card));
    return snapshot;
}
extern "C" JNIEXPORT void JNICALL JNI(nativeRestoreMemoryCard)(JNIEnv* env, jclass,
        jlong handle, jbyteArray snapshot) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return;
    if (!snapshot || env->GetArrayLength(snapshot) != static_cast<jsize>(kMemoryCardBytes)) {
        fail(env, "Invalid PS1 memory card restore size"); return;
    }
    void* card = memoryCard(env, *session);
    if (!card) return;
    // Validate into a temporary buffer BEFORE mutating the live card.
    std::unique_ptr<jbyte[]> bytes(new (std::nothrow) jbyte[kMemoryCardBytes]);
    if (!bytes) { fail(env, "PS1 memory card restore allocation failed"); return; }
    env->GetByteArrayRegion(snapshot, 0, static_cast<jsize>(kMemoryCardBytes), bytes.get());
    if (env->ExceptionCheck()) return;
    if (bytes[0] != 'M' || bytes[1] != 'C' || static_cast<uint8_t>(bytes[127]) != 0x0e) {
        fail(env, "Invalid PS1 memory card restore header"); return;
    }
    std::memcpy(card, bytes.get(), kMemoryCardBytes);
}

extern "C" JNIEXPORT jintArray JNICALL JNI(nativeDiscState)(JNIEnv* env, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return nullptr;
    jint state[3]{};
    return discState(env, *session, state) ? newDiscState(env, state) : nullptr;
}

extern "C" JNIEXPORT jintArray JNICALL JNI(nativeSwitchDisc)(JNIEnv* env, jclass,
        jlong handle, jint requested) {
    std::lock_guard<std::mutex> lock(nativeCoreMutex());
    NativeSession* session = checked(env, handle);
    if (!session) return nullptr;
    if (requested < 0) { fail(env, "Invalid PS1 disc index"); return nullptr; }
    jint before[3]{};
    if (!discState(env, *session, before)) return nullptr;
    const unsigned previous = static_cast<unsigned>(before[1]);
    const unsigned target = static_cast<unsigned>(requested);
    if (target >= static_cast<unsigned>(before[0])) { fail(env, "PS1 disc index is not loaded"); return nullptr; }
    if (before[2] != 0) { fail(env, "PS1 disc tray is unexpectedly open"); return nullptr; }
    if (target == previous) return newDiscState(env, before);

    // The core owns its preloaded disk list.  Keep the entire eject/select/
    // insert sequence under nativeCoreMutex so no frame or input can interleave
    // with it.  If selection fails, try to return the tray to its prior closed
    // state before reporting a failure.
    if (!session->discControl.setEjected(true)) {
        fail(env, !session->discControl.ejected() && session->discControl.index() == previous
                ? "PS1 core refused to open the disc tray"
                : restoreClosedDisc(*session, previous)
                    ? "PS1 core refused tray open but restored the previous disc"
                    : "PS1 core refused tray open and could not restore the previous disc");
        return nullptr;
    }
    if (!session->discControl.select(target)) {
        fail(env, restoreClosedDisc(*session, previous)
                ? "PS1 core refused the selected preloaded disc"
                : "PS1 core refused a disc and could not restore the previous disc");
        return nullptr;
    }
    if (!session->discControl.setEjected(false)) {
        fail(env, restoreClosedDisc(*session, previous)
                ? "PS1 core refused to close the disc tray"
                : "PS1 core refused tray close and could not restore the previous disc");
        return nullptr;
    }
    jint after[3]{};
    if (!discState(env, *session, after)) return nullptr;
    if (after[1] != requested || after[2] != 0) {
        fail(env, restoreClosedDisc(*session, previous)
                ? "PS1 core did not confirm the requested disc"
                : "PS1 core did not confirm or restore the requested disc");
        return nullptr;
    }
    return newDiscState(env, after);
}
