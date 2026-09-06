use crate::descriptor::ReportDescriptor;
use crate::keycodes::{keycode_to_char, HidEvent, Key, ModifierTracker};

/// Reads `count` bits from `packet` starting at `bit_offset` (LSB first).
pub fn read_bits(packet: &[u8], bit_offset: usize, count: usize) -> u32 {
    if count == 0 {
        return 0;
    }
    let mut result: u32 = 0;
    for i in 0..count {
        let bit_index = bit_offset + i;
        let byte_index = bit_index / 8;
        let bit_in_byte = bit_index % 8;
        if byte_index < packet.len() {
            let bit = ((packet[byte_index] >> bit_in_byte) & 1) as u32;
            result |= bit << i;
        }
    }
    result
}

/// Sign-extends a `size`-bit integer to a 32-bit signed integer (`i32`).
pub fn sign_extend(value: u32, size: u8) -> i32 {
    if size == 0 || size >= 32 {
        return value as i32;
    }
    let sign_bit = 1 << (size - 1);
    if (value & sign_bit) != 0 {
        let mask = !((1u32 << size) - 1);
        (value | mask) as i32
    } else {
        value as i32
    }
}

#[derive(Clone, Debug)]
struct GroupState {
    old_array_usages: Vec<u32>,
    previous_values: Vec<i32>,
}

/// Report Decoder maintains state across incoming report packets
/// and decodes raw USB HID packets into high-level `HidEvent`s.
#[derive(Clone, Debug)]
pub struct ReportDecoder {
    pub descriptor: ReportDescriptor,
    group_states: Vec<GroupState>,
    pub modifier_tracker: ModifierTracker,
}

impl ReportDecoder {
    /// Create a new `ReportDecoder` for a parsed `ReportDescriptor`.
    pub fn new(descriptor: ReportDescriptor) -> Self {
        let mut group_states = Vec::with_capacity(descriptor.input_groups.len());
        for group in &descriptor.input_groups {
            let old_array_usages = if group.is_array {
                Vec::new()
            } else {
                Vec::new()
            };
            let previous_values = if !group.is_array {
                vec![0; group.readers.len()]
            } else {
                Vec::new()
            };
            group_states.push(GroupState {
                old_array_usages,
                previous_values,
            });
        }

        Self {
            descriptor,
            group_states,
            modifier_tracker: ModifierTracker::new(),
        }
    }

    /// Reset internal state (e.g. on device reconnect or flush).
    pub fn reset(&mut self) {
        for state in &mut self.group_states {
            state.old_array_usages.clear();
            for v in &mut state.previous_values {
                *v = 0;
            }
        }
        self.modifier_tracker = ModifierTracker::new();
    }

    /// Decode a raw report packet into a list of `HidEvent`s.
    pub fn decode(&mut self, packet: &[u8]) -> Vec<HidEvent> {
        self.decode_packet(packet)
    }

    /// Decode a raw report packet into a list of `HidEvent`s (alias for `decode`).
    pub fn decode_packet(&mut self, packet: &[u8]) -> Vec<HidEvent> {
        if packet.is_empty() {
            return Vec::new();
        }

        let has_report_id = self
            .descriptor
            .input_groups
            .iter()
            .any(|g| g.report_id.is_some());

        let (packet_report_id, mut bit_offset) = if has_report_id {
            (Some(packet[0]), 8)
        } else {
            (None, 0)
        };

        let mut events = Vec::new();
        let mut mouse_dx: i32 = 0;
        let mut mouse_dy: i32 = 0;
        let mut mouse_moved = false;

        for group_idx in 0..self.descriptor.input_groups.len() {
            let group = &self.descriptor.input_groups[group_idx];

            if has_report_id && group.report_id != packet_report_id {
                continue;
            }

            let field_bits = (group.size as usize) * (group.count as usize);
            if group.discard {
                bit_offset += field_bits;
                continue;
            }

            if group.is_array {
                let mut new_usages = Vec::new();
                for _ in 0..group.count {
                    if bit_offset + (group.size as usize) > packet.len() * 8 {
                        break;
                    }
                    let raw = read_bits(packet, bit_offset, group.size as usize);
                    bit_offset += group.size as usize;
                    // Filter out 0 (no key) and standard rollover/POST error codes (0x01..=0x03)
                    if raw >= 0x04 {
                        new_usages.push(raw);
                    }
                }

                let old_usages = self.group_states[group_idx].old_array_usages.clone();

                // Newly pressed keys
                for &u in &new_usages {
                    if !old_usages.contains(&u) {
                        if group.usage_page == 0x07 {
                            let keycode = u as u8;
                            self.modifier_tracker.update(keycode, true);
                            let key = Key::from_keycode(keycode);
                            let character =
                                keycode_to_char(keycode, self.modifier_tracker.shift());
                            events.push(HidEvent::KeyDown {
                                keycode,
                                key,
                                character,
                            });
                        }
                    }
                }

                // Newly released keys
                for &u in &old_usages {
                    if !new_usages.contains(&u) {
                        if group.usage_page == 0x07 {
                            let keycode = u as u8;
                            self.modifier_tracker.update(keycode, false);
                            let key = Key::from_keycode(keycode);
                            events.push(HidEvent::KeyUp { keycode, key });
                        }
                    }
                }

                self.group_states[group_idx].old_array_usages = new_usages;
            } else {
                // Variable inputs
                for (reader_idx, reader) in group.readers.iter().enumerate() {
                    if bit_offset + (group.size as usize) > packet.len() * 8 {
                        break;
                    }
                    let raw = read_bits(packet, bit_offset, group.size as usize);
                    bit_offset += group.size as usize;

                    let val = if group.is_signed {
                        sign_extend(raw, group.size)
                    } else {
                        raw as i32
                    };

                    if group.is_relative {
                        // Relative values (axes / wheel) are reported whenever non-zero
                        if val != 0 {
                            if group.usage_page == 0x01 {
                                match reader.usage {
                                    0x30 => {
                                        mouse_dx += val;
                                        mouse_moved = true;
                                    }
                                    0x31 => {
                                        mouse_dy += val;
                                        mouse_moved = true;
                                    }
                                    0x38 => {
                                        events.push(HidEvent::MouseWheel { delta: val });
                                    }
                                    _ => {}
                                }
                            }
                        }
                    } else {
                        // Absolute values (buttons / modifiers) are reported when state changes
                        let prev_val = self.group_states[group_idx].previous_values[reader_idx];
                        if val != prev_val {
                            self.group_states[group_idx].previous_values[reader_idx] = val;

                            if group.usage_page == 0x09 {
                                // Button Page (usages 1..=5 etc.)
                                let button = reader.usage as u8;
                                let pressed = val != 0;
                                events.push(HidEvent::MouseButton { button, pressed });
                            } else if group.usage_page == 0x07 {
                                // Keyboard modifier keys
                                let keycode = reader.usage as u8;
                                let pressed = val != 0;
                                self.modifier_tracker.update(keycode, pressed);
                                let key = Key::from_keycode(keycode);
                                if pressed {
                                    let character =
                                        keycode_to_char(keycode, self.modifier_tracker.shift());
                                    events.push(HidEvent::KeyDown {
                                        keycode,
                                        key,
                                        character,
                                    });
                                } else {
                                    events.push(HidEvent::KeyUp { keycode, key });
                                }
                            }
                        }
                    }
                }
            }
        }

        if mouse_moved {
            events.push(HidEvent::MouseMove {
                dx: mouse_dx,
                dy: mouse_dy,
            });
        }

        events
    }
}
