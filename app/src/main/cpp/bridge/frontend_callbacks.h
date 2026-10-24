#pragma once

#include <cstddef>
#include <cstdint>

#include "native_session.h"

namespace emuorbit::bridge {

using FrontendErrorHandler = void (*)(const char* message);

/**
 * Binds the single active libretro session to the C callbacks. Calls are made
 * while the bridge lifecycle mutex is held, so the callbacks never outlive the
 * session they reference.
 */
void bindFrontendCallbacks(NativeSession* session, FrontendErrorHandler errorHandler);
void unbindFrontendCallbacks(NativeSession* session);
NativeSession* activeFrontendSession();

bool environmentCallback(unsigned command, void* data);
void videoCallback(const void* data, unsigned width, unsigned height, size_t pitch);
size_t audioCallback(const int16_t* data, size_t frames);

}  // namespace emuorbit::bridge
