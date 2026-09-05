use crate::context::{allocate_input_context, EndpointContext, InputContext};
use crate::registers::XhciRegisters;
use crate::ring::{
    EventRing, Trb, TrbRing, TRB_ADDRESS_DEVICE, TRB_COMMAND_COMPLETION_EVENT,
    TRB_CONFIGURE_ENDPOINT, TRB_DATA_STAGE, TRB_ENABLE_SLOT, TRB_EVALUATE_CONTEXT,
    TRB_IDT, TRB_IOC, TRB_SETUP_STAGE, TRB_STATUS_STAGE, TRB_TRANSFER_EVENT,
};
use honeyos_dma::DmaBuffer;
use honeyos_pci::PciDevice;
use honeyos_pic::IrqSubscription;
use honeyos_usb::{
    parse_configuration, parse_string_descriptor, ConfigurationDescriptor, DeviceDescriptor,
    ParsedInterface,
};
use std::fs::OpenOptions;
use std::io::Write;

extern "C" {
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

#[derive(Debug, Clone)]
pub struct DiscoveredUsbDevice {
    pub slot_id: u8,
    pub port_id: u8,
    pub speed_name: &'static str,
    pub vendor_id: u16,
    pub product_id: u16,
    pub device_class: u8,
    pub device_subclass: u8,
    pub manufacturer: String,
    pub product: String,
    pub serial: String,
    pub config: ConfigurationDescriptor,
    pub interfaces: Vec<ParsedInterface>,
}

pub struct XhciController {
    pub pci_dev: PciDevice,
    pub regs: XhciRegisters,
    irq_sub: Option<IrqSubscription>,
    _dcbaa: DmaBuffer,
    _scratchpad_array: Option<DmaBuffer>,
    _scratchpad_buffers: Vec<DmaBuffer>,
    cmd_ring: TrbRing,
    event_ring: EventRing,
    _erst: DmaBuffer,
    device_contexts: Vec<Option<DmaBuffer>>,
    input_contexts: Vec<Option<DmaBuffer>>,
    ep0_rings: Vec<Option<TrbRing>>,
    endpoint_rings: Vec<Vec<Option<TrbRing>>>,
    pub port_count: u8,
    pub max_slots: u8,
}

impl XhciController {
    pub fn new(pci_dev: PciDevice) -> Result<Self, String> {
        println!("Mapping MMIO BAR0: 0x{:x} (size: 0x{:x})", pci_dev.bar0, pci_dev.bar0_size);
        let regs = XhciRegisters::map(pci_dev.bar0, pci_dev.bar0_size)?;

        let cap_len = regs.cap_length();
        let hci_ver = regs.hci_version();
        let max_slots = regs.max_slots();
        let max_ports = regs.max_ports();
        println!(
            "Controller CapLength: 0x{:x}, Version: {}.{}, MaxSlots: {}, MaxPorts: {}",
            cap_len,
            (hci_ver >> 8) & 0xFF,
            hci_ver & 0xFF,
            max_slots,
            max_ports
        );

        // Subscribe to IRQ via honeyos-pic (automatically unmasks the IRQ)
        let irq = pci_dev.irq;
        println!("Controller assigned IRQ: {}", irq);
        let irq_sub = match IrqSubscription::subscribe(irq) {
            Ok(sub) => {
                println!("Subscribed to IRQ notification on /dev/pic/{}", irq);
                Some(sub)
            }
            Err(e) => {
                println!("Warning: Could not subscribe to IRQ {} ({}), will fallback to polling", irq, e);
                None
            }
        };

        // 1. Reset host controller
        println!("Resetting host controller...");
        // Clear Run/Stop (RS)
        let cmd = regs.read_usbcmd();
        regs.write_usbcmd(cmd & !1);
        // Wait for HCHalted (HCH)
        let mut timeout = 1000;
        while (regs.read_usbsts() & 1) == 0 && timeout > 0 {
            std::thread::sleep(std::time::Duration::from_millis(1));
            timeout -= 1;
        }
        // Set Host Controller Reset (HCRST)
        regs.write_usbcmd(regs.read_usbcmd() | (1 << 1));
        timeout = 1000;
        while (regs.read_usbcmd() & (1 << 1)) != 0 && timeout > 0 {
            std::thread::sleep(std::time::Duration::from_millis(1));
            timeout -= 1;
        }
        // Wait for Controller Not Ready (CNR) to clear
        timeout = 1000;
        while (regs.read_usbsts() & (1 << 11)) != 0 && timeout > 0 {
            std::thread::sleep(std::time::Duration::from_millis(1));
            timeout -= 1;
        }
        println!("Controller reset successful!");

        // 2. Allocate DCBAA (1 page)
        let dcbaa = DmaBuffer::allocate(1)?;
        let dcbaa_phys = dcbaa.phys_addr();
        regs.write_dcbaap(dcbaa_phys as u64);

        // 3. Set Max Slots Enabled
        let effective_slots = max_slots.min(32);
        regs.write_config(effective_slots as u32);

        // 4. Allocate Scratchpad Buffers if needed
        let sp_count = regs.max_scratchpad_buffers();
        let mut scratchpad_array = None;
        let mut scratchpad_buffers = Vec::new();
        if sp_count > 0 {
            println!("Allocating {} scratchpad buffers...", sp_count);
            let array_buf = DmaBuffer::allocate(1)?;
            let array_phys = array_buf.phys_addr();
            let array_ptr = array_buf.virt_ptr() as *mut u64;

            for i in 0..sp_count {
                let sp_page = DmaBuffer::allocate(1)?;
                unsafe {
                    std::ptr::write_volatile(array_ptr.add(i as usize), sp_page.phys_addr() as u64);
                }
                scratchpad_buffers.push(sp_page);
            }
            // Store physical address of scratchpad array in DCBAA[0]
            unsafe {
                let dcbaa_ptr = dcbaa.virt_ptr() as *mut u64;
                std::ptr::write_volatile(dcbaa_ptr, array_phys as u64);
            }
            scratchpad_array = Some(array_buf);
        }

        // 5. Initialize Command Ring (256 TRBs)
        println!("Initializing Command Ring...");
        let cmd_ring = TrbRing::new(256)?;
        let cmd_ring_phys = cmd_ring.phys_addr();
        regs.write_crcr((cmd_ring_phys as u64) | 1); // RCS = 1

        // 6. Initialize Event Ring (256 TRBs) & Event Ring Segment Table (ERST)
        println!("Initializing Event Ring and ERST...");
        let event_ring = EventRing::new(256)?;
        let erst = DmaBuffer::allocate(1)?;
        let erst_phys = erst.phys_addr();

        // ERST Entry 0: ring base address (64-bit), ring size (16-bit), reserved (16-bit), reserved (32-bit)
        unsafe {
            let erst_ptr = erst.virt_ptr() as *mut u32;
            std::ptr::write_volatile(erst_ptr.add(0), event_ring.phys_addr());
            std::ptr::write_volatile(erst_ptr.add(1), 0);
            std::ptr::write_volatile(erst_ptr.add(2), 256); // segment size
            std::ptr::write_volatile(erst_ptr.add(3), 0);
        }

        // 7. Configure Runtime Interrupter 0
        regs.write_erstsz(1);
        regs.write_erstba(erst_phys as u64);
        regs.write_erdp((event_ring.phys_addr() as u64) | (1 << 3)); // EHB = 1 to clear
        regs.write_imod(0);
        regs.write_iman(regs.read_iman() | 3); // IE = 1, IP = 1 to clear

        // 8. Start Host Controller (USBCMD.RS = 1, USBCMD.INTE = 1)
        println!("Starting Host Controller...");
        regs.write_usbsts(1 << 3); // Clear EINT
        regs.write_usbcmd(regs.read_usbcmd() | (1 << 0) | (1 << 2)); // RS=1, INTE=1

        timeout = 1000;
        while (regs.read_usbsts() & 1) != 0 && timeout > 0 {
            std::thread::sleep(std::time::Duration::from_millis(1));
            timeout -= 1;
        }

        let sts = regs.read_usbsts();
        if (sts & (1 << 2)) != 0 {
            return Err(format!("Host System Error (USBSTS: 0x{:x})", sts));
        }
        println!("Host controller running! (USBSTS: 0x{:x})", sts);

        Ok(Self {
            pci_dev,
            regs,
            irq_sub,
            _dcbaa: dcbaa,
            _scratchpad_array: scratchpad_array,
            _scratchpad_buffers: scratchpad_buffers,
            cmd_ring,
            event_ring,
            _erst: erst,
            device_contexts: (0..32).map(|_| None).collect(),
            input_contexts: (0..32).map(|_| None).collect(),
            ep0_rings: (0..32).map(|_| None).collect(),
            endpoint_rings: (0..32).map(|_| (0..32).map(|_| None).collect()).collect(),
            port_count: max_ports,
            max_slots: effective_slots,
        })
    }

    /// Waits for an event matching the predicate, unblocking via IRQ
    pub fn wait_for_event<F>(&mut self, mut predicate: F) -> Result<Trb, String>
    where
        F: FnMut(&Trb) -> bool,
    {
        for _ in 0..2000 {
            // Check if matching event is already in the event ring
            while let Some(ev) = self.event_ring.fetch_event() {
                // Acknowledge dequeue pointer to xHCI
                let erdp = (self.event_ring.current_dequeue_phys() as u64) | (1 << 3);
                self.regs.write_erdp(erdp);
                self.regs.write_iman(self.regs.read_iman() | 1); // Clear IP

                if predicate(&ev) {
                    return Ok(ev);
                }
            }

            // Block waiting for IRQ via honeyos-pic
            if let Some(ref mut sub) = self.irq_sub {
                if sub.wait().is_ok() {
                    continue;
                }
            }

            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        Err("Timeout waiting for event via IRQ".to_string())
    }

    pub fn send_command(&mut self, trb: Trb) -> Result<Trb, String> {
        let cmd_phys = self.cmd_ring.enqueue(trb);
        // Ring Host Controller Doorbell 0
        self.regs.ring_doorbell(0, 0);

        self.wait_for_event(|ev| {
            ev.trb_type() == TRB_COMMAND_COMPLETION_EVENT && ev.parameter == (cmd_phys as u64)
        })
    }

    pub fn enable_slot(&mut self) -> Result<u8, String> {
        let trb = Trb {
            parameter: 0,
            status: 0,
            control: TRB_ENABLE_SLOT << 10,
        };
        let completion = self.send_command(trb)?;
        let code = completion.completion_code();
        if code != 1 {
            return Err(format!("EnableSlot failed with code {}", code));
        }
        Ok(completion.slot_id())
    }

    pub fn address_device(&mut self, slot_id: u8, bsr: bool) -> Result<(), String> {
        let input_ctx_phys = self.input_contexts[slot_id as usize]
            .as_ref()
            .ok_or("Input context not found")?
            .phys_addr();

        let control = (TRB_ADDRESS_DEVICE << 10)
            | ((slot_id as u32) << 24)
            | (if bsr { 1 << 9 } else { 0 });

        let trb = Trb {
            parameter: input_ctx_phys as u64,
            status: 0,
            control,
        };
        let completion = self.send_command(trb)?;
        let code = completion.completion_code();
        if code != 1 {
            return Err(format!("AddressDevice(bsr={}) failed with code {}", bsr, code));
        }
        Ok(())
    }

    pub fn evaluate_context(&mut self, slot_id: u8) -> Result<(), String> {
        let input_ctx_phys = self.input_contexts[slot_id as usize]
            .as_ref()
            .ok_or("Input context not found")?
            .phys_addr();

        let control = (TRB_EVALUATE_CONTEXT << 10) | ((slot_id as u32) << 24);
        let trb = Trb {
            parameter: input_ctx_phys as u64,
            status: 0,
            control,
        };
        let completion = self.send_command(trb)?;
        let code = completion.completion_code();
        if code != 1 {
            return Err(format!("EvaluateContext failed with code {}", code));
        }
        Ok(())
    }

    pub fn configure_endpoint_cmd(&mut self, slot_id: u8) -> Result<(), String> {
        let input_ctx_phys = self.input_contexts[slot_id as usize]
            .as_ref()
            .ok_or("Input context not found")?
            .phys_addr();

        let control = (TRB_CONFIGURE_ENDPOINT << 10) | ((slot_id as u32) << 24);
        let trb = Trb {
            parameter: input_ctx_phys as u64,
            status: 0,
            control,
        };
        let completion = self.send_command(trb)?;
        let code = completion.completion_code();
        if code != 1 {
            return Err(format!("ConfigureEndpoint failed with code {}", code));
        }
        Ok(())
    }

    pub fn control_transfer(
        &mut self,
        slot_id: u8,
        req_type: u8,
        request: u8,
        value: u16,
        index: u16,
        buffer: Option<&mut [u8]>,
        in_direction: bool,
    ) -> Result<usize, String> {
        let length = buffer.as_ref().map_or(0, |b| b.len()) as u16;

        let mut data_dma: Option<DmaBuffer> = None;
        if length > 0 {
            let mut dma = DmaBuffer::allocate(1)?;
            if !in_direction {
                if let Some(ref buf) = buffer {
                    dma.as_mut_slice()[..length as usize].copy_from_slice(&buf[..length as usize]);
                }
            }
            data_dma = Some(dma);
        }

        let ep0_ring = self.ep0_rings[slot_id as usize]
            .as_mut()
            .ok_or("EP0 ring not found")?;

        // 1. Setup Stage TRB
        let param = (req_type as u64)
            | ((request as u64) << 8)
            | ((value as u64) << 16)
            | ((index as u64) << 32)
            | ((length as u64) << 48);

        let transfer_type = if length == 0 {
            0 // No data stage
        } else if in_direction {
            3 // IN data stage
        } else {
            2 // OUT data stage
        };

        let setup_trb = Trb {
            parameter: param,
            status: 8, // length of setup packet
            control: (TRB_SETUP_STAGE << 10) | TRB_IDT | (transfer_type << 16),
        };
        ep0_ring.enqueue(setup_trb);

        // 2. Data Stage TRB (if length > 0)
        if let Some(ref dma) = data_dma {
            let data_trb = Trb {
                parameter: dma.phys_addr() as u64,
                status: length as u32,
                control: (TRB_DATA_STAGE << 10) | (if in_direction { 1 << 16 } else { 0 }),
            };
            ep0_ring.enqueue(data_trb);
        }

        // 3. Status Stage TRB
        let status_trb = Trb {
            parameter: 0,
            status: 0,
            control: (TRB_STATUS_STAGE << 10)
                | TRB_IOC
                | (if in_direction || length == 0 {
                    0
                } else {
                    1 << 16
                }),
        };
        let status_phys = ep0_ring.enqueue(status_trb);

        // 4. Ring Doorbell for Slot, target 1 (EP0)
        self.regs.ring_doorbell(slot_id, 1);

        // 5. Await completion event via IRQ
        let ev = self.wait_for_event(|e| {
            e.trb_type() == TRB_TRANSFER_EVENT && (e.parameter == status_phys as u64 || e.slot_id() == slot_id)
        })?;

        let code = ev.completion_code();
        if code != 1 && code != 6 {
            // 1 = Success, 6 = Short Packet
            return Err(format!("Control transfer failed with code {}", code));
        }

        if in_direction && length > 0 {
            if let Some(dma) = data_dma {
                if let Some(out_buf) = buffer {
                    out_buf[..length as usize].copy_from_slice(&dma.as_slice()[..length as usize]);
                }
            }
        }

        Ok(length as usize)
    }

    pub fn get_device_descriptor(&mut self, slot_id: u8) -> Result<DeviceDescriptor, String> {
        let mut buf = [0u8; 18];
        self.control_transfer(slot_id, 0x80, 6, 1 << 8, 0, Some(&mut buf), true)?;

        let mut desc = DeviceDescriptor::default();
        unsafe {
            std::ptr::copy_nonoverlapping(
                buf.as_ptr(),
                &mut desc as *mut _ as *mut u8,
                std::mem::size_of::<DeviceDescriptor>(),
            );
        }
        Ok(desc)
    }

    pub fn get_string_descriptor(
        &mut self,
        slot_id: u8,
        str_idx: u8,
        lang_id: u16,
    ) -> Result<String, String> {
        if str_idx == 0 {
            return Ok(String::new());
        }
        let mut buf = [0u8; 255];
        let val = (3 << 8) | (str_idx as u16);
        let len = self.control_transfer(slot_id, 0x80, 6, val, lang_id, Some(&mut buf), true)?;
        parse_string_descriptor(&buf[..len]).ok_or("Failed to parse string descriptor".to_string())
    }

    pub fn get_configuration_descriptor(&mut self, slot_id: u8) -> Result<Vec<u8>, String> {
        // First read 9 bytes to get wTotalLength
        let mut header = [0u8; 9];
        self.control_transfer(slot_id, 0x80, 6, 2 << 8, 0, Some(&mut header), true)?;
        let total_length = u16::from_le_bytes([header[2], header[3]]) as usize;

        // Read full configuration descriptor
        let mut full_buf = vec![0u8; total_length];
        self.control_transfer(slot_id, 0x80, 6, 2 << 8, 0, Some(&mut full_buf), true)?;
        Ok(full_buf)
    }

    pub fn set_configuration(&mut self, slot_id: u8, config_val: u8) -> Result<(), String> {
        self.control_transfer(slot_id, 0x00, 9, config_val as u16, 0, None, false)?;
        Ok(())
    }

    pub fn enumerate_devices(&mut self) -> Vec<DiscoveredUsbDevice> {
        let mut devices = Vec::new();

        for port in 1..=self.port_count {
            let portsc = self.regs.read_portsc(port);
            let connected = (portsc & 1) != 0;
            if !connected {
                continue;
            }

            println!("Device detected on Port {}! PORTSC: 0x{:08x}", port, portsc);

            // Reset port
            let reset_cmd = (portsc & 0x0E00_C3E0) | (1 << 4); // Set PR (bit 4), preserve PP
            self.regs.write_portsc(port, reset_cmd);

            // Await port reset completion
            let mut port_enabled = false;
            for _ in 0..100 {
                std::thread::sleep(std::time::Duration::from_millis(10));
                let p = self.regs.read_portsc(port);
                if (p & (1 << 4)) == 0 && (p & (1 << 1)) != 0 {
                    port_enabled = true;
                    // Clear change bits
                    self.regs.write_portsc(port, (p & 0x0E00_C3E0) | (1 << 21) | (1 << 17));
                    break;
                }
            }

            if !port_enabled {
                println!("Port {} reset failed!", port);
                continue;
            }

            let final_portsc = self.regs.read_portsc(port);
            let speed = ((final_portsc >> 10) & 0xF) as u8;
            let (speed_name, initial_max_packet): (&'static str, u16) = match speed {
                1 => ("Full-Speed (12 Mbps)", 8),
                2 => ("Low-Speed (1.5 Mbps)", 8),
                3 => ("High-Speed (480 Mbps)", 64),
                4 => ("SuperSpeed (5 Gbps)", 512),
                _ => ("Unknown", 8),
            };
            println!("Port {} enabled at {}", port, speed_name);

            // Enable slot
            let slot_id = match self.enable_slot() {
                Ok(id) => id,
                Err(e) => {
                    println!("Failed to enable slot for port {}: {}", port, e);
                    continue;
                }
            };
            println!("Assigned Slot ID {} to Port {}", slot_id, port);

            // Allocate Device Context (1 page) and put physical address in DCBAA[slot_id]
            let dev_ctx = match DmaBuffer::allocate(1) {
                Ok(b) => b,
                Err(e) => {
                    println!("Failed to allocate device context: {}", e);
                    continue;
                }
            };
            let dev_ctx_phys = dev_ctx.phys_addr();
            self.device_contexts[slot_id as usize] = Some(dev_ctx);

            unsafe {
                let dcbaa_ptr = self._dcbaa.virt_ptr() as *mut u64;
                std::ptr::write_volatile(dcbaa_ptr.add(slot_id as usize), dev_ctx_phys as u64);
            }

            // Create EP0 ring
            let ep0_ring = match TrbRing::new(256) {
                Ok(r) => r,
                Err(e) => {
                    println!("Failed to create EP0 ring: {}", e);
                    continue;
                }
            };
            let ep0_ring_phys = ep0_ring.phys_addr();
            self.ep0_rings[slot_id as usize] = Some(ep0_ring);

            // Create Input Context
            let input_ctx = match allocate_input_context(port, speed, initial_max_packet, ep0_ring_phys) {
                Ok(b) => b,
                Err(e) => {
                    println!("Failed to allocate input context: {}", e);
                    continue;
                }
            };
            self.input_contexts[slot_id as usize] = Some(input_ctx);

            // Address Device (BSR=1 then BSR=0)
            if let Err(e) = self.address_device(slot_id, true) {
                println!("AddressDevice(BSR=1) failed: {}", e);
                continue;
            }
            if let Err(e) = self.address_device(slot_id, false) {
                println!("AddressDevice(BSR=0) failed: {}", e);
                continue;
            }

            // Read 8-byte Device Descriptor to get actual max packet size
            let mut desc_header = [0u8; 8];
            if let Ok(_) = self.control_transfer(slot_id, 0x80, 6, 1 << 8, 0, Some(&mut desc_header), true) {
                let actual_max_packet = if speed == 4 {
                    1 << desc_header[7]
                } else {
                    desc_header[7] as u16
                };

                if actual_max_packet != initial_max_packet && actual_max_packet > 0 {
                    // Update Input Context & Evaluate Context
                    let input_ctx = self.input_contexts[slot_id as usize].as_mut().unwrap();
                    let input_ptr = input_ctx.virt_ptr() as *mut InputContext;
                    unsafe {
                        (*input_ptr).control.add_flags = 1 << 1; // Add EP0 Context
                        (*input_ptr).device.endpoints[0].dword1 =
                            (3 << 1) | (4 << 3) | ((actual_max_packet as u32) << 16);
                    }
                    let _ = self.evaluate_context(slot_id);
                }
            }

            // Read full 18-byte Device Descriptor
            let dev_desc = match self.get_device_descriptor(slot_id) {
                Ok(d) => d,
                Err(e) => {
                    println!("Failed to read full device descriptor: {}", e);
                    continue;
                }
            };

            // Query String Descriptors
            let lang_id = 0x0409; // English (US)
            let mfg = self
                .get_string_descriptor(slot_id, dev_desc.i_manufacturer, lang_id)
                .unwrap_or_else(|_| "Unknown Manufacturer".to_string());
            let prod = self
                .get_string_descriptor(slot_id, dev_desc.i_product, lang_id)
                .unwrap_or_else(|_| "Unknown Product".to_string());
            let serial = self
                .get_string_descriptor(slot_id, dev_desc.i_serial_number, lang_id)
                .unwrap_or_else(|_| "No Serial".to_string());

            // Read Configuration Descriptor
            let config_bytes = match self.get_configuration_descriptor(slot_id) {
                Ok(b) => b,
                Err(e) => {
                    println!("Failed to read configuration descriptor: {}", e);
                    continue;
                }
            };

            let (config, interfaces) = match parse_configuration(&config_bytes) {
                Some(res) => res,
                None => {
                    println!("Failed to parse configuration descriptor!");
                    continue;
                }
            };

            // Configure endpoints for all interfaces
            let input_ctx = self.input_contexts[slot_id as usize].as_mut().unwrap();
            let input_ptr = input_ctx.virt_ptr() as *mut InputContext;
            let mut add_flags = 1u32; // bit 0: Slot Context
            let mut max_ctx_entry = 1u8;

            for iface in &interfaces {
                for ep in &iface.endpoints {
                    let ep_index = ((ep.endpoint_number as usize) * 2) - 1 + (if ep.is_in { 1 } else { 0 });
                    if ep_index >= 31 {
                        continue;
                    }

                    if let Ok(ep_ring) = TrbRing::new(256) {
                        let ring_phys = ep_ring.phys_addr();
                        self.endpoint_rings[slot_id as usize][ep_index] = Some(ep_ring);

                        let ep_type = match (ep.transfer_type, ep.is_in) {
                            (1, false) => 1, // Isoch OUT
                            (2, false) => 2, // Bulk OUT
                            (3, false) => 3, // Interrupt OUT
                            (1, true) => 5,  // Isoch IN
                            (2, true) => 6,  // Bulk IN
                            (3, true) => 7,  // Interrupt IN
                            _ => 4,
                        };

                        let ep_ctx = EndpointContext::new_transfer(
                            ep_type,
                            ep.max_packet_size,
                            ep.interval,
                            ring_phys,
                        );

                        unsafe {
                            (*input_ptr).device.endpoints[ep_index] = ep_ctx;
                        }
                        add_flags |= 1 << (ep_index + 1);
                        max_ctx_entry = max_ctx_entry.max(ep_index as u8 + 1);
                    }
                }
            }

            unsafe {
                (*input_ptr).control.add_flags = add_flags;
                (*input_ptr).device.slot.dword0 =
                    ((*input_ptr).device.slot.dword0 & !(0x1Fu32 << 27)) | ((max_ctx_entry as u32) << 27);
            }

            if let Err(e) = self.configure_endpoint_cmd(slot_id) {
                println!("ConfigureEndpoint command failed: {}", e);
            }

            // Set configuration
            if let Err(e) = self.set_configuration(slot_id, config.b_configuration_value) {
                println!("SetConfiguration failed: {}", e);
            }

            // Prepare VFS device nodes and endpoint paths in /dev/usb/<slot>/
            let dev_dir = format!("/dev/usb/{}", slot_id);
            let _ = std::fs::create_dir_all(&dev_dir);
            let info_path = format!("{}/info", dev_dir);
            if let Ok(mut f) = OpenOptions::new().create(true).write(true).truncate(true).open(&info_path) {
                let vid = dev_desc.id_vendor;
                let pid = dev_desc.id_product;
                let dev_class = dev_desc.b_device_class;
                let dev_subclass = dev_desc.b_device_sub_class;
                let _ = writeln!(f, "vendor: 0x{:04x}", vid);
                let _ = writeln!(f, "product: 0x{:04x}", pid);
                let _ = writeln!(f, "class: {}", dev_class);
                let _ = writeln!(f, "subclass: {}", dev_subclass);
                let _ = writeln!(f, "manufacturer: {}", mfg);
                let _ = writeln!(f, "product_name: {}", prod);
                let _ = writeln!(f, "serial: {}", serial);
            }

            let ctrl_fifo = format!("{}/control\0", dev_dir);
            unsafe {
                mkfifo(ctrl_fifo.as_ptr(), 0);
            }

            for iface in &interfaces {
                for ep in &iface.endpoints {
                    let ep_fifo = if ep.is_in {
                        format!("{}/ep{:02x}_in\0", dev_dir, ep.endpoint_number)
                    } else {
                        format!("{}/ep{:02x}_out\0", dev_dir, ep.endpoint_number)
                    };
                    unsafe {
                        mkfifo(ep_fifo.as_ptr(), 0);
                    }
                }
            }
            println!("Prepared VFS endpoint paths in /dev/usb/{}/", slot_id);

            devices.push(DiscoveredUsbDevice {
                slot_id,
                port_id: port,
                speed_name,
                vendor_id: dev_desc.id_vendor,
                product_id: dev_desc.id_product,
                device_class: dev_desc.b_device_class,
                device_subclass: dev_desc.b_device_sub_class,
                manufacturer: mfg,
                product: prod,
                serial,
                config,
                interfaces,
            });
        }

        devices
    }
}
