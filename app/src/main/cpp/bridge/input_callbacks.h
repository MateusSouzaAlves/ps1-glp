#pragma once

#include <cstddef>
#include <cstdint>

#include "native_session.h"

namespace emuorbit::bridge {

void inputPollCallback();
int16_t inputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id);

uint16_t maskForJavaButton(const NativeSession& session, int button);
void setNintendoDsPointer(
        NativeSession& session,
        int frameX,
        int frameY,
        bool pressed);

bool configureFrontendMicrophoneInterface(NativeSession& session, void* data);
void closeFrontendMicrophone(FrontendMicHandle* microphone);
bool canPushFrontendMicrophoneSamples(const NativeSession& session);
size_t pushFrontendMicrophoneSamples(
        NativeSession& session,
        const int16_t* samples,
        size_t count);

}  // namespace emuorbit::bridge
