use std::fs::{read_link, OpenOptions};
use std::io::{self, Write};

extern "C" {
    fn open(path: *const u8, flags: i32, ...) -> i32;
    fn read(fd: i32, buf: *mut u8, count: usize) -> isize;
    fn close(fd: i32) -> i32;
}

/// Read a symbolic link target using HoneyOS O_SYMLINK (0x1000)
pub fn read_symlink(path: &str) -> io::Result<String> {
    let cpath =
        std::ffi::CString::new(path).map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;
    const O_RDONLY: i32 = 0;
    const O_SYMLINK: i32 = 0x1000;
    let fd = unsafe { open(cpath.as_ptr() as *const u8, O_RDONLY | O_SYMLINK) };
    if fd < 0 {
        return Err(io::Error::last_os_error());
    }
    let mut buf = [0u8; 256];
    let n = unsafe { read(fd, buf.as_mut_ptr(), buf.len() - 1) };
    unsafe { close(fd) };
    if n < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(String::from_utf8_lossy(&buf[..n as usize])
        .trim()
        .to_string())
}

#[derive(Debug, Clone)]
pub struct PciBar {
    pub index: u8,
    pub phys_base: u32,
    pub size: u32,
    pub symlink_path: String,
    pub target_mem_path: String,
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
    pub class_name: String,
    pub irq: u8,
    pub bars: Vec<PciBar>,
}

impl PciDevice {
    pub fn from_path(dev_dir: &String) -> Result<PciDevice, &str> {
        let name = dev_dir.split('/').last().unwrap();
        let parts: Vec<&str> = name.split(|c| c == ':' || c == '.').collect();
        if parts.len() != 3 {
            return Err("Bad path parts");
        }

        let bus: u8 = match parts[0].parse() {
            Ok(b) => b,
            Err(_) => return Err("Bad bus"),
        };
        let device: u8 = match parts[1].parse() {
            Ok(d) => d,
            Err(_) => return Err("bad device"),
        };
        let function: u8 = match parts[2].parse() {
            Ok(f) => f,
            Err(_) => return Err("bad function"),
        };

        let read_u32 = |sub: &str| -> u32 {
            read_small_file(&format!("{}/{}", dev_dir, sub))
                .ok()
                .and_then(|s| {
                    let trimmed = s.trim();
                    if trimmed.starts_with("0x") || trimmed.starts_with("0X") {
                        u32::from_str_radix(&trimmed[2..], 16).ok()
                    } else {
                        trimmed.parse().ok()
                    }
                })
                .unwrap_or(0)
        };

        let read_str = |sub: &str| -> String {
            read_small_file(&format!("{}/{}", dev_dir, sub)).unwrap_or_default()
        };

        let vendor_id = read_u32("vendor") as u16;
        let device_id = read_u32("device") as u16;
        let class = read_u32("class") as u8;
        let subclass = read_u32("subclass") as u8;
        let prog_if = read_u32("programming_interface") as u8;
        let class_name = read_str("class_name");
        let irq = read_u32("irq") as u8;

        let mut bars = Vec::new();

        for bar_idx in 0..6 {
            let mut phys_base = 0u32;
            let mut size = 0u32;
            let mut target_str = String::new();

            let bar_link_path = format!("{}/bar/{}", dev_dir, bar_idx);
            if let Ok(target) = read_symlink(&bar_link_path) {
                if let Some(mem_part) = target.strip_prefix("/kernel/mem/") {
                    let splits: Vec<&str> = mem_part.split('+').collect();
                    if splits.len() == 2 {
                        let p_str = splits[0].trim_start_matches("0x").trim_start_matches("0X");
                        let s_str = splits[1].trim_start_matches("0x").trim_start_matches("0X");
                        phys_base = u32::from_str_radix(p_str, 16).unwrap_or(0);
                        size = u32::from_str_radix(s_str, 16).unwrap_or(0);
                        target_str = target;
                        bars.push(PciBar {
                            index: bar_idx,
                            phys_base,
                            size,
                            symlink_path: bar_link_path,
                            target_mem_path: target_str,
                        });
                    }
                }
            }
        }

        Ok(PciDevice {
            bus,
            device,
            function,
            vendor_id,
            device_id,
            class,
            subclass,
            prog_if,
            class_name,
            irq,
            bars,
        })
    }
}

impl PciDevice {
    /// Enable PCI Bus Mastering and Memory Space on this device
    pub fn enable_bus_mastering(&self) -> io::Result<()> {
        let dev_id = format!("{}:{}.{}", self.bus, self.device, self.function);

        // Try writing "1" to device-specific bus_master file if present
        let dev_bm_path = format!("/dev/pci/{}/bus_master", dev_id);
        if let Ok(mut f) = OpenOptions::new().write(true).open(&dev_bm_path) {
            let _ = f.write_all(b"1");
            let _ = f.flush();
            return Ok(());
        }

        // Otherwise send command to /dev/pci/control
        if let Ok(mut f) = OpenOptions::new().write(true).open("/dev/pci/control") {
            let cmd = format!("BUS_MASTER {}\n", dev_id);
            let _ = f.write_all(cmd.as_bytes());
            let _ = f.flush();
            return Ok(());
        }

        // Direct port I/O fallback (0xCF8 / 0xCFC or 3320 / 3324)
        if let (Ok(mut addr_f), Ok(mut data_f)) = (
            OpenOptions::new()
                .write(true)
                .open("/dev/port/0xCF8"),
            OpenOptions::new()
                .read(true)
                .write(true)
                .open("/dev/port/0xCFC")
        ) {
            use std::io::Read;
            let address: u32 = 0x8000_0000
                | ((self.bus as u32) << 16)
                | ((self.device as u32) << 11)
                | ((self.function as u32) << 8)
                | 0x04;
            if addr_f.write_all(&address.to_le_bytes()).is_ok() {
                let mut val_bytes = [0u8; 4];
                if data_f.read_exact(&mut val_bytes).is_ok() {
                    let cmd = u32::from_le_bytes(val_bytes);
                    let new_cmd = (cmd | (1 << 1) | (1 << 2)) & !(1 << 10);
                    let _ = addr_f.write_all(&address.to_le_bytes());
                    let _ = data_f.write_all(&new_cmd.to_le_bytes());
                    return Ok(());
                }
            }
        }

        Err(io::Error::new(
            io::ErrorKind::NotFound,
            "Could not open PCI control interface",
        ))
    }
}

/// Read a small text file safely in HoneyOS with a single read syscall
pub fn read_small_file(path: &str) -> io::Result<String> {
    use std::fs::File;
    use std::io::Read;
    let mut f = File::open(path)?;
    let mut buf = [0u8; 512];
    let n = f.read(&mut buf)?;
    Ok(String::from_utf8_lossy(&buf[..n]).trim().to_string())
}

pub struct PciScanner;

impl PciScanner {
    /// Scan /dev/pci/ and return all discovered PCI devices
    pub fn scan() -> io::Result<Vec<PciDevice>> {
        let mut devices = Vec::new();
        let mut dev_names = Vec::new();

        // 1. Try reading /dev/pci/devices
        if let Ok(dev_list) = read_small_file("/dev/pci/devices") {
            for line in dev_list.lines() {
                let trimmed = line.trim();
                if !trimmed.is_empty() {
                    dev_names.push(trimmed.to_string());
                }
            }
        }

        // 2. Fallback: probe all possible PCI functions directly via read_small_file on "vendor"
        if dev_names.is_empty() {
            for bus in 0..8 {
                for dev in 0..32 {
                    for func in 0..8 {
                        let path = format!("/dev/pci/{}:{}.{}/vendor", bus, dev, func);
                        if read_small_file(&path).is_ok() {
                            dev_names.push(format!("{}:{}.{}", bus, dev, func));
                        }
                    }
                }
            }
        }

        for name in dev_names {
            // Expected format: <bus>:<device>.<function>
            let path = format!("/dev/pci/{}", name);
            let device = PciDevice::from_path(&path);
            if device.is_ok() {
                devices.push(device.unwrap());
            }
        }

        Ok(devices)
    }
}
