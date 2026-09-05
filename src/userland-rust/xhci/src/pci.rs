use std::fs::{File, OpenOptions};
use std::io::{Read, Write};

pub struct PciConfig {
    addr_file: File,
    data_file: File,
}

impl PciConfig {
    pub fn new() -> Result<Self, String> {
        let addr_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/port/0xCF8")
            .or_else(|_| {
                OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open("/dev/port/3320")
            })
            .map_err(|e| format!("Failed to open PCI config address port: {}", e))?;

        let data_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/port/0xCFC")
            .or_else(|_| {
                OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open("/dev/port/3324")
            })
            .map_err(|e| format!("Failed to open PCI config data port: {}", e))?;

        Ok(Self {
            addr_file,
            data_file,
        })
    }

    pub fn read32(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u32 {
        let address: u32 = 0x8000_0000
            | ((bus as u32) << 16)
            | ((device as u32) << 11)
            | ((function as u32) << 8)
            | ((offset as u32) & 0xFC);

        self.addr_file.write_all(&address.to_le_bytes()).unwrap();
        let mut buf = [0u8; 4];
        self.data_file.read_exact(&mut buf).unwrap();
        u32::from_le_bytes(buf)
    }

    pub fn write32(&mut self, bus: u8, device: u8, function: u8, offset: u8, val: u32) {
        let address: u32 = 0x8000_0000
            | ((bus as u32) << 16)
            | ((device as u32) << 11)
            | ((function as u32) << 8)
            | ((offset as u32) & 0xFC);

        self.addr_file.write_all(&address.to_le_bytes()).unwrap();
        self.data_file.write_all(&val.to_le_bytes()).unwrap();
    }

    pub fn read16(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u16 {
        let val32 = self.read32(bus, device, function, offset);
        let shift = (offset % 4) * 8;
        ((val32 >> shift) & 0xFFFF) as u16
    }

    pub fn write16(&mut self, bus: u8, device: u8, function: u8, offset: u8, val: u16) {
        let shift = (offset % 4) * 8;
        let mask = !(0xFFFFu32 << shift);
        let orig = self.read32(bus, device, function, offset);
        let new_val = (orig & mask) | ((val as u32) << shift);
        self.write32(bus, device, function, offset, new_val);
    }

    pub fn read8(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u8 {
        let val32 = self.read32(bus, device, function, offset);
        let shift = (offset % 4) * 8;
        ((val32 >> shift) & 0xFF) as u8
    }

    pub fn write8(&mut self, bus: u8, device: u8, function: u8, offset: u8, val: u8) {
        let shift = (offset % 4) * 8;
        let mask = !(0xFFu32 << shift);
        let orig = self.read32(bus, device, function, offset);
        let new_val = (orig & mask) | ((val as u32) << shift);
        self.write32(bus, device, function, offset, new_val);
    }
}

#[derive(Debug, Clone)]
pub struct PciDevice {
    pub bus: u8,
    pub device: u8,
    pub function: u8,
    pub vendor_id: u16,
    pub device_id: u16,
    pub class: u8,
    pub subclass: u8,
    pub prog_if: u8,
    pub bar0: u32,
    pub bar0_size: u32,
    pub irq: u8,
}

impl PciConfig {
    pub fn find_xhci(&mut self) -> Result<PciDevice, String> {
        for bus in 0..8 {
            for device in 0..32 {
                let vendor = self.read16(bus, device, 0, 0x00);
                if vendor == 0xFFFF || vendor == 0 {
                    continue;
                }

                let header_type = self.read8(bus, device, 0, 0x0E);
                let func_limit = if (header_type & 0x80) != 0 { 8 } else { 1 };

                for function in 0..func_limit {
                    let v = self.read16(bus, device, function, 0x00);
                    if v == 0xFFFF || v == 0 {
                        continue;
                    }

                    let class = self.read8(bus, device, function, 0x0B);
                    let subclass = self.read8(bus, device, function, 0x0A);
                    let prog_if = self.read8(bus, device, function, 0x09);

                    // Class 0x0C = Serial Bus Controller, Subclass 0x03 = USB, Prog IF 0x30 = xHCI
                    if class == 0x0C && subclass == 0x03 && prog_if == 0x30 {
                        let dev_id = self.read16(bus, device, function, 0x02);
                        let bar0_raw = self.read32(bus, device, function, 0x10);
                        let bar0_phys = bar0_raw & 0xFFFF_FFF0;

                        // Size BAR0
                        self.write32(bus, device, function, 0x10, 0xFFFF_FFFF);
                        let mask = self.read32(bus, device, function, 0x10);
                        self.write32(bus, device, function, 0x10, bar0_raw);

                        let bar0_size = if mask != 0 && mask != 0xFFFF_FFFF {
                            !(mask & 0xFFFF_FFF0) + 1
                        } else {
                            0x4000
                        };

                        let irq = self.read8(bus, device, function, 0x3C);

                        return Ok(PciDevice {
                            bus,
                            device,
                            function,
                            vendor_id: v,
                            device_id: dev_id,
                            class,
                            subclass,
                            prog_if,
                            bar0: bar0_phys,
                            bar0_size,
                            irq,
                        });
                    }
                }
            }
        }
        Err("No xHCI host controller found on PCI bus".to_string())
    }

    pub fn enable_bus_mastering(&mut self, dev: &PciDevice) {
        let cmd = self.read16(dev.bus, dev.device, dev.function, 0x04);
        // Bit 1 = Memory Space Enable, Bit 2 = Bus Master Enable, clear Bit 10 (Interrupt Disable)
        let new_cmd = (cmd | (1 << 1) | (1 << 2)) & !(1 << 10);
        self.write16(dev.bus, dev.device, dev.function, 0x04, new_cmd);
    }
}

pub fn unmask_irq(irq: u8) {
    if irq < 8 {
        if let Ok(mut f) = OpenOptions::new().read(true).write(true).open("/dev/port/0x21") {
            let mut b = [0u8; 1];
            let _ = f.read_exact(&mut b);
            b[0] &= !(1 << irq);
            let _ = f.write_all(&b);
        }
    } else {
        // Unmask cascade (bit 2) on master PIC
        if let Ok(mut f) = OpenOptions::new().read(true).write(true).open("/dev/port/0x21") {
            let mut b = [0u8; 1];
            let _ = f.read_exact(&mut b);
            b[0] &= !(1 << 2);
            let _ = f.write_all(&b);
        }
        // Unmask specific IRQ on slave PIC
        if let Ok(mut f) = OpenOptions::new().read(true).write(true).open("/dev/port/0xA1") {
            let mut b = [0u8; 1];
            let _ = f.read_exact(&mut b);
            b[0] &= !(1 << (irq - 8));
            let _ = f.write_all(&b);
        }
    }
}
