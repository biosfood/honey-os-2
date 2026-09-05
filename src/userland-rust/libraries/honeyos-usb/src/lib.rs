#[repr(C, packed)]
#[derive(Copy, Clone, Debug, Default)]
pub struct DeviceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_usb: u16,
    pub b_device_class: u8,
    pub b_device_sub_class: u8,
    pub b_device_protocol: u8,
    pub b_max_packet_size0: u8,
    pub id_vendor: u16,
    pub id_product: u16,
    pub bcd_device: u16,
    pub i_manufacturer: u8,
    pub i_product: u8,
    pub i_serial_number: u8,
    pub b_num_configurations: u8,
}

#[repr(C, packed)]
#[derive(Copy, Clone, Debug, Default)]
pub struct ConfigurationDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub w_total_length: u16,
    pub b_num_interfaces: u8,
    pub b_configuration_value: u8,
    pub i_configuration: u8,
    pub bm_attributes: u8,
    pub b_max_power: u8,
}

#[repr(C, packed)]
#[derive(Copy, Clone, Debug, Default)]
pub struct InterfaceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_interface_number: u8,
    pub b_alternate_setting: u8,
    pub b_num_endpoints: u8,
    pub b_interface_class: u8,
    pub b_interface_sub_class: u8,
    pub b_interface_protocol: u8,
    pub i_interface: u8,
}

#[repr(C, packed)]
#[derive(Copy, Clone, Debug, Default)]
pub struct EndpointDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_endpoint_address: u8,
    pub bm_attributes: u8,
    pub w_max_packet_size: u16,
    pub b_interval: u8,
}

#[repr(C, packed)]
#[derive(Copy, Clone, Debug, Default)]
pub struct HidDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_hid: u16,
    pub b_country_code: u8,
    pub b_num_descriptors: u8,
    pub b_report_descriptor_type: u8,
    pub w_descriptor_length: u16,
}

#[derive(Clone, Debug)]
pub struct ParsedEndpoint {
    pub address: u8,
    pub endpoint_number: u8,
    pub is_in: bool,
    pub transfer_type: u8, // 0: Control, 1: Isoch, 2: Bulk, 3: Interrupt
    pub max_packet_size: u16,
    pub interval: u8,
}

#[derive(Clone, Debug)]
pub struct ParsedInterface {
    pub interface_number: u8,
    pub class: u8,
    pub subclass: u8,
    pub protocol: u8,
    pub endpoints: Vec<ParsedEndpoint>,
}

pub fn parse_string_descriptor(data: &[u8]) -> Option<String> {
    if data.len() < 2 {
        return None;
    }
    let length = data[0] as usize;
    if length < 2 || length > data.len() || data[1] != 3 {
        return None;
    }
    let chars: Vec<u16> = data[2..length]
        .chunks_exact(2)
        .map(|c| u16::from_le_bytes([c[0], c[1]]))
        .collect();
    Some(String::from_utf16_lossy(&chars))
}

pub fn parse_configuration(data: &[u8]) -> Option<(ConfigurationDescriptor, Vec<ParsedInterface>)> {
    if data.len() < 9 {
        return None;
    }
    let mut config = ConfigurationDescriptor::default();
    unsafe {
        std::ptr::copy_nonoverlapping(
            data.as_ptr(),
            &mut config as *mut _ as *mut u8,
            std::mem::size_of::<ConfigurationDescriptor>(),
        );
    }

    let total_length = (config.w_total_length as usize).min(data.len());
    let mut interfaces = Vec::new();
    let mut offset = config.b_length as usize;

    let mut current_interface: Option<ParsedInterface> = None;

    while offset + 2 <= total_length {
        let length = data[offset] as usize;
        if length == 0 || offset + length > total_length {
            break;
        }
        let desc_type = data[offset + 1];

        if desc_type == 4 && length >= 9 {
            // Interface descriptor
            if let Some(iface) = current_interface.take() {
                interfaces.push(iface);
            }
            let mut iface_desc = InterfaceDescriptor::default();
            unsafe {
                std::ptr::copy_nonoverlapping(
                    data[offset..].as_ptr(),
                    &mut iface_desc as *mut _ as *mut u8,
                    std::mem::size_of::<InterfaceDescriptor>(),
                );
            }
            current_interface = Some(ParsedInterface {
                interface_number: iface_desc.b_interface_number,
                class: iface_desc.b_interface_class,
                subclass: iface_desc.b_interface_sub_class,
                protocol: iface_desc.b_interface_protocol,
                endpoints: Vec::new(),
            });
        } else if desc_type == 5 && length >= 7 {
            // Endpoint descriptor
            let mut ep_desc = EndpointDescriptor::default();
            unsafe {
                std::ptr::copy_nonoverlapping(
                    data[offset..].as_ptr(),
                    &mut ep_desc as *mut _ as *mut u8,
                    std::mem::size_of::<EndpointDescriptor>(),
                );
            }
            let is_in = (ep_desc.b_endpoint_address & 0x80) != 0;
            let ep_num = ep_desc.b_endpoint_address & 0x0F;
            let transfer_type = ep_desc.bm_attributes & 0x03;

            let parsed_ep = ParsedEndpoint {
                address: ep_desc.b_endpoint_address,
                endpoint_number: ep_num,
                is_in,
                transfer_type,
                max_packet_size: ep_desc.w_max_packet_size,
                interval: ep_desc.b_interval,
            };

            if let Some(ref mut iface) = current_interface {
                iface.endpoints.push(parsed_ep);
            }
        }

        offset += length;
    }

    if let Some(iface) = current_interface {
        interfaces.push(iface);
    }

    Some((config, interfaces))
}
