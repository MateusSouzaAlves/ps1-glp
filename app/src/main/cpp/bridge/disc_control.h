#pragma once

#include "libretro.h"

namespace emuorbit::bridge {

/** Optional session-owned disk interface. All calls, including registration and
 * reset, must run under the native lifecycle mutex on the core's owner thread.
 * Callback pointers are copied, never a pointer to a core-owned stack struct.
 * Only discs already loaded by the core may be selected here; no arbitrary
 * paths/replacement or persistence. Transaction/rollback belongs to PS1-09. */
class DiscControl {
public:
    bool registerLegacy(const retro_disk_control_callback* callbacks) {
        reset();
        if (callbacks == nullptr) return false;
        callbacks_.set_eject_state = callbacks->set_eject_state;
        callbacks_.get_eject_state = callbacks->get_eject_state;
        callbacks_.get_image_index = callbacks->get_image_index;
        callbacks_.set_image_index = callbacks->set_image_index;
        callbacks_.get_num_images = callbacks->get_num_images;
        callbacks_.replace_image_index = callbacks->replace_image_index;
        callbacks_.add_image_index = callbacks->add_image_index;
        return validate();
    }

    bool registerExtended(const retro_disk_control_ext_callback* callbacks) {
        reset();
        if (callbacks == nullptr) return false;
        callbacks_ = *callbacks;
        extended_ = true;
        return validate();
    }

    bool available() const {
        return callbacks_.set_eject_state && callbacks_.get_eject_state &&
                callbacks_.get_image_index && callbacks_.set_image_index &&
                callbacks_.get_num_images;
    }
    bool extended() const { return available() && extended_; }
    unsigned count() const { return available() ? callbacks_.get_num_images() : 0; }
    bool ejected() const { return available() && callbacks_.get_eject_state(); }
    // count() means "no disc selected", per Libretro, and must not index a list.
    unsigned index() const { return available() ? callbacks_.get_image_index() : 0; }
    bool setEjected(bool ejected) {
        return available() && callbacks_.set_eject_state(ejected) &&
                callbacks_.get_eject_state() == ejected;
    }
    bool select(unsigned index) {
        return available() && index < count() && ejected() &&
                callbacks_.set_image_index(index) && callbacks_.get_image_index() == index;
    }
    // Pre-load hint only; the PS1 owner validates its prepared path/index and
    // verifies the actual list after load. Never a replacement-media API.
    bool setInitial(unsigned index, const char* preparedPath) {
        return extended() && callbacks_.set_initial_image && index < 8
                && preparedPath && preparedPath[0] != '\0'
                && callbacks_.set_initial_image(index, preparedPath);
    }
    void reset() { callbacks_ = {}; extended_ = false; }

private:
    bool validate() {
        if (available()) return true;
        reset();
        return false;
    }
    retro_disk_control_ext_callback callbacks_{};
    bool extended_ = false;
};

}  // namespace emuorbit::bridge
