/// USB HID Report Descriptor Parser.
///
/// Ports and improves the report descriptor parser from `src/old/hid/main.c`
/// to safe, clean, idiomatic Rust.

/// Representation of a single reader mapped to a specific usage.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InputReader {
    pub usage: u32,
    pub previous_state: i32,
}

/// A grouped sequence of input fields with common parameters.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InputGroup {
    pub usage_page: u16,
    pub size: u8,
    pub count: u32,
    pub logical_min: i32,
    pub logical_max: i32,
    pub physical_min: i32,
    pub physical_max: i32,
    pub discard: bool,
    pub is_relative: bool,
    pub is_signed: bool,
    pub is_array: bool,
    pub readers: Vec<InputReader>,
    pub report_id: Option<u8>,
}

/// Structured report descriptor containing decoded input groups and total bit count.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ReportDescriptor {
    pub input_groups: Vec<InputGroup>,
    pub total_bits: usize,
}

pub type ReportDescriptorParser = ReportDescriptor;

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

#[derive(Clone, Debug, Default)]
struct GlobalState {
    usage_page: u16,
    logical_min: i32,
    logical_max: i32,
    physical_min: i32,
    physical_max: i32,
    report_size: u8,
    report_count: u32,
    report_id: Option<u8>,
}

#[derive(Clone, Debug, Default)]
struct LocalState {
    usages: Vec<u32>,
    usage_min: Option<u32>,
    usage_max: Option<u32>,
}

impl ReportDescriptor {
    /// Parse raw report descriptor bytes into a structured `ReportDescriptor`.
    pub fn parse(bytes: &[u8]) -> Self {
        let mut report_desc = ReportDescriptor::default();
        let mut global_state = GlobalState::default();
        let mut global_stack: Vec<GlobalState> = Vec::new();
        let mut local_state = LocalState::default();

        let mut offset = 0;
        while offset < bytes.len() {
            let item = bytes[offset];
            offset += 1;

            if item == 0x00 {
                // Padding or end of descriptor
                continue;
            }

            // Long item handling (0xFE)
            if item == 0xFE {
                if offset + 2 > bytes.len() {
                    break;
                }
                let data_len = bytes[offset] as usize;
                offset += 2 + data_len;
                continue;
            }

            let size_code = item & 0x03;
            let data_size = match size_code {
                0 => 0,
                1 => 1,
                2 => 2,
                3 => 4,
                _ => unreachable!(),
            };

            if offset + data_size > bytes.len() {
                break;
            }

            let raw_data = match data_size {
                0 => 0,
                1 => bytes[offset] as u32,
                2 => u16::from_le_bytes([bytes[offset], bytes[offset + 1]]) as u32,
                4 => u32::from_le_bytes([
                    bytes[offset],
                    bytes[offset + 1],
                    bytes[offset + 2],
                    bytes[offset + 3],
                ]),
                _ => unreachable!(),
            };

            let signed_data = match data_size {
                0 => 0,
                1 => (bytes[offset] as i8) as i32,
                2 => (i16::from_le_bytes([bytes[offset], bytes[offset + 1]])) as i32,
                4 => i32::from_le_bytes([
                    bytes[offset],
                    bytes[offset + 1],
                    bytes[offset + 2],
                    bytes[offset + 3],
                ]),
                _ => unreachable!(),
            };

            offset += data_size;

            let item_type = (item >> 2) & 0x03;
            let item_tag = (item >> 4) & 0x0F;

            match item_type {
                0 => {
                    // Main Items
                    match item_tag {
                        0x08 => {
                            // Input (0x80)
                            let discard = (raw_data & 0x01) != 0;
                            let is_array = (raw_data & 0x02) == 0;
                            let is_relative = (raw_data & 0x04) != 0;
                            let is_signed = global_state.logical_min < 0
                                || (global_state.logical_min as u32 > global_state.logical_max as u32
                                    && global_state.logical_min != 0);

                            let mut readers = Vec::new();
                            let usage_count = local_state.usages.len();

                            if usage_count == 1 {
                                let usage = local_state.usages[0];
                                for _ in 0..global_state.report_count {
                                    readers.push(InputReader {
                                        usage,
                                        previous_state: 0,
                                    });
                                }
                            } else if usage_count == global_state.report_count as usize {
                                for &usage in &local_state.usages {
                                    readers.push(InputReader {
                                        usage,
                                        previous_state: 0,
                                    });
                                }
                            } else if usage_count == 0
                                && local_state.usage_min.is_some()
                                && local_state.usage_max.is_some()
                            {
                                let min = local_state.usage_min.unwrap();
                                let max = local_state.usage_max.unwrap();
                                if max >= min && (max - min + 1 == global_state.report_count) {
                                    for u in min..=max {
                                        readers.push(InputReader {
                                            usage: u,
                                            previous_state: 0,
                                        });
                                    }
                                } else {
                                    for i in 0..global_state.report_count {
                                        readers.push(InputReader {
                                            usage: i,
                                            previous_state: 0,
                                        });
                                    }
                                }
                            } else {
                                for i in 0..global_state.report_count {
                                    let usage =
                                        local_state.usages.get(i as usize).copied().unwrap_or(i);
                                    readers.push(InputReader {
                                        usage,
                                        previous_state: 0,
                                    });
                                }
                            }

                            let group = InputGroup {
                                usage_page: global_state.usage_page,
                                size: global_state.report_size,
                                count: global_state.report_count,
                                logical_min: global_state.logical_min,
                                logical_max: global_state.logical_max,
                                physical_min: global_state.physical_min,
                                physical_max: global_state.physical_max,
                                discard,
                                is_relative,
                                is_signed,
                                is_array,
                                readers,
                                report_id: global_state.report_id,
                            };

                            report_desc.input_groups.push(group);
                            report_desc.total_bits += (global_state.report_size as usize)
                                * (global_state.report_count as usize);

                            // Local items are cleared after each Main item
                            local_state = LocalState::default();
                        }
                        0x09 => {
                            // Output (0x90)
                            local_state = LocalState::default();
                        }
                        0x0A => {
                            // Collection (0xA0)
                            local_state = LocalState::default();
                        }
                        0x0B => {
                            // Feature (0xB0)
                            local_state = LocalState::default();
                        }
                        0x0C => {
                            // EndCollection (0xC0)
                            local_state = LocalState::default();
                        }
                        _ => {}
                    }
                }
                1 => {
                    // Global Items
                    match item_tag {
                        0x00 => {
                            // Usage Page (0x04)
                            global_state.usage_page = raw_data as u16;
                        }
                        0x01 => {
                            // Logical Minimum (0x14)
                            global_state.logical_min = signed_data;
                        }
                        0x02 => {
                            // Logical Maximum (0x24)
                            if global_state.logical_min < 0 {
                                global_state.logical_max = signed_data;
                            } else {
                                global_state.logical_max = raw_data as i32;
                            }
                        }
                        0x03 => {
                            // Physical Minimum (0x34)
                            global_state.physical_min = signed_data;
                        }
                        0x04 => {
                            // Physical Maximum (0x44)
                            if global_state.physical_min < 0 {
                                global_state.physical_max = signed_data;
                            } else {
                                global_state.physical_max = raw_data as i32;
                            }
                        }
                        0x07 => {
                            // Report Size (0x74)
                            global_state.report_size = raw_data as u8;
                        }
                        0x08 => {
                            // Report ID (0x84)
                            global_state.report_id = Some(raw_data as u8);
                        }
                        0x09 => {
                            // Report Count (0x94)
                            global_state.report_count = raw_data;
                        }
                        0x0A => {
                            // Push (0xA4)
                            global_stack.push(global_state.clone());
                        }
                        0x0B => {
                            // Pop (0xB4)
                            if let Some(popped) = global_stack.pop() {
                                global_state = popped;
                            }
                        }
                        _ => {}
                    }
                }
                2 => {
                    // Local Items
                    match item_tag {
                        0x00 => {
                            // Usage (0x08)
                            local_state.usages.push(raw_data);
                        }
                        0x01 => {
                            // Usage Minimum (0x18)
                            local_state.usage_min = Some(raw_data);
                        }
                        0x02 => {
                            // Usage Maximum (0x28)
                            local_state.usage_max = Some(raw_data);
                        }
                        _ => {}
                    }
                }
                _ => {}
            }
        }

        report_desc
    }
}
