use std::fmt;
use std::str::FromStr;

/// Standard Key enum representing keys from the USB HID Keyboard/Keypad Usage Page (0x07).
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub enum Key {
    // Letters (0x04 - 0x1D)
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    // Digits (0x1E - 0x27)
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Num0,

    // Controls and Editing (0x28 - 0x2C)
    Return,
    Escape,
    Backspace,
    Tab,
    Space,

    // Punctuation and Symbols (0x2D - 0x38)
    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Backslash,
    NonUsHash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,

    // Locks and Functions (0x39 - 0x45)
    CapsLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    // Navigation and Editing (0x46 - 0x52)
    PrintScreen,
    ScrollLock,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    RightArrow,
    LeftArrow,
    DownArrow,
    UpArrow,

    // Keypad (0x53 - 0x63)
    KeypadNumLock,
    KeypadSlash,
    KeypadAsterisk,
    KeypadMinus,
    KeypadPlus,
    KeypadEnter,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    Keypad0,
    KeypadPeriod,

    // Additional Standard Keys
    NonUsBackslash,
    Application,
    Power,
    KeypadEqual,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    // Modifiers (0xE0 - 0xE7)
    LeftCtrl,
    LeftShift,
    LeftAlt,
    LeftGui,
    RightCtrl,
    RightShift,
    RightAlt,
    RightGui,

    // Unmapped / Unknown keycode
    Unknown(u8),
}

impl Key {
    /// Convert a USB HID page 0x07 keycode into a `Key` enum variant.
    pub fn from_keycode(keycode: u8) -> Self {
        match keycode {
            0x04 => Key::A,
            0x05 => Key::B,
            0x06 => Key::C,
            0x07 => Key::D,
            0x08 => Key::E,
            0x09 => Key::F,
            0x0A => Key::G,
            0x0B => Key::H,
            0x0C => Key::I,
            0x0D => Key::J,
            0x0E => Key::K,
            0x0F => Key::L,
            0x10 => Key::M,
            0x11 => Key::N,
            0x12 => Key::O,
            0x13 => Key::P,
            0x14 => Key::Q,
            0x15 => Key::R,
            0x16 => Key::S,
            0x17 => Key::T,
            0x18 => Key::U,
            0x19 => Key::V,
            0x1A => Key::W,
            0x1B => Key::X,
            0x1C => Key::Y,
            0x1D => Key::Z,

            0x1E => Key::Num1,
            0x1F => Key::Num2,
            0x20 => Key::Num3,
            0x21 => Key::Num4,
            0x22 => Key::Num5,
            0x23 => Key::Num6,
            0x24 => Key::Num7,
            0x25 => Key::Num8,
            0x26 => Key::Num9,
            0x27 => Key::Num0,

            0x28 => Key::Return,
            0x29 => Key::Escape,
            0x2A => Key::Backspace,
            0x2B => Key::Tab,
            0x2C => Key::Space,

            0x2D => Key::Minus,
            0x2E => Key::Equal,
            0x2F => Key::LeftBracket,
            0x30 => Key::RightBracket,
            0x31 => Key::Backslash,
            0x32 => Key::NonUsHash,
            0x33 => Key::Semicolon,
            0x34 => Key::Apostrophe,
            0x35 => Key::Grave,
            0x36 => Key::Comma,
            0x37 => Key::Period,
            0x38 => Key::Slash,

            0x39 => Key::CapsLock,
            0x3A => Key::F1,
            0x3B => Key::F2,
            0x3C => Key::F3,
            0x3D => Key::F4,
            0x3E => Key::F5,
            0x3F => Key::F6,
            0x40 => Key::F7,
            0x41 => Key::F8,
            0x42 => Key::F9,
            0x43 => Key::F10,
            0x44 => Key::F11,
            0x45 => Key::F12,

            0x46 => Key::PrintScreen,
            0x47 => Key::ScrollLock,
            0x48 => Key::Pause,
            0x49 => Key::Insert,
            0x4A => Key::Home,
            0x4B => Key::PageUp,
            0x4C => Key::Delete,
            0x4D => Key::End,
            0x4E => Key::PageDown,
            0x4F => Key::RightArrow,
            0x50 => Key::LeftArrow,
            0x51 => Key::DownArrow,
            0x52 => Key::UpArrow,

            0x53 => Key::KeypadNumLock,
            0x54 => Key::KeypadSlash,
            0x55 => Key::KeypadAsterisk,
            0x56 => Key::KeypadMinus,
            0x57 => Key::KeypadPlus,
            0x58 => Key::KeypadEnter,
            0x59 => Key::Keypad1,
            0x5A => Key::Keypad2,
            0x5B => Key::Keypad3,
            0x5C => Key::Keypad4,
            0x5D => Key::Keypad5,
            0x5E => Key::Keypad6,
            0x5F => Key::Keypad7,
            0x60 => Key::Keypad8,
            0x61 => Key::Keypad9,
            0x62 => Key::Keypad0,
            0x63 => Key::KeypadPeriod,

            0x64 => Key::NonUsBackslash,
            0x65 => Key::Application,
            0x66 => Key::Power,
            0x67 => Key::KeypadEqual,
            0x68 => Key::F13,
            0x69 => Key::F14,
            0x6A => Key::F15,
            0x6B => Key::F16,
            0x6C => Key::F17,
            0x6D => Key::F18,
            0x6E => Key::F19,
            0x6F => Key::F20,
            0x70 => Key::F21,
            0x71 => Key::F22,
            0x72 => Key::F23,
            0x73 => Key::F24,

            0xE0 => Key::LeftCtrl,
            0xE1 => Key::LeftShift,
            0xE2 => Key::LeftAlt,
            0xE3 => Key::LeftGui,
            0xE4 => Key::RightCtrl,
            0xE5 => Key::RightShift,
            0xE6 => Key::RightAlt,
            0xE7 => Key::RightGui,

            other => Key::Unknown(other),
        }
    }

    /// Return the corresponding USB HID page 0x07 keycode for this key.
    pub fn keycode(&self) -> u8 {
        match *self {
            Key::A => 0x04,
            Key::B => 0x05,
            Key::C => 0x06,
            Key::D => 0x07,
            Key::E => 0x08,
            Key::F => 0x09,
            Key::G => 0x0A,
            Key::H => 0x0B,
            Key::I => 0x0C,
            Key::J => 0x0D,
            Key::K => 0x0E,
            Key::L => 0x0F,
            Key::M => 0x10,
            Key::N => 0x11,
            Key::O => 0x12,
            Key::P => 0x13,
            Key::Q => 0x14,
            Key::R => 0x15,
            Key::S => 0x16,
            Key::T => 0x17,
            Key::U => 0x18,
            Key::V => 0x19,
            Key::W => 0x1A,
            Key::X => 0x1B,
            Key::Y => 0x1C,
            Key::Z => 0x1D,

            Key::Num1 => 0x1E,
            Key::Num2 => 0x1F,
            Key::Num3 => 0x20,
            Key::Num4 => 0x21,
            Key::Num5 => 0x22,
            Key::Num6 => 0x23,
            Key::Num7 => 0x24,
            Key::Num8 => 0x25,
            Key::Num9 => 0x26,
            Key::Num0 => 0x27,

            Key::Return => 0x28,
            Key::Escape => 0x29,
            Key::Backspace => 0x2A,
            Key::Tab => 0x2B,
            Key::Space => 0x2C,

            Key::Minus => 0x2D,
            Key::Equal => 0x2E,
            Key::LeftBracket => 0x2F,
            Key::RightBracket => 0x30,
            Key::Backslash => 0x31,
            Key::NonUsHash => 0x32,
            Key::Semicolon => 0x33,
            Key::Apostrophe => 0x34,
            Key::Grave => 0x35,
            Key::Comma => 0x36,
            Key::Period => 0x37,
            Key::Slash => 0x38,

            Key::CapsLock => 0x39,
            Key::F1 => 0x3A,
            Key::F2 => 0x3B,
            Key::F3 => 0x3C,
            Key::F4 => 0x3D,
            Key::F5 => 0x3E,
            Key::F6 => 0x3F,
            Key::F7 => 0x40,
            Key::F8 => 0x41,
            Key::F9 => 0x42,
            Key::F10 => 0x43,
            Key::F11 => 0x44,
            Key::F12 => 0x45,

            Key::PrintScreen => 0x46,
            Key::ScrollLock => 0x47,
            Key::Pause => 0x48,
            Key::Insert => 0x49,
            Key::Home => 0x4A,
            Key::PageUp => 0x4B,
            Key::Delete => 0x4C,
            Key::End => 0x4D,
            Key::PageDown => 0x4E,
            Key::RightArrow => 0x4F,
            Key::LeftArrow => 0x50,
            Key::DownArrow => 0x51,
            Key::UpArrow => 0x52,

            Key::KeypadNumLock => 0x53,
            Key::KeypadSlash => 0x54,
            Key::KeypadAsterisk => 0x55,
            Key::KeypadMinus => 0x56,
            Key::KeypadPlus => 0x57,
            Key::KeypadEnter => 0x58,
            Key::Keypad1 => 0x59,
            Key::Keypad2 => 0x5A,
            Key::Keypad3 => 0x5B,
            Key::Keypad4 => 0x5C,
            Key::Keypad5 => 0x5D,
            Key::Keypad6 => 0x5E,
            Key::Keypad7 => 0x5F,
            Key::Keypad8 => 0x60,
            Key::Keypad9 => 0x61,
            Key::Keypad0 => 0x62,
            Key::KeypadPeriod => 0x63,

            Key::NonUsBackslash => 0x64,
            Key::Application => 0x65,
            Key::Power => 0x66,
            Key::KeypadEqual => 0x67,
            Key::F13 => 0x68,
            Key::F14 => 0x69,
            Key::F15 => 0x6A,
            Key::F16 => 0x6B,
            Key::F17 => 0x6C,
            Key::F18 => 0x6D,
            Key::F19 => 0x6E,
            Key::F20 => 0x6F,
            Key::F21 => 0x70,
            Key::F22 => 0x71,
            Key::F23 => 0x72,
            Key::F24 => 0x73,

            Key::LeftCtrl => 0xE0,
            Key::LeftShift => 0xE1,
            Key::LeftAlt => 0xE2,
            Key::LeftGui => 0xE3,
            Key::RightCtrl => 0xE4,
            Key::RightShift => 0xE5,
            Key::RightAlt => 0xE6,
            Key::RightGui => 0xE7,

            Key::Unknown(code) => code,
        }
    }
}

impl fmt::Display for Key {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl FromStr for Key {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if let Some(rest) = s.strip_prefix("Unknown(") {
            if let Some(inner) = rest.strip_suffix(')') {
                if let Ok(code) = inner.parse::<u8>() {
                    return Ok(Key::Unknown(code));
                }
            }
        }
        match s {
            "A" => Ok(Key::A),
            "B" => Ok(Key::B),
            "C" => Ok(Key::C),
            "D" => Ok(Key::D),
            "E" => Ok(Key::E),
            "F" => Ok(Key::F),
            "G" => Ok(Key::G),
            "H" => Ok(Key::H),
            "I" => Ok(Key::I),
            "J" => Ok(Key::J),
            "K" => Ok(Key::K),
            "L" => Ok(Key::L),
            "M" => Ok(Key::M),
            "N" => Ok(Key::N),
            "O" => Ok(Key::O),
            "P" => Ok(Key::P),
            "Q" => Ok(Key::Q),
            "R" => Ok(Key::R),
            "S" => Ok(Key::S),
            "T" => Ok(Key::T),
            "U" => Ok(Key::U),
            "V" => Ok(Key::V),
            "W" => Ok(Key::W),
            "X" => Ok(Key::X),
            "Y" => Ok(Key::Y),
            "Z" => Ok(Key::Z),

            "Num1" => Ok(Key::Num1),
            "Num2" => Ok(Key::Num2),
            "Num3" => Ok(Key::Num3),
            "Num4" => Ok(Key::Num4),
            "Num5" => Ok(Key::Num5),
            "Num6" => Ok(Key::Num6),
            "Num7" => Ok(Key::Num7),
            "Num8" => Ok(Key::Num8),
            "Num9" => Ok(Key::Num9),
            "Num0" => Ok(Key::Num0),

            "Return" => Ok(Key::Return),
            "Escape" => Ok(Key::Escape),
            "Backspace" => Ok(Key::Backspace),
            "Tab" => Ok(Key::Tab),
            "Space" => Ok(Key::Space),

            "Minus" => Ok(Key::Minus),
            "Equal" => Ok(Key::Equal),
            "LeftBracket" => Ok(Key::LeftBracket),
            "RightBracket" => Ok(Key::RightBracket),
            "Backslash" => Ok(Key::Backslash),
            "NonUsHash" => Ok(Key::NonUsHash),
            "Semicolon" => Ok(Key::Semicolon),
            "Apostrophe" => Ok(Key::Apostrophe),
            "Grave" => Ok(Key::Grave),
            "Comma" => Ok(Key::Comma),
            "Period" => Ok(Key::Period),
            "Slash" => Ok(Key::Slash),

            "CapsLock" => Ok(Key::CapsLock),
            "F1" => Ok(Key::F1),
            "F2" => Ok(Key::F2),
            "F3" => Ok(Key::F3),
            "F4" => Ok(Key::F4),
            "F5" => Ok(Key::F5),
            "F6" => Ok(Key::F6),
            "F7" => Ok(Key::F7),
            "F8" => Ok(Key::F8),
            "F9" => Ok(Key::F9),
            "F10" => Ok(Key::F10),
            "F11" => Ok(Key::F11),
            "F12" => Ok(Key::F12),

            "PrintScreen" => Ok(Key::PrintScreen),
            "ScrollLock" => Ok(Key::ScrollLock),
            "Pause" => Ok(Key::Pause),
            "Insert" => Ok(Key::Insert),
            "Home" => Ok(Key::Home),
            "PageUp" => Ok(Key::PageUp),
            "Delete" => Ok(Key::Delete),
            "End" => Ok(Key::End),
            "PageDown" => Ok(Key::PageDown),
            "RightArrow" => Ok(Key::RightArrow),
            "LeftArrow" => Ok(Key::LeftArrow),
            "DownArrow" => Ok(Key::DownArrow),
            "UpArrow" => Ok(Key::UpArrow),

            "KeypadNumLock" => Ok(Key::KeypadNumLock),
            "KeypadSlash" => Ok(Key::KeypadSlash),
            "KeypadAsterisk" => Ok(Key::KeypadAsterisk),
            "KeypadMinus" => Ok(Key::KeypadMinus),
            "KeypadPlus" => Ok(Key::KeypadPlus),
            "KeypadEnter" => Ok(Key::KeypadEnter),
            "Keypad1" => Ok(Key::Keypad1),
            "Keypad2" => Ok(Key::Keypad2),
            "Keypad3" => Ok(Key::Keypad3),
            "Keypad4" => Ok(Key::Keypad4),
            "Keypad5" => Ok(Key::Keypad5),
            "Keypad6" => Ok(Key::Keypad6),
            "Keypad7" => Ok(Key::Keypad7),
            "Keypad8" => Ok(Key::Keypad8),
            "Keypad9" => Ok(Key::Keypad9),
            "Keypad0" => Ok(Key::Keypad0),
            "KeypadPeriod" => Ok(Key::KeypadPeriod),

            "NonUsBackslash" => Ok(Key::NonUsBackslash),
            "Application" => Ok(Key::Application),
            "Power" => Ok(Key::Power),
            "KeypadEqual" => Ok(Key::KeypadEqual),
            "F13" => Ok(Key::F13),
            "F14" => Ok(Key::F14),
            "F15" => Ok(Key::F15),
            "F16" => Ok(Key::F16),
            "F17" => Ok(Key::F17),
            "F18" => Ok(Key::F18),
            "F19" => Ok(Key::F19),
            "F20" => Ok(Key::F20),
            "F21" => Ok(Key::F21),
            "F22" => Ok(Key::F22),
            "F23" => Ok(Key::F23),
            "F24" => Ok(Key::F24),

            "LeftCtrl" => Ok(Key::LeftCtrl),
            "LeftShift" => Ok(Key::LeftShift),
            "LeftAlt" => Ok(Key::LeftAlt),
            "LeftGui" => Ok(Key::LeftGui),
            "RightCtrl" => Ok(Key::RightCtrl),
            "RightShift" => Ok(Key::RightShift),
            "RightAlt" => Ok(Key::RightAlt),
            "RightGui" => Ok(Key::RightGui),

            _ => Err(()),
        }
    }
}

/// Convert a USB HID keycode to its corresponding ASCII character given the Shift modifier state.
pub fn keycode_to_char(keycode: u8, shift: bool) -> Option<char> {
    match keycode {
        0x04..=0x1D => {
            let offset = keycode - 0x04;
            if shift {
                Some((b'A' + offset) as char)
            } else {
                Some((b'a' + offset) as char)
            }
        }
        0x1E => Some(if shift { '!' } else { '1' }),
        0x1F => Some(if shift { '@' } else { '2' }),
        0x20 => Some(if shift { '#' } else { '3' }),
        0x21 => Some(if shift { '$' } else { '4' }),
        0x22 => Some(if shift { '%' } else { '5' }),
        0x23 => Some(if shift { '^' } else { '6' }),
        0x24 => Some(if shift { '&' } else { '7' }),
        0x25 => Some(if shift { '*' } else { '8' }),
        0x26 => Some(if shift { '(' } else { '9' }),
        0x27 => Some(if shift { ')' } else { '0' }),

        0x28 => Some('\n'),   // Return
        0x2A => Some('\x08'), // Backspace
        0x2B => Some('\t'),   // Tab
        0x2C => Some(' '),    // Space

        0x2D => Some(if shift { '_' } else { '-' }),
        0x2E => Some(if shift { '+' } else { '=' }),
        0x2F => Some(if shift { '{' } else { '[' }),
        0x30 => Some(if shift { '}' } else { ']' }),
        0x31 => Some(if shift { '|' } else { '\\' }),
        0x32 => Some(if shift { '~' } else { '#' }),
        0x33 => Some(if shift { ':' } else { ';' }),
        0x34 => Some(if shift { '"' } else { '\'' }),
        0x35 => Some(if shift { '~' } else { '`' }),
        0x36 => Some(if shift { '<' } else { ',' }),
        0x37 => Some(if shift { '>' } else { '.' }),
        0x38 => Some(if shift { '?' } else { '/' }),

        0x54 => Some('/'),
        0x55 => Some('*'),
        0x56 => Some('-'),
        0x57 => Some('+'),
        0x58 => Some('\n'),
        0x59 => Some('1'),
        0x5A => Some('2'),
        0x5B => Some('3'),
        0x5C => Some('4'),
        0x5D => Some('5'),
        0x5E => Some('6'),
        0x5F => Some('7'),
        0x60 => Some('8'),
        0x61 => Some('9'),
        0x62 => Some('0'),
        0x63 => Some('.'),

        _ => None,
    }
}

/// Modifier state tracker for keyboard modifiers.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ModifierTracker {
    pub left_ctrl: bool,
    pub left_shift: bool,
    pub left_alt: bool,
    pub left_gui: bool,
    pub right_ctrl: bool,
    pub right_shift: bool,
    pub right_alt: bool,
    pub right_gui: bool,
}

impl ModifierTracker {
    pub fn new() -> Self {
        Self::default()
    }

    /// Whether either Left Shift or Right Shift is currently pressed.
    pub fn shift(&self) -> bool {
        self.left_shift || self.right_shift
    }

    /// Whether either Left Ctrl or Right Ctrl is currently pressed.
    pub fn ctrl(&self) -> bool {
        self.left_ctrl || self.right_ctrl
    }

    /// Whether either Left Alt or Right Alt is currently pressed.
    pub fn alt(&self) -> bool {
        self.left_alt || self.right_alt
    }

    /// Whether either Left GUI or Right GUI is currently pressed.
    pub fn gui(&self) -> bool {
        self.left_gui || self.right_gui
    }

    /// Check whether a keycode is a modifier (0xE0..=0xE7).
    pub fn is_modifier(keycode: u8) -> bool {
        (0xE0..=0xE7).contains(&keycode)
    }

    /// Update modifier state if the given keycode is a modifier.
    /// Returns `true` if the keycode was a modifier and updated state.
    pub fn update(&mut self, keycode: u8, pressed: bool) -> bool {
        match keycode {
            0xE0 => {
                self.left_ctrl = pressed;
                true
            }
            0xE1 => {
                self.left_shift = pressed;
                true
            }
            0xE2 => {
                self.left_alt = pressed;
                true
            }
            0xE3 => {
                self.left_gui = pressed;
                true
            }
            0xE4 => {
                self.right_ctrl = pressed;
                true
            }
            0xE5 => {
                self.right_shift = pressed;
                true
            }
            0xE6 => {
                self.right_alt = pressed;
                true
            }
            0xE7 => {
                self.right_gui = pressed;
                true
            }
            _ => false,
        }
    }
}

/// High-level event produced by decoded HID report packets.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HidEvent {
    KeyDown {
        keycode: u8,
        key: Key,
        character: Option<char>,
    },
    KeyUp {
        keycode: u8,
        key: Key,
    },
    MouseMove {
        dx: i32,
        dy: i32,
    },
    MouseButton {
        button: u8,
        pressed: bool,
    },
    MouseWheel {
        delta: i32,
    },
}

impl HidEvent {
    /// Format event into a single text line suitable for FIFOs / unix pipes.
    pub fn to_line(&self) -> String {
        match *self {
            HidEvent::KeyDown {
                keycode,
                key,
                character,
            } => {
                let char_repr = match character {
                    Some(c) => format!("{:04X}", c as u32),
                    None => "-".to_string(),
                };
                format!("KEY_DOWN {} {} {}", keycode, key, char_repr)
            }
            HidEvent::KeyUp { keycode, key } => {
                format!("KEY_UP {} {}", keycode, key)
            }
            HidEvent::MouseMove { dx, dy } => {
                format!("MOUSE_MOVE {} {}", dx, dy)
            }
            HidEvent::MouseButton { button, pressed } => {
                format!("MOUSE_BUTTON {} {}", button, if pressed { 1 } else { 0 })
            }
            HidEvent::MouseWheel { delta } => {
                format!("MOUSE_WHEEL {}", delta)
            }
        }
    }

    /// Parse an event from a text line.
    pub fn from_line(line: &str) -> Result<Self, String> {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.is_empty() {
            return Err("Empty event line".to_string());
        }

        match parts[0] {
            "KEY_DOWN" => {
                if parts.len() < 4 {
                    return Err(format!("Invalid KEY_DOWN format: {}", line));
                }
                let keycode = parts[1]
                    .parse::<u8>()
                    .map_err(|e| format!("Invalid keycode: {}", e))?;
                let key = Key::from_str(parts[2]).unwrap_or_else(|_| Key::from_keycode(keycode));
                let character = if parts[3] == "-" {
                    None
                } else {
                    let code = u32::from_str_radix(parts[3], 16)
                        .map_err(|e| format!("Invalid character code: {}", e))?;
                    char::from_u32(code)
                };
                Ok(HidEvent::KeyDown {
                    keycode,
                    key,
                    character,
                })
            }
            "KEY_UP" => {
                if parts.len() < 3 {
                    return Err(format!("Invalid KEY_UP format: {}", line));
                }
                let keycode = parts[1]
                    .parse::<u8>()
                    .map_err(|e| format!("Invalid keycode: {}", e))?;
                let key = Key::from_str(parts[2]).unwrap_or_else(|_| Key::from_keycode(keycode));
                Ok(HidEvent::KeyUp { keycode, key })
            }
            "MOUSE_MOVE" => {
                if parts.len() < 3 {
                    return Err(format!("Invalid MOUSE_MOVE format: {}", line));
                }
                let dx = parts[1]
                    .parse::<i32>()
                    .map_err(|e| format!("Invalid dx: {}", e))?;
                let dy = parts[2]
                    .parse::<i32>()
                    .map_err(|e| format!("Invalid dy: {}", e))?;
                Ok(HidEvent::MouseMove { dx, dy })
            }
            "MOUSE_BUTTON" => {
                if parts.len() < 3 {
                    return Err(format!("Invalid MOUSE_BUTTON format: {}", line));
                }
                let button = parts[1]
                    .parse::<u8>()
                    .map_err(|e| format!("Invalid button: {}", e))?;
                let pressed = match parts[2] {
                    "1" | "true" => true,
                    "0" | "false" => false,
                    other => return Err(format!("Invalid pressed state: {}", other)),
                };
                Ok(HidEvent::MouseButton { button, pressed })
            }
            "MOUSE_WHEEL" => {
                if parts.len() < 2 {
                    return Err(format!("Invalid MOUSE_WHEEL format: {}", line));
                }
                let delta = parts[1]
                    .parse::<i32>()
                    .map_err(|e| format!("Invalid wheel delta: {}", e))?;
                Ok(HidEvent::MouseWheel { delta })
            }
            other => Err(format!("Unknown event type: {}", other)),
        }
    }

    /// Encode event into compact binary format.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::new();
        match *self {
            HidEvent::KeyDown {
                keycode,
                key: _,
                character,
            } => {
                buf.push(0x01); // Tag KeyDown
                buf.push(keycode);
                let char_code = character.map(|c| c as u32).unwrap_or(0);
                buf.extend_from_slice(&char_code.to_le_bytes());
            }
            HidEvent::KeyUp { keycode, key: _ } => {
                buf.push(0x02); // Tag KeyUp
                buf.push(keycode);
            }
            HidEvent::MouseMove { dx, dy } => {
                buf.push(0x03); // Tag MouseMove
                buf.extend_from_slice(&dx.to_le_bytes());
                buf.extend_from_slice(&dy.to_le_bytes());
            }
            HidEvent::MouseButton { button, pressed } => {
                buf.push(0x04); // Tag MouseButton
                buf.push(button);
                buf.push(if pressed { 1 } else { 0 });
            }
            HidEvent::MouseWheel { delta } => {
                buf.push(0x05); // Tag MouseWheel
                buf.extend_from_slice(&delta.to_le_bytes());
            }
        }
        buf
    }

    /// Decode event from compact binary format.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, String> {
        if bytes.is_empty() {
            return Err("Empty binary event buffer".to_string());
        }
        match bytes[0] {
            0x01 => {
                // KeyDown: tag(1) + keycode(1) + char(4) = 6 bytes
                if bytes.len() < 6 {
                    return Err("Insufficient bytes for KeyDown".to_string());
                }
                let keycode = bytes[1];
                let key = Key::from_keycode(keycode);
                let char_code = u32::from_le_bytes([bytes[2], bytes[3], bytes[4], bytes[5]]);
                let character = if char_code == 0 {
                    None
                } else {
                    char::from_u32(char_code)
                };
                Ok(HidEvent::KeyDown {
                    keycode,
                    key,
                    character,
                })
            }
            0x02 => {
                // KeyUp: tag(1) + keycode(1) = 2 bytes
                if bytes.len() < 2 {
                    return Err("Insufficient bytes for KeyUp".to_string());
                }
                let keycode = bytes[1];
                let key = Key::from_keycode(keycode);
                Ok(HidEvent::KeyUp { keycode, key })
            }
            0x03 => {
                // MouseMove: tag(1) + dx(4) + dy(4) = 9 bytes
                if bytes.len() < 9 {
                    return Err("Insufficient bytes for MouseMove".to_string());
                }
                let dx = i32::from_le_bytes([bytes[1], bytes[2], bytes[3], bytes[4]]);
                let dy = i32::from_le_bytes([bytes[5], bytes[6], bytes[7], bytes[8]]);
                Ok(HidEvent::MouseMove { dx, dy })
            }
            0x04 => {
                // MouseButton: tag(1) + button(1) + pressed(1) = 3 bytes
                if bytes.len() < 3 {
                    return Err("Insufficient bytes for MouseButton".to_string());
                }
                let button = bytes[1];
                let pressed = bytes[2] != 0;
                Ok(HidEvent::MouseButton { button, pressed })
            }
            0x05 => {
                // MouseWheel: tag(1) + delta(4) = 5 bytes
                if bytes.len() < 5 {
                    return Err("Insufficient bytes for MouseWheel".to_string());
                }
                let delta = i32::from_le_bytes([bytes[1], bytes[2], bytes[3], bytes[4]]);
                Ok(HidEvent::MouseWheel { delta })
            }
            other => Err(format!("Unknown binary event tag: {}", other)),
        }
    }
}

impl fmt::Display for HidEvent {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_line())
    }
}

impl FromStr for HidEvent {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::from_line(s)
    }
}
