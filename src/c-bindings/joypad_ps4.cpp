#include "helpers.hpp"
#include <inputtino/input.h>

#ifdef INPUTTINO_HAS_UHID

InputtinoPS4Joypad *inputtino_joypad_ps4_create(const InputtinoDeviceDefinition *device,
                                                const InputtinoErrorHandler *eh) {
  auto joypad_ = inputtino::PS4Joypad::create({
      .name = device->name ? device->name : "Inputtino virtual device",
      .vendor_id = device->vendor_id,
      .product_id = device->product_id,
      .version = device->version,
      .device_phys = device->device_phys ? device->device_phys : "00:11:22:33:44:55",
      .device_uniq = device->device_uniq ? device->device_uniq : "00:11:22:33:44:55",
  });
  if (joypad_) {
    return reinterpret_cast<InputtinoPS4Joypad *>(new inputtino::PS4Joypad(std::move(*joypad_)));
  } else {
    eh->eh(joypad_.getErrorMessage().c_str(), eh->user_data);
    return nullptr;
  }
}

char **inputtino_joypad_ps4_get_nodes(InputtinoPS4Joypad *joypad, int *num_nodes) {
  return c_get_nodes(joypad, num_nodes);
}

void inputtino_joypad_ps4_set_pressed_buttons(InputtinoPS4Joypad *joypad, int newly_pressed) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_pressed_buttons(newly_pressed);
  }
}

void inputtino_joypad_ps4_set_triggers(InputtinoPS4Joypad *joypad, short left_trigger, short right_trigger) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_triggers(left_trigger, right_trigger);
  }
}

void inputtino_joypad_ps4_set_stick(InputtinoPS4Joypad *joypad,
                                    enum INPUTTINO_JOYPAD_STICK_POSITION stick_type,
                                    short x,
                                    short y) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_stick(inputtino::Joypad::STICK_POSITION(stick_type), x, y);
  }
}

void inputtino_joypad_ps4_set_on_rumble(InputtinoPS4Joypad *joypad,
                                        InputtinoJoypadRumbleFn rumble_fn,
                                        void *user_data) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_on_rumble(
        [user_data, rumble_fn](short left, short right) { rumble_fn(left, right, user_data); });
  }
}

void inputtino_joypad_ps4_place_finger(InputtinoPS4Joypad *joypad, int finger_nr, unsigned short x, unsigned short y) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->place_finger(finger_nr, x, y);
  }
}

void inputtino_joypad_ps4_release_finger(InputtinoPS4Joypad *joypad, int finger_nr) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->release_finger(finger_nr);
  }
}

void inputtino_joypad_ps4_set_motion(
    InputtinoPS4Joypad *joypad, enum INPUTTINO_JOYPAD_MOTION_TYPE motion_type, float x, float y, float z) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_motion(inputtino::PS4Joypad::MOTION_TYPE(motion_type),
                                                                 x,
                                                                 y,
                                                                 z);
  }
}

void inputtino_joypad_ps4_set_battery(InputtinoPS4Joypad *joypad,
                                      enum BATTERY_STATE battery_state,
                                      unsigned short level) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_battery(inputtino::PS4Joypad::BATTERY_STATE(battery_state),
                                                                  level);
  }
}

void inputtino_joypad_ps4_set_on_led(InputtinoPS4Joypad *joypad, InputtinoJoypadLEDFn led_fn, void *user_data) {
  if (joypad) {
    reinterpret_cast<inputtino::PS4Joypad *>(joypad)->set_on_led(
        [user_data, led_fn](unsigned char r, unsigned char g, unsigned char b) { led_fn(r, g, b, user_data); });
  }
}

void inputtino_joypad_ps4_destroy(InputtinoPS4Joypad *joypad) {
  if (joypad) {
    auto joypad_ptr = reinterpret_cast<inputtino::PS4Joypad *>(joypad);
    delete joypad_ptr;
  }
}

#else // !INPUTTINO_HAS_UHID: DS4 emulation requires uhid; provide stubs so the
      // symbols exist when building the uinput-only variant.

InputtinoPS4Joypad *inputtino_joypad_ps4_create(const InputtinoDeviceDefinition *, const InputtinoErrorHandler *eh) {
  eh->eh("DualShock 4 emulation requires the uhid implementation (USE_UHID)", eh->user_data);
  return nullptr;
}
char **inputtino_joypad_ps4_get_nodes(InputtinoPS4Joypad *, int *num_nodes) {
  *num_nodes = 0;
  return nullptr;
}
void inputtino_joypad_ps4_set_pressed_buttons(InputtinoPS4Joypad *, int) {}
void inputtino_joypad_ps4_set_triggers(InputtinoPS4Joypad *, short, short) {}
void inputtino_joypad_ps4_set_stick(InputtinoPS4Joypad *, enum INPUTTINO_JOYPAD_STICK_POSITION, short, short) {}
void inputtino_joypad_ps4_set_on_rumble(InputtinoPS4Joypad *, InputtinoJoypadRumbleFn, void *) {}
void inputtino_joypad_ps4_place_finger(InputtinoPS4Joypad *, int, unsigned short, unsigned short) {}
void inputtino_joypad_ps4_release_finger(InputtinoPS4Joypad *, int) {}
void inputtino_joypad_ps4_set_motion(InputtinoPS4Joypad *, enum INPUTTINO_JOYPAD_MOTION_TYPE, float, float, float) {}
void inputtino_joypad_ps4_set_battery(InputtinoPS4Joypad *, enum BATTERY_STATE, unsigned short) {}
void inputtino_joypad_ps4_set_on_led(InputtinoPS4Joypad *, InputtinoJoypadLEDFn, void *) {}
void inputtino_joypad_ps4_destroy(InputtinoPS4Joypad *) {}

#endif
