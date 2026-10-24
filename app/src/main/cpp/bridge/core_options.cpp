#include "core_options.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>

#ifdef EMUORBIT_PROTECTED_DS
#include "protected_ds_symbols.h"
#endif

namespace emuorbit::bridge {
namespace {

// Volatile storage prevents LTO from folding decoded values back into the
// wrapper's plaintext string table.
std::string decodeObfuscated(
        const volatile unsigned char* data,
        size_t length,
        unsigned char key) {
    std::string result(length, '\0');
    for (size_t index = 0; index < length; ++index) {
        result[index] = static_cast<char>(data[index] ^ key);
    }
    return result;
}

bool hasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size()
            && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool applyClassicAudioOptions(NativeSession& session) {
    static volatile unsigned char kFilterSuffix[] = {
            0x34, 0x0A, 0x1E, 0x0F, 0x02, 0x04, 0x34, 0x07,
            0x04, 0x1C, 0x34, 0x1B, 0x0A, 0x18, 0x18, 0x34,
            0x0D, 0x02, 0x07, 0x1F, 0x0E, 0x19};
    static volatile unsigned char kRangeSuffix[] = {
            0x34, 0x0A, 0x1E, 0x0F, 0x02, 0x04, 0x34, 0x07,
            0x04, 0x1C, 0x34, 0x1B, 0x0A, 0x18, 0x18, 0x34,
            0x19, 0x0A, 0x05, 0x0C, 0x0E};
    static volatile unsigned char kEnabled[] = {
            0x0E, 0x05, 0x0A, 0x09, 0x07, 0x0E, 0x0F};
    static volatile unsigned char kDefaultRange[] = {0x5D, 0x5B};
    constexpr unsigned char kKey = 0x6B;
    const std::string filterSuffix = decodeObfuscated(
            kFilterSuffix, sizeof(kFilterSuffix), kKey);
    const std::string rangeSuffix = decodeObfuscated(
            kRangeSuffix, sizeof(kRangeSuffix), kKey);
    bool filterApplied = false;
    bool rangeApplied = false;
    for (auto& option : session.coreOptions) {
        if (hasSuffix(option.first, filterSuffix)) {
            option.second = decodeObfuscated(kEnabled, sizeof(kEnabled), kKey);
            filterApplied = true;
        } else if (hasSuffix(option.first, rangeSuffix)) {
            option.second = decodeObfuscated(
                    kDefaultRange, sizeof(kDefaultRange), kKey);
            rangeApplied = true;
        }
    }
    return filterApplied && rangeApplied;
}

std::string nintendoDsLayoutValue(int layout) {
    static volatile unsigned char kTopBottom[] = {
            0x3D, 0x26, 0x39, 0x64, 0x2B, 0x26, 0x3D, 0x3D, 0x26, 0x24};
    static volatile unsigned char kBottomTop[] = {
            0x30, 0x3D, 0x26, 0x26, 0x3D, 0x3F, 0x7F, 0x26, 0x3D, 0x22};
    static volatile unsigned char kLeftRight[] = {
            0x5B, 0x52, 0x51, 0x43, 0x1A, 0x45, 0x5E, 0x50, 0x5F, 0x43};
    static volatile unsigned char kRightLeft[] = {
            0x13, 0x08, 0x06, 0x09, 0x15, 0x4C, 0x0D, 0x04, 0x07, 0x15};
    static volatile unsigned char kTopOnly[] = {0x5F, 0x44, 0x5B};
    static volatile unsigned char kBottomOnly[] = {
            0x16, 0x1B, 0x00, 0x00, 0x1B, 0x19};
    static volatile unsigned char kLargeTop[] = {
            0x70, 0x7D, 0x6E, 0x7B, 0x79, 0x6F, 0x7F, 0x6E,
            0x79, 0x79, 0x72, 0x31, 0x68, 0x73, 0x6C};
    static volatile unsigned char kLargeBottom[] = {
            0xCF, 0xC2, 0xD1, 0xC4, 0xC6, 0xD0, 0xC0, 0xD1,
            0xC6, 0xC6, 0xCD, 0x8E, 0xC1, 0xCC, 0xD7, 0xD7,
            0xCC, 0xCE};
    switch (layout) {
    case 0:
        return decodeObfuscated(kTopBottom, sizeof(kTopBottom), 0x49);
    case 1:
        return decodeObfuscated(kBottomTop, sizeof(kBottomTop), 0x52);
    case 2:
        return decodeObfuscated(kLeftRight, sizeof(kLeftRight), 0x37);
    case 3:
        return decodeObfuscated(kRightLeft, sizeof(kRightLeft), 0x61);
    case 4:
        return decodeObfuscated(kTopOnly, sizeof(kTopOnly), 0x2B);
    case 5:
        return decodeObfuscated(kBottomOnly, sizeof(kBottomOnly), 0x74);
    case 13:
        return decodeObfuscated(kLargeTop, sizeof(kLargeTop), 0x1C);
    case 14:
        return decodeObfuscated(kLargeBottom, sizeof(kLargeBottom), 0xA3);
    default:
        return decodeObfuscated(kLeftRight, sizeof(kLeftRight), 0x37);
    }
}

std::string nintendoDsMicrophoneValue(int source) {
    static volatile unsigned char kSilence[] = {
            0x24, 0x3E, 0x3B, 0x32, 0x39, 0x34, 0x32};
    static volatile unsigned char kBlow[] = {0x35, 0x3B, 0x38, 0x20};
    static volatile unsigned char kNoise[] = {0x39, 0x38, 0x3E, 0x24, 0x32};
    static volatile unsigned char kDevice[] = {
            0x3A, 0x3E, 0x34, 0x25, 0x38,
            0x27, 0x3F, 0x38, 0x39, 0x32};
    constexpr unsigned char kKey = 0x57;
    switch (source) {
    case 1:
        return decodeObfuscated(kBlow, sizeof(kBlow), kKey);
    case 2:
        return decodeObfuscated(kNoise, sizeof(kNoise), kKey);
    case 3:
        return decodeObfuscated(kDevice, sizeof(kDevice), kKey);
    case 0:
    default:
        return decodeObfuscated(kSilence, sizeof(kSilence), kKey);
    }
}

void applyNintendoDsLayoutOptions(NativeSession& session) {
    const int layout = session.dsLayout.load(std::memory_order_relaxed);
    const int gap = std::clamp(
            session.dsScreenGap.load(std::memory_order_relaxed), 0, 32);
    const int ratio = std::clamp(
            session.dsFocusRatio.load(std::memory_order_relaxed), 2, 3);
    session.coreOptions[EMUORBIT_DS_OPTION_LAYOUT_COUNT] = "1";
    session.coreOptions[EMUORBIT_DS_OPTION_LAYOUT_1] =
            nintendoDsLayoutValue(layout);
    session.coreOptions[EMUORBIT_DS_OPTION_SCREEN_GAP] = std::to_string(gap);
    session.coreOptions[EMUORBIT_DS_OPTION_HYBRID_RATIO] = std::to_string(ratio);
}

void applyNintendoDsInputOptions(NativeSession& session) {
    static volatile unsigned char kAlways[] = {
            0x36, 0x3B, 0x20, 0x36, 0x2E, 0x24};
    static volatile unsigned char kEnabled[] = {
            0x32, 0x39, 0x36, 0x35, 0x3B, 0x32, 0x33};
    static volatile unsigned char kDisabled[] = {
            0x33, 0x3E, 0x24, 0x36, 0x35, 0x3B, 0x32, 0x33};
    static volatile unsigned char kTouching[] = {
            0x23, 0x38, 0x22, 0x34, 0x3F, 0x3E, 0x39, 0x30};
    static volatile unsigned char kTouch[] = {
            0x23, 0x38, 0x22, 0x34, 0x3F};
    constexpr unsigned char kValueKey = 0x57;
    const int source = std::clamp(
            session.dsMicrophoneSource.load(std::memory_order_relaxed), 0, 3);
    const bool showCursor = session.dsShowTouchCursor.load(std::memory_order_relaxed);
    session.coreOptions[EMUORBIT_DS_OPTION_MIC_INPUT] =
            nintendoDsMicrophoneValue(source);
    session.coreOptions[EMUORBIT_DS_OPTION_MIC_INPUT_ACTIVE] =
            decodeObfuscated(kAlways, sizeof(kAlways), kValueKey);
    session.coreOptions[EMUORBIT_DS_OPTION_SHOW_MIC_STATE] =
            decodeObfuscated(
                    source == 3 ? kEnabled : kDisabled,
                    source == 3 ? sizeof(kEnabled) : sizeof(kDisabled),
                    kValueKey);
    session.coreOptions[EMUORBIT_DS_OPTION_SHOW_CURSOR] =
            decodeObfuscated(
                    showCursor ? kTouching : kDisabled,
                    showCursor ? sizeof(kTouching) : sizeof(kDisabled),
                    kValueKey);
    session.coreOptions[EMUORBIT_DS_OPTION_TOUCH_MODE] =
            decodeObfuscated(kTouch, sizeof(kTouch), kValueKey);
}

}  // namespace

bool registerCoreOptionsV2(
        NativeSession& session,
        const retro_core_options_v2* options) {
    if (options == nullptr || options->definitions == nullptr) {
        return false;
    }
    for (const auto* definition = options->definitions;
         definition->key != nullptr;
         ++definition) {
        const char* defaultValue = definition->default_value;
        if (defaultValue == nullptr && definition->values[0].value != nullptr) {
            defaultValue = definition->values[0].value;
        }
        session.coreOptions[definition->key] = defaultValue == nullptr ? "" : defaultValue;
    }
    if (session.nintendoDs) {
        applyNintendoDsLayoutOptions(session);
        applyNintendoDsInputOptions(session);
        session.coreOptions[EMUORBIT_DS_OPTION_AUDIO_BITDEPTH] = "16bit";
        session.coreOptions[EMUORBIT_DS_OPTION_AUDIO_INTERPOLATION] = "gaussian";
    } else {
#if defined(EMUORBIT_TEST_PROBE)
        session.classicAudioFilterConfigured = applyClassicAudioOptions(session);
#else
        applyClassicAudioOptions(session);
#endif
    }
    return true;
}

bool readCoreOption(NativeSession& session, retro_variable* variable) {
    if (variable == nullptr || variable->key == nullptr) {
        return false;
    }
    const auto option = session.coreOptions.find(variable->key);
    if (option == session.coreOptions.end()) {
        variable->value = nullptr;
        return false;
    }
    variable->value = option->second.c_str();
    return true;
}

bool writeCoreOption(NativeSession& session, const retro_variable* variable) {
    if (variable == nullptr || variable->key == nullptr || variable->value == nullptr) {
        return false;
    }
    session.coreOptions[variable->key] = variable->value;
    return true;
}

bool consumeCoreOptionsUpdated(NativeSession& session) {
    return session.coreOptionsUpdated.exchange(false, std::memory_order_relaxed);
}

void setNintendoDsLayoutOptions(
        NativeSession& session,
        int layout,
        int screenGap,
        int focusRatio) {
    switch (layout) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 13:
    case 14:
        session.dsLayout.store(layout, std::memory_order_relaxed);
        break;
    default:
        session.dsLayout.store(2, std::memory_order_relaxed);
        break;
    }
    session.dsScreenGap.store(std::clamp(screenGap, 0, 32), std::memory_order_relaxed);
    session.dsFocusRatio.store(std::clamp(focusRatio, 2, 3), std::memory_order_relaxed);
    applyNintendoDsLayoutOptions(session);
    session.coreOptionsUpdated.store(true, std::memory_order_relaxed);
}

void setNintendoDsInputOptions(
        NativeSession& session,
        int microphoneSource,
        bool showTouchCursor) {
    session.dsMicrophoneSource.store(
            std::clamp(microphoneSource, 0, 3),
            std::memory_order_relaxed);
    session.dsShowTouchCursor.store(showTouchCursor, std::memory_order_relaxed);
    applyNintendoDsInputOptions(session);
    session.coreOptionsUpdated.store(true, std::memory_order_relaxed);
}

}  // namespace emuorbit::bridge
