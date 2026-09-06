pub mod client;
pub mod decoder;
pub mod descriptor;
pub mod keycodes;

pub use client::{emit_event_to_path, write_event, HidClient, DEFAULT_EVENTS_PATH};
pub use decoder::{read_bits, sign_extend, ReportDecoder};
pub use descriptor::{
    InputGroup, InputReader, ReportDescriptor, ReportDescriptorParser,
    KEYBOARD_REPORT_DESCRIPTOR, MOUSE_REPORT_DESCRIPTOR,
};
pub use keycodes::{keycode_to_char, HidEvent, Key, ModifierTracker};

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    pub const MOUSE_REPORT_DESCRIPTOR: &[u8] = &[
        0x05, 0x01, // Usage Page (Generic Desktop)
        0x09, 0x02, // Usage (Mouse)
        0xA1, 0x01, // Collection (Application)
        0x09, 0x01, //   Usage (Pointer)
        0xA1, 0x00, //   Collection (Physical)
        0x05, 0x09, //     Usage Page (Button)
        0x19, 0x01, //     Usage Minimum (Button 1)
        0x29, 0x03, //     Usage Maximum (Button 3)
        0x15, 0x00, //     Logical Minimum (0)
        0x25, 0x01, //     Logical Maximum (1)
        0x95, 0x03, //     Report Count (3)
        0x75, 0x01, //     Report Size (1)
        0x81, 0x02, //     Input (Data, Variable, Absolute) - 3 button bits
        0x95, 0x01, //     Report Count (1)
        0x75, 0x05, //     Report Size (5)
        0x81, 0x01, //     Input (Constant) - 5 bit padding
        0x05, 0x01, //     Usage Page (Generic Desktop)
        0x09, 0x30, //     Usage (X)
        0x09, 0x31, //     Usage (Y)
        0x15, 0x81, //     Logical Minimum (-127)
        0x25, 0x7F, //     Logical Maximum (127)
        0x75, 0x08, //     Report Size (8)
        0x95, 0x02, //     Report Count (2)
        0x81, 0x06, //     Input (Data, Variable, Relative) - X, Y
        0x09, 0x38, //     Usage (Wheel)
        0x15, 0x81, //     Logical Minimum (-127)
        0x25, 0x7F, //     Logical Maximum (127)
        0x75, 0x08, //     Report Size (8)
        0x95, 0x01, //     Report Count (1)
        0x81, 0x06, //     Input (Data, Variable, Relative) - Wheel
        0xC0,       //   End Collection
        0xC0,       // End Collection
    ];

    pub const KEYBOARD_REPORT_DESCRIPTOR: &[u8] = &[
        0x05, 0x07, // Usage Page (Key Codes / Keyboard)
        0x19, 0xE0, // Usage Minimum (224: Left Control)
        0x29, 0xE7, // Usage Maximum (231: Right GUI)
        0x15, 0x00, // Logical Minimum (0)
        0x25, 0x01, // Logical Maximum (1)
        0x75, 0x01, // Report Size (1)
        0x95, 0x08, // Report Count (8)
        0x81, 0x02, // Input (Data, Variable, Absolute) - Modifier byte
        0x95, 0x01, // Report Count (1)
        0x75, 0x08, // Report Size (8)
        0x81, 0x01, // Input (Constant) - Reserved byte
        0x95, 0x05, // Report Count (5)
        0x75, 0x01, // Report Size (1)
        0x05, 0x08, // Usage Page (LEDs)
        0x19, 0x01, // Usage Minimum (1)
        0x29, 0x05, // Usage Maximum (5)
        0x91, 0x02, // Output (Data, Variable, Absolute) - LED report
        0x95, 0x01, // Report Count (1)
        0x75, 0x03, // Report Size (3)
        0x91, 0x01, // Output (Constant) - LED report padding
        0x95, 0x06, // Report Count (6)
        0x75, 0x08, // Report Size (8)
        0x15, 0x00, // Logical Minimum (0)
        0x25, 0x65, // Logical Maximum (101)
        0x05, 0x07, // Usage Page (Key Codes / Keyboard)
        0x19, 0x00, // Usage Minimum (0)
        0x29, 0x65, // Usage Maximum (101)
        0x81, 0x00, // Input (Data, Array) - Key array (6 keys)
        0xC0,       // End Collection
    ];

    #[test]
    fn test_parse_mouse_descriptor() {
        let desc = ReportDescriptor::parse(MOUSE_REPORT_DESCRIPTOR);
        assert_eq!(desc.total_bits, 32);
        assert_eq!(desc.input_groups.len(), 4);

        // Buttons
        let g0 = &desc.input_groups[0];
        assert_eq!(g0.usage_page, 0x09);
        assert_eq!(g0.size, 1);
        assert_eq!(g0.count, 3);
        assert!(!g0.discard);
        assert!(!g0.is_array);
        assert!(!g0.is_relative);
        assert_eq!(g0.readers.len(), 3);
        assert_eq!(g0.readers[0].usage, 1);
        assert_eq!(g0.readers[1].usage, 2);
        assert_eq!(g0.readers[2].usage, 3);

        // Padding
        let g1 = &desc.input_groups[1];
        assert_eq!(g1.size, 5);
        assert_eq!(g1.count, 1);
        assert!(g1.discard);

        // X and Y
        let g2 = &desc.input_groups[2];
        assert_eq!(g2.usage_page, 0x01);
        assert_eq!(g2.size, 8);
        assert_eq!(g2.count, 2);
        assert!(g2.is_signed);
        assert!(g2.is_relative);
        assert_eq!(g2.readers.len(), 2);
        assert_eq!(g2.readers[0].usage, 0x30);
        assert_eq!(g2.readers[1].usage, 0x31);

        // Wheel
        let g3 = &desc.input_groups[3];
        assert_eq!(g3.usage_page, 0x01);
        assert_eq!(g3.size, 8);
        assert_eq!(g3.count, 1);
        assert!(g3.is_signed);
        assert!(g3.is_relative);
        assert_eq!(g3.readers.len(), 1);
        assert_eq!(g3.readers[0].usage, 0x38);
    }

    #[test]
    fn test_decode_mouse_movement() {
        let desc = ReportDescriptor::parse(MOUSE_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // dx = 12, dy = -18, wheel = 0
        let packet = [0x00, 12, (-18i8) as u8, 0x00];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::MouseMove { dx: 12, dy: -18 });

        // No movement in next packet -> no MouseMove emitted
        let packet_still = [0x00, 0, 0, 0];
        let events = decoder.decode(&packet_still);
        assert!(events.is_empty());
    }

    #[test]
    fn test_decode_mouse_buttons() {
        let desc = ReportDescriptor::parse(MOUSE_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // Press Button 1 (Left)
        let packet = [0x01, 0, 0, 0];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::MouseButton {
                button: 1,
                pressed: true
            }
        );

        // Press Button 2 (Right) while Button 1 held
        let packet = [0x03, 0, 0, 0];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::MouseButton {
                button: 2,
                pressed: true
            }
        );

        // Release Button 1, keep Button 2
        let packet = [0x02, 0, 0, 0];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::MouseButton {
                button: 1,
                pressed: false
            }
        );

        // Release Button 2
        let packet = [0x00, 0, 0, 0];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::MouseButton {
                button: 2,
                pressed: false
            }
        );
    }

    #[test]
    fn test_decode_mouse_wheel() {
        let desc = ReportDescriptor::parse(MOUSE_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // Wheel +1
        let packet = [0x00, 0, 0, 1];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::MouseWheel { delta: 1 });

        // Wheel -2
        let packet = [0x00, 0, 0, (-2i8) as u8];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::MouseWheel { delta: -2 });
    }

    #[test]
    fn test_parse_keyboard_descriptor() {
        let desc = ReportDescriptor::parse(KEYBOARD_REPORT_DESCRIPTOR);
        assert_eq!(desc.total_bits, 64);
        assert_eq!(desc.input_groups.len(), 3);

        // Modifiers
        let g0 = &desc.input_groups[0];
        assert_eq!(g0.usage_page, 0x07);
        assert_eq!(g0.size, 1);
        assert_eq!(g0.count, 8);
        assert!(!g0.is_array);
        assert_eq!(g0.readers.len(), 8);
        assert_eq!(g0.readers[0].usage, 0xE0);
        assert_eq!(g0.readers[7].usage, 0xE7);

        // Reserved byte
        let g1 = &desc.input_groups[1];
        assert_eq!(g1.size, 8);
        assert_eq!(g1.count, 1);
        assert!(g1.discard);

        // Keys array
        let g2 = &desc.input_groups[2];
        assert_eq!(g2.usage_page, 0x07);
        assert_eq!(g2.size, 8);
        assert_eq!(g2.count, 6);
        assert!(g2.is_array);
    }

    #[test]
    fn test_decode_keyboard_single_key() {
        let desc = ReportDescriptor::parse(KEYBOARD_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // Press 'a' (0x04)
        let packet = [0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('a')
            }
        );

        // Repeat packet (key held down) -> no new events
        let events = decoder.decode(&packet);
        assert!(events.is_empty());

        // Release 'a'
        let release_packet = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&release_packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::KeyUp { keycode: 0x04, key: Key::A });
    }

    #[test]
    fn test_decode_keyboard_multiple_keys() {
        let desc = ReportDescriptor::parse(KEYBOARD_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // Press 'a' (0x04) and 'b' (0x05)
        let packet = [0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 2);
        assert_eq!(
            events[0],
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('a')
            }
        );
        assert_eq!(
            events[1],
            HidEvent::KeyDown {
                keycode: 0x05,
                key: Key::B,
                character: Some('b')
            }
        );

        // Release 'a' while keeping 'b'
        let packet = [0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::KeyUp { keycode: 0x04, key: Key::A });

        // Release 'b'
        let packet = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::KeyUp { keycode: 0x05, key: Key::B });
    }

    #[test]
    fn test_decode_keyboard_modifiers_and_shift() {
        let desc = ReportDescriptor::parse(KEYBOARD_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(desc);

        // Press LeftShift (bit 1 = 0x02, usage 0xE1)
        let packet_shift = [0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet_shift);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::KeyDown {
                keycode: 0xE1,
                key: Key::LeftShift,
                character: None
            }
        );
        assert!(decoder.modifier_tracker.shift());

        // Press 'a' (0x04) while holding Shift -> character should be 'A'
        let packet_shift_a = [0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet_shift_a);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('A')
            }
        );

        // Press '1' (0x1E) while holding Shift -> character should be '!'
        let packet_shift_1 = [0x02, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet_shift_1);
        assert_eq!(events.len(), 2);
        assert_eq!(events[0], HidEvent::KeyDown {
            keycode: 0x1E,
            key: Key::Num1,
            character: Some('!')
        });
        assert_eq!(events[1], HidEvent::KeyUp { keycode: 0x04, key: Key::A });

        // Release Shift while holding '1'
        let packet_1 = [0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet_1);
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            HidEvent::KeyUp {
                keycode: 0xE1,
                key: Key::LeftShift
            }
        );
        assert!(!decoder.modifier_tracker.shift());

        // Release '1'
        let packet_none = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode(&packet_none);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], HidEvent::KeyUp { keycode: 0x1E, key: Key::Num1 });
    }

    #[test]
    fn test_event_serialization_roundtrip() {
        let events = vec![
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('a'),
            },
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('A'),
            },
            HidEvent::KeyDown {
                keycode: 0x1E,
                key: Key::Num1,
                character: Some('!'),
            },
            HidEvent::KeyDown {
                keycode: 0x2C,
                key: Key::Space,
                character: Some(' '),
            },
            HidEvent::KeyDown {
                keycode: 0x28,
                key: Key::Return,
                character: Some('\n'),
            },
            HidEvent::KeyDown {
                keycode: 0xE0,
                key: Key::LeftCtrl,
                character: None,
            },
            HidEvent::KeyUp {
                keycode: 0x04,
                key: Key::A,
            },
            HidEvent::MouseMove { dx: -123, dy: 456 },
            HidEvent::MouseButton {
                button: 1,
                pressed: true,
            },
            HidEvent::MouseButton {
                button: 2,
                pressed: false,
            },
            HidEvent::MouseWheel { delta: -4 },
            HidEvent::MouseWheel { delta: 7 },
        ];

        for event in &events {
            // Line format roundtrip
            let line = event.to_line();
            let parsed_line = HidEvent::from_line(&line).expect("Failed to parse line");
            assert_eq!(*event, parsed_line);

            // Display / FromStr roundtrip
            let displayed = event.to_string();
            let parsed_from_str: HidEvent = displayed.parse().expect("Failed FromStr");
            assert_eq!(*event, parsed_from_str);

            // Binary format roundtrip
            let bytes = event.to_bytes();
            let parsed_bytes = HidEvent::from_bytes(&bytes).expect("Failed to parse bytes");
            assert_eq!(*event, parsed_bytes);
        }
    }

    #[test]
    fn test_client_streaming() {
        let mut buffer = Vec::new();
        let events = vec![
            HidEvent::MouseMove { dx: 10, dy: 20 },
            HidEvent::MouseButton {
                button: 1,
                pressed: true,
            },
            HidEvent::KeyDown {
                keycode: 0x04,
                key: Key::A,
                character: Some('a'),
            },
            HidEvent::KeyUp {
                keycode: 0x04,
                key: Key::A,
            },
        ];

        for event in &events {
            write_event(&mut buffer, event).expect("write_event failed");
        }

        let cursor = Cursor::new(buffer);
        let mut client = HidClient::new(cursor);

        let mut read_events = Vec::new();
        while let Some(event) = client.next_event().expect("next_event failed") {
            read_events.push(event);
        }

        assert_eq!(events, read_events);
    }

    #[test]
    fn test_bit_unpacker_and_sign_extension() {
        // Test reading across byte boundary
        // Packet: [0b11001010, 0b10101111] = [0xCA, 0xAF]
        let packet = [0xCA, 0xAF];
        // Read 4 bits starting at offset 2: bits 2..5 of 0xCA -> (0xCA >> 2) & 0x0F = (202 >> 2) & 15 = 50 & 15 = 2 (0b0010)
        assert_eq!(read_bits(&packet, 2, 4), (0xCA >> 2) & 0x0F);

        // Read 8 bits starting at offset 6: 2 bits from byte 0 (bits 6..7 = 0b11 = 3), 6 bits from byte 1 (bits 0..5 = 0b101111 = 47)
        // 3 | (47 << 2) = 3 | 188 = 191 (0xBF)
        assert_eq!(read_bits(&packet, 6, 8), 0xBF);

        // Test sign extension
        assert_eq!(sign_extend(0x7F, 8), 127);
        assert_eq!(sign_extend(0x80, 8), -128);
        assert_eq!(sign_extend(0xFF, 8), -1);
        assert_eq!(sign_extend(0x01, 8), 1);

        assert_eq!(sign_extend(0x7FFF, 16), 32767);
        assert_eq!(sign_extend(0x8000, 16), -32768);
        assert_eq!(sign_extend(0xFFFF, 16), -1);

        // 12-bit signed
        assert_eq!(sign_extend(0x7FF, 12), 2047);
        assert_eq!(sign_extend(0x800, 12), -2048);
        assert_eq!(sign_extend(0xFFF, 12), -1);
    }

    #[test]
    fn test_report_id_support() {
        // Create a descriptor with Report ID 1 (mouse) and Report ID 2 (keyboard)
        let desc_bytes: &[u8] = &[
            0x05, 0x01, // Usage Page (Generic Desktop)
            0x09, 0x02, // Usage (Mouse)
            0xA1, 0x01, // Collection (Application)
            0x85, 0x01, //   Report ID (1)
            0x05, 0x09, //   Usage Page (Button)
            0x19, 0x01, //   Usage Minimum (1)
            0x29, 0x03, //   Usage Maximum (3)
            0x15, 0x00, //   Logical Minimum (0)
            0x25, 0x01, //   Logical Maximum (1)
            0x95, 0x03, //   Report Count (3)
            0x75, 0x01, //   Report Size (1)
            0x81, 0x02, //   Input (Data, Variable, Absolute)
            0x95, 0x01, //   Report Count (1)
            0x75, 0x05, //   Report Size (5)
            0x81, 0x01, //   Input (Constant)
            0x05, 0x01, //   Usage Page (Generic Desktop)
            0x09, 0x30, //   Usage (X)
            0x09, 0x31, //   Usage (Y)
            0x15, 0x81, //   Logical Minimum (-127)
            0x25, 0x7F, //   Logical Maximum (127)
            0x75, 0x08, //   Report Size (8)
            0x95, 0x02, //   Report Count (2)
            0x81, 0x06, //   Input (Data, Variable, Relative)
            0xC0,       // End Collection
        ];

        let desc = ReportDescriptor::parse(desc_bytes);
        assert_eq!(desc.input_groups[0].report_id, Some(1));

        let mut decoder = ReportDecoder::new(desc);
        // Packet for Report ID 1: [report_id = 1, buttons = 1, dx = 10, dy = -5]
        let packet = [0x01, 0x01, 10, (-5i8) as u8];
        let events = decoder.decode(&packet);
        assert_eq!(events.len(), 2);
        assert_eq!(events[0], HidEvent::MouseButton { button: 1, pressed: true });
        assert_eq!(events[1], HidEvent::MouseMove { dx: 10, dy: -5 });

        // Packet for unknown Report ID 2 -> ignored, no events
        let packet_unknown = [0x02, 0x01, 10, 10];
        let events = decoder.decode(&packet_unknown);
        assert!(events.is_empty());
    }

    #[test]
    fn test_modifier_tracker_all_keys() {
        let mut tracker = ModifierTracker::new();
        assert!(!tracker.shift());
        assert!(!tracker.ctrl());
        assert!(!tracker.alt());
        assert!(!tracker.gui());

        tracker.update(0xE0, true); // Left Ctrl
        assert!(tracker.ctrl());
        tracker.update(0xE4, true); // Right Ctrl
        assert!(tracker.ctrl());
        tracker.update(0xE0, false); // Release Left Ctrl
        assert!(tracker.ctrl()); // Still true due to Right Ctrl
        tracker.update(0xE4, false); // Release Right Ctrl
        assert!(!tracker.ctrl());

        tracker.update(0xE2, true); // Left Alt
        assert!(tracker.alt());
        tracker.update(0xE2, false);
        assert!(!tracker.alt());

        tracker.update(0xE3, true); // Left GUI
        assert!(tracker.gui());
        tracker.update(0xE3, false);
        assert!(!tracker.gui());
    }

    #[test]
    fn test_keyboard_special_keys_and_symbols() {
        assert_eq!(keycode_to_char(0x28, false), Some('\n'));
        assert_eq!(keycode_to_char(0x2A, false), Some('\x08'));
        assert_eq!(keycode_to_char(0x2B, false), Some('\t'));
        assert_eq!(keycode_to_char(0x2C, false), Some(' '));

        assert_eq!(keycode_to_char(0x2D, false), Some('-'));
        assert_eq!(keycode_to_char(0x2D, true), Some('_'));

        assert_eq!(keycode_to_char(0x2E, false), Some('='));
        assert_eq!(keycode_to_char(0x2E, true), Some('+'));

        assert_eq!(keycode_to_char(0x2F, false), Some('['));
        assert_eq!(keycode_to_char(0x2F, true), Some('{'));

        assert_eq!(keycode_to_char(0x38, false), Some('/'));
        assert_eq!(keycode_to_char(0x38, true), Some('?'));

        // Keys without character representation
        assert_eq!(keycode_to_char(0x29, false), None); // Escape
        assert_eq!(keycode_to_char(0x3A, false), None); // F1
        assert_eq!(keycode_to_char(0x4F, false), None); // RightArrow
        assert_eq!(keycode_to_char(0xE1, true), None);  // LeftShift
    }
}
