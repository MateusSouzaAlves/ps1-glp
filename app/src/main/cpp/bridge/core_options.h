#pragma once

#include "native_session.h"

namespace emuorbit::bridge {

bool registerCoreOptionsV2(
        NativeSession& session,
        const retro_core_options_v2* options);
bool readCoreOption(NativeSession& session, retro_variable* variable);
bool writeCoreOption(NativeSession& session, const retro_variable* variable);
bool consumeCoreOptionsUpdated(NativeSession& session);

void setNintendoDsLayoutOptions(
        NativeSession& session,
        int layout,
        int screenGap,
        int focusRatio);
void setNintendoDsInputOptions(
        NativeSession& session,
        int microphoneSource,
        bool showTouchCursor);

}  // namespace emuorbit::bridge
