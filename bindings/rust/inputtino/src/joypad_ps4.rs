use inputtino_sys::{inputtino_joypad_ps4_place_finger, inputtino_joypad_ps4_release_finger};
use std::ffi::{c_int, c_void};
use std::path::PathBuf;

use crate::common::{get_nodes, make_device, DeviceDefinition};
use crate::sys::{
    inputtino_joypad_ps4_create, inputtino_joypad_ps4_destroy, inputtino_joypad_ps4_get_nodes,
    inputtino_joypad_ps4_set_battery, inputtino_joypad_ps4_set_motion, inputtino_joypad_ps4_set_on_led,
    inputtino_joypad_ps4_set_on_rumble, inputtino_joypad_ps4_set_pressed_buttons,
    inputtino_joypad_ps4_set_stick, inputtino_joypad_ps4_set_triggers,
};
use crate::{BatteryState, InputtinoError, JoypadMotionType, JoypadStickPosition};

/// Emulated PlayStation 4's DualShock 4 joypad.
///
/// Unlike the DualSense, the DS4 has no haptics, so games drive its two rumble
/// motors directly — forwarding those gives strong rumble on a streaming
/// client. Adaptive triggers do not exist on this hardware.
pub struct PS4Joypad {
    joypad: *mut crate::sys::InputtinoPS4Joypad,
    on_rumble_fn: *mut c_void,
    on_led_fn: *mut c_void,
}

impl PS4Joypad {
    pub const TOUCHPAD_WIDTH: i32 = 1920;
    pub const TOUCHPAD_HEIGHT: i32 = 942;

    /// Create a new emulated PS4 DualShock 4 device with the given device definition.
    ///
    /// # Examples
    ///
    /// ```
    /// let definition = inputtino::DeviceDefinition::new(
    ///     "Inputtino PS4 controller",
    ///     0x054C,
    ///     0x05C4,
    ///     0x8111,
    ///     "00:11:22:33:44",
    ///     "00:11:22:33:44",
    /// );
    /// let device = inputtino::PS4Joypad::new(&definition);
    /// ```
    pub fn new(device: &DeviceDefinition) -> Result<Self, InputtinoError> {
        make_device(inputtino_joypad_ps4_create, device).map(|joypad| PS4Joypad {
            joypad,
            on_rumble_fn: std::ptr::null_mut(),
            on_led_fn: std::ptr::null_mut(),
        })
    }

    /// Set the state of all buttons.
    ///
    /// Any buttons that are not set are released if they were set before.
    pub fn set_pressed(&self, buttons: i32) {
        unsafe {
            inputtino_joypad_ps4_set_pressed_buttons(self.joypad, buttons);
        }
    }

    /// Set the state of the triggers.
    pub fn set_triggers(&self, left_trigger: i16, right_trigger: i16) {
        unsafe {
            inputtino_joypad_ps4_set_triggers(self.joypad, left_trigger, right_trigger);
        }
    }

    /// Set the state of the joysticks.
    pub fn set_stick(&self, stick_type: JoypadStickPosition, x: i16, y: i16) {
        unsafe {
            inputtino_joypad_ps4_set_stick(self.joypad, stick_type, x, y);
        }
    }

    /// Sets a callback to be called when this device receives a rumble event.
    pub fn set_on_rumble(&mut self, on_rumble_fn: impl FnMut(i32, i32) + 'static) {
        let on_rumble_fn = Box::new(RumbleFunction {
            on_rumble_fn: Box::new(on_rumble_fn),
        });
        self.on_rumble_fn = Box::into_raw(on_rumble_fn) as *mut c_void;
        unsafe {
            inputtino_joypad_ps4_set_on_rumble(self.joypad, Some(on_rumble_c_fn), self.on_rumble_fn);
        }
    }

    /// Sets a callback to be called when this device receives a LED change event.
    pub fn set_on_led(&mut self, on_led_fn: impl FnMut(i32, i32, i32) + 'static) {
        let on_led_fn = Box::new(LedFunction {
            on_led_fn: Box::new(on_led_fn),
        });
        self.on_led_fn = Box::into_raw(on_led_fn) as *mut c_void;
        unsafe {
            inputtino_joypad_ps4_set_on_led(self.joypad, Some(on_led_c_fn), self.on_led_fn);
        }
    }

    pub fn get_nodes(&self) -> Result<Vec<PathBuf>, InputtinoError> {
        get_nodes(inputtino_joypad_ps4_get_nodes, self.joypad)
    }

    /// Simulates placement of a finger on the touchpad.
    pub fn place_finger(&self, finger_id: u32, x: u16, y: u16) {
        unsafe {
            inputtino_joypad_ps4_place_finger(self.joypad, finger_id as i32, x, y);
        }
    }

    /// Simulates releasing of a finger from the touchpad.
    pub fn release_finger(&self, finger_id: u32) {
        unsafe {
            inputtino_joypad_ps4_release_finger(self.joypad, finger_id as i32);
        }
    }

    /// Sets the state of the gyro or acceleration sensors.
    pub fn set_motion(&self, motion_type: JoypadMotionType, x: f32, y: f32, z: f32) {
        unsafe {
            inputtino_joypad_ps4_set_motion(self.joypad, motion_type, x, y, z);
        }
    }

    /// Sets the state of the battery.
    pub fn set_battery(&self, battery_state: BatteryState, level: u8) {
        unsafe {
            inputtino_joypad_ps4_set_battery(self.joypad, battery_state, level as u16);
        }
    }
}

impl Drop for PS4Joypad {
    fn drop(&mut self) {
        unsafe {
            inputtino_joypad_ps4_destroy(self.joypad);
            if !self.on_rumble_fn.is_null() {
                drop(Box::from_raw(self.on_rumble_fn as *mut RumbleFunction));
            }
            if !self.on_led_fn.is_null() {
                drop(Box::from_raw(self.on_led_fn as *mut LedFunction));
            }
        }
    }
}

struct RumbleFunction {
    on_rumble_fn: Box<dyn FnMut(i32, i32)>,
}

unsafe extern "C" fn on_rumble_c_fn(
    left_motor: c_int,
    right_motor: c_int,
    user_data: *mut ::core::ffi::c_void,
) {
    let on_rumble_fn = user_data as *mut RumbleFunction;
    ((*on_rumble_fn).on_rumble_fn)(left_motor, right_motor);
}

struct LedFunction {
    on_led_fn: Box<dyn FnMut(i32, i32, i32)>,
}

unsafe extern "C" fn on_led_c_fn(r: c_int, g: c_int, b: c_int, user_data: *mut ::core::ffi::c_void) {
    let on_led_fn = user_data as *mut LedFunction;
    ((*on_led_fn).on_led_fn)(r, g, b);
}

unsafe impl Send for PS4Joypad {}
