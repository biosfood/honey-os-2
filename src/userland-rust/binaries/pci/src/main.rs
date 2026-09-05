use std::fs::OpenOptions;
use std::io::{Read, Write};

extern "C" {
    fn mkdir(path: *const u8, mode: u32) -> i32;
    fn symlink(target: *const u8, linkpath: *const u8) -> i32;
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

const CLASS_NAMES: &[&str] = &[
    "Unclassified",
    "Mass Storage Controller",
    "Network controller",
    "Display controller",
    "Multimedia controller",
    "Memory Controller",
    "Bridge",
    "Simple Communication controller",
    "Base System Peripheral",
    "Input Device controller",
    "Docking station",
    "Processor",
    "Serial bus controller",
    "Wireless controller",
    "intelligent controller",
    "satellite communication controller",
    "encryption controller",
    "signal processing controller",
    "processing accelerator",
    "non-essential instrumentation",
];

struct PciConfigPorts {
    addr_file: std::fs::File,
    data_file: std::fs::File,
}

impl PciConfigPorts {
    fn new() -> Self {
        let addr_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/port/0xCF8")
            .expect("Failed to open PCI address port");

        let data_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/port/0xCFC")
            .expect("Failed to open PCI data port");

        Self {
            addr_file,
            data_file,
        }
    }

    fn read32(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u32 {
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

    fn write32(&mut self, bus: u8, device: u8, function: u8, offset: u8, val: u32) {
        let address: u32 = 0x8000_0000
            | ((bus as u32) << 16)
            | ((device as u32) << 11)
            | ((function as u32) << 8)
            | ((offset as u32) & 0xFC);

        self.addr_file.write_all(&address.to_le_bytes()).unwrap();
        self.data_file.write_all(&val.to_le_bytes()).unwrap();
    }

    fn read16(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u16 {
        let val32 = self.read32(bus, device, function, offset);
        let shift = (offset % 4) * 8;
        ((val32 >> shift) & 0xFFFF) as u16
    }

    fn write16(&mut self, bus: u8, device: u8, function: u8, offset: u8, val: u16) {
        let shift = (offset % 4) * 8;
        let mask = !(0xFFFFu32 << shift);
        let orig = self.read32(bus, device, function, offset);
        let new_val = (orig & mask) | ((val as u32) << shift);
        self.write32(bus, device, function, offset, new_val);
    }

    fn read8(&mut self, bus: u8, device: u8, function: u8, offset: u8) -> u8 {
        let val32 = self.read32(bus, device, function, offset);
        let shift = (offset % 4) * 8;
        ((val32 >> shift) & 0xFF) as u8
    }

    fn enable_bus_mastering(&mut self, bus: u8, device: u8, function: u8) {
        let cmd = self.read16(bus, device, function, 0x04);
        // Bit 1 = Memory Space Enable, Bit 2 = Bus Master Enable, clear Bit 10 (Interrupt Disable)
        let new_cmd = (cmd | (1 << 1) | (1 << 2)) & !(1 << 10);
        self.write16(bus, device, function, 0x04, new_cmd);
    }
}

fn write_file(dir: &str, name: &str, content: &str) {
    let path = format!("{}/{}", dir, name);
    if let Ok(mut f) = OpenOptions::new().create(true).write(true).truncate(true).open(&path) {
        let _ = f.write_all(content.as_bytes());
        let _ = f.flush();
    }
}

fn scan_function(ports: &mut PciConfigPorts, bus: u8, device: u8, function: u8, dev_list: &mut Vec<String>) {
    let vendor_id = ports.read16(bus, device, function, 0x00);
    if vendor_id == 0xFFFF || vendor_id == 0 {
        return;
    }

    let class = ports.read8(bus, device, function, 0x0B);
    if class == 0 || class == 0xFF {
        return;
    }

    let device_id = ports.read16(bus, device, function, 0x02);
    let configuration = ports.read16(bus, device, function, 0x04);
    let subclass = ports.read8(bus, device, function, 0x0A);
    let prog_if = ports.read8(bus, device, function, 0x09);
    let irq = ports.read8(bus, device, function, 0x3C);

    let dev_dir = format!("/dev/pci/{}:{}.{}\0", bus, device, function);
    let bar_dir = format!("/dev/pci/{}:{}.{}/bar\0", bus, device, function);
    unsafe {
        mkdir(dev_dir.as_ptr(), 0);
        mkdir(bar_dir.as_ptr(), 0);
    }

    let class_name = if (class as usize) < CLASS_NAMES.len() {
        CLASS_NAMES[class as usize]
    } else {
        "Unknown"
    };

    println!("{}:{}.{}: class {} ({})", bus, device, function, class, class_name);

    // Size and expose BARs
    let mut i = 0;
    while i < 6 {
        let bar_offset = 0x10 + 4 * i;
        let orig = ports.read32(bus, device, function, bar_offset);
        if orig != 0 && orig != 0xFFFF_FFFF {
            let is_io = (orig & 1) != 0;
            let mem_type = (orig >> 1) & 3;
            let is_64bit = mem_type == 2;

            if !is_io {
                ports.write32(bus, device, function, bar_offset, 0xFFFF_FFFF);
                let mask = ports.read32(bus, device, function, bar_offset);
                ports.write32(bus, device, function, bar_offset, orig);

                if mask != 0 && mask != 0xFFFF_FFFF {
                    let size = !(mask & 0xFFFF_FFF0) + 1;
                    let phys_base = orig & 0xFFFF_FFF0;
                    if size > 0 && phys_base > 0 {
                        let target = format!("/kernel/mem/0x{:x}+0x{:x}\0", phys_base, size);
                        let linkpath = format!("/dev/pci/{}:{}.{}/bar/{}\0", bus, device, function, i);
                        unsafe {
                            symlink(target.as_ptr(), linkpath.as_ptr());
                        }
                        println!("  bar {} -> /kernel/mem/0x{:x}+0x{:x}", i, phys_base, size);
                        let dev_dir_str = &dev_dir[..dev_dir.len() - 1];
                        write_file(dev_dir_str, &format!("bar{}", i), &format!("0x{:x}+0x{:x}", phys_base, size));
                    }
                }
            }

            if is_64bit && i < 5 {
                i += 1;
            }
        }
        i += 1;
    }

    // Automatically enable Memory Space & Bus Mastering for functional devices
    ports.enable_bus_mastering(bus, device, function);

    let dev_dir_str = &dev_dir[..dev_dir.len() - 1];
    write_file(dev_dir_str, "class", &format!("{}", class));
    write_file(dev_dir_str, "class_name", class_name);
    write_file(dev_dir_str, "vendor", &format!("{}", vendor_id));
    write_file(dev_dir_str, "device", &format!("{}", device_id));
    write_file(dev_dir_str, "configuration", &format!("{}", configuration));
    write_file(dev_dir_str, "subclass", &format!("{}", subclass));
    write_file(dev_dir_str, "programming_interface", &format!("{}", prog_if));
    write_file(dev_dir_str, "irq", &format!("{}", irq));
    write_file(dev_dir_str, "bus_master", "1");

    dev_list.push(format!("{}:{}.{}", bus, device, function));
}

fn scan_device(ports: &mut PciConfigPorts, bus: u8, device: u8, dev_list: &mut Vec<String>) {
    let vendor_id = ports.read16(bus, device, 0, 0x00);
    if vendor_id == 0xFFFF || vendor_id == 0 {
        return;
    }

    let header_type = ports.read8(bus, device, 0, 0x0E);
    if (header_type & 0x80) != 0 {
        // Multifunction device
        for func in 0..8 {
            scan_function(ports, bus, device, func, dev_list);
        }
    } else {
        scan_function(ports, bus, device, 0, dev_list);
    }
}

fn scan_all(ports: &mut PciConfigPorts) -> usize {
    let mut dev_list = Vec::new();

    // Check if host bridge is multifunction to determine bus count (standard PCI scan)
    let host_header = ports.read8(0, 0, 0, 0x0E);
    let max_buses = if (host_header & 0x80) != 0 { 8 } else { 1 };

    for bus in 0..max_buses {
        for device in 0..32 {
            scan_device(ports, bus, device, &mut dev_list);
        }
    }

    let mut content = dev_list.join("\n");
    content.push('\n');
    write_file("/dev/pci", "devices", &content);
    println!("Wrote {} devices to /dev/pci/devices", dev_list.len());
    dev_list.len()
}

fn main() {
    unsafe {
        mkdir(b"/dev/pci\0".as_ptr(), 0);
        mkfifo(b"/dev/pci/rescan\0".as_ptr(), 0);
    }

    let mut ports = PciConfigPorts::new();
    scan_all(&mut ports);

    println!("ready");

    let mut rescan = match OpenOptions::new().read(true).open("/dev/pci/rescan") {
        Ok(f) => f,
        Err(_) => {
            let mut dummy = [0u8; 1];
            loop {
                let _ = std::io::stdin().read(&mut dummy);
            }
        }
    };

    let mut buf = [0u8; 16];
    loop {
        let n = rescan.read(&mut buf).unwrap_or(0);
        if n > 0 {
            println!("Rescanning PCI bus...");
            scan_all(&mut ports);
        }
    }
}
