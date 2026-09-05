use honeyos_dma::DmaBuffer;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct SlotContext {
    pub dword0: u32,
    pub dword1: u32,
    pub dword2: u32,
    pub dword3: u32,
    pub reserved: [u32; 4],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct EndpointContext {
    pub dword0: u32,
    pub dword1: u32,
    pub tr_dequeue_low: u32,
    pub tr_dequeue_high: u32,
    pub dword4: u32,
    pub reserved: [u32; 3],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct DeviceContext {
    pub slot: SlotContext,
    pub endpoints: [EndpointContext; 31],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct InputControl {
    pub drop_flags: u32,
    pub add_flags: u32,
    pub reserved: [u32; 6],
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct InputContext {
    pub control: InputControl,
    pub device: DeviceContext,
}

impl SlotContext {
    pub fn new(root_hub_port: u8, speed: u8, context_entries: u8) -> Self {
        let dword0 = ((speed as u32) << 20) | ((context_entries as u32) << 27);
        let dword1 = (root_hub_port as u32) << 16;
        Self {
            dword0,
            dword1,
            dword2: 0,
            dword3: 0,
            reserved: [0; 4],
        }
    }

    #[inline]
    pub fn slot_state(&self) -> u8 {
        ((self.dword3 >> 27) & 0x1F) as u8
    }

    #[inline]
    pub fn device_address(&self) -> u8 {
        (self.dword3 & 0xFF) as u8
    }
}

impl EndpointContext {
    pub fn new_control_ep0(max_packet_size: u16, ring_phys: u32) -> Self {
        // Endpoint Type 4 = Control Bidirectional, Error Count = 3
        let dword1 = (3 << 1) | (4 << 3) | ((max_packet_size as u32) << 16);
        let tr_dequeue_low = (ring_phys & !0xF) | 1; // DCS bit = 1
        let dword4 = 8; // average TRB length

        Self {
            dword0: 0,
            dword1,
            tr_dequeue_low,
            tr_dequeue_high: 0,
            dword4,
            reserved: [0; 3],
        }
    }

    pub fn new_transfer(
        ep_type: u8,
        max_packet_size: u16,
        interval: u8,
        ring_phys: u32,
    ) -> Self {
        let dword0 = (interval as u32) << 16;
        let dword1 = (3 << 1) | ((ep_type as u32) << 3) | ((max_packet_size as u32) << 16);
        let tr_dequeue_low = (ring_phys & !0xF) | 1; // DCS bit = 1
        let dword4 = 1024; // average TRB length

        Self {
            dword0,
            dword1,
            tr_dequeue_low,
            tr_dequeue_high: 0,
            dword4,
            reserved: [0; 3],
        }
    }
}

pub fn allocate_input_context(
    root_hub_port: u8,
    speed: u8,
    ep0_max_packet: u16,
    ep0_ring_phys: u32,
) -> Result<DmaBuffer, String> {
    let buf = DmaBuffer::allocate(1)?;
    let ptr = buf.virt_ptr() as *mut InputContext;

    unsafe {
        let control = InputControl {
            drop_flags: 0,
            add_flags: 3, // bit 0: Slot Context, bit 1: EP0 Context
            reserved: [0; 6],
        };
        std::ptr::write_volatile(&mut (*ptr).control, control);

        let slot = SlotContext::new(root_hub_port, speed, 1);
        std::ptr::write_volatile(&mut (*ptr).device.slot, slot);

        let ep0 = EndpointContext::new_control_ep0(ep0_max_packet, ep0_ring_phys);
        std::ptr::write_volatile(&mut (*ptr).device.endpoints[0], ep0);
    }

    Ok(buf)
}
