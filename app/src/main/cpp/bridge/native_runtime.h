#pragma once

#include <jni.h>

#include <mutex>

#include "native_session.h"

namespace emuorbit::bridge {

std::mutex& nativeCoreMutex();
NativeSession* activeNativeSession();
void setActiveNativeSession(NativeSession* session);

void clearNativeError();
const char* lastNativeError();
void setNativeError(const char* message);

NativeSession* nativeSessionFromHandle(jlong handle);
void closeNativeCore(NativeSession& session);
// Owner thread / nativeCoreMutex only; rejects unadvertised device IDs.
bool selectSessionController(NativeSession& session, unsigned device);
void destroyNativeSession(jlong handle);

}  // namespace emuorbit::bridge
