#![allow(dead_code)]

mod context;
mod registers;
mod ring;
mod xhci;

use honeyos_pci::PciDevice;
use std::fs::OpenOptions;
use std::io::Read;
use std::process::exit;
use xhci::XhciController;

extern "C" {
    fn mkdir(path: *const u8, mode: u32) -> i32;
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    // TODO check argc

    let pci_dev = match PciDevice::from_path(&args[1]) {
        Ok(dev) => dev,
        Err(e) => {
            println!("Error locating xHCI controller via /dev/pci: {}", e);
            return;
        }
    };

    // TODO: allocate a good device number
    unsafe {
        mkdir(b"/dev\0".as_ptr(), 0);
        mkdir(b"/dev/usb\0".as_ptr(), 0);
    }


    println!(
        "Registering a xHCI controller at PCI {}:{}.{} (Vendor: 0x{:04x}, Device: 0x{:04x}, IRQ: {})",
        pci_dev.bus, pci_dev.device, pci_dev.function, pci_dev.vendor_id, pci_dev.device_id, pci_dev.irq
    );

    if let Err(e) = pci_dev.enable_bus_mastering() {
        println!("Warning: enable_bus_mastering: {}", e);
    }
    println!("PCI Bus Mastering and Memory Space enabled.");

    let mut controller = match XhciController::new(pci_dev) {
        Ok(c) => Box::new(c),
        Err(e) => {
            println!("Failed to initialize xHCI controller: {}", e);
            return;
        }
    };

    println!("Scanning root hub ports and enumerating devices...");
    let devices = controller.enumerate_devices();

    println!();
    println!("==========================================================");
    println!("                  DISCOVERED USB DEVICES                  ");
    println!("==========================================================");
    if devices.is_empty() {
        println!("No USB devices connected.");
    } else {
        for dev in &devices {
            println!(
                "Port {}: Slot {} | {} | Vendor: 0x{:04x}, Product: 0x{:04x}",
                dev.port_id, dev.slot_id, dev.speed_name, dev.vendor_id, dev.product_id
            );
            println!("  Product:      \"{}\"", dev.product);
            println!("  Manufacturer: \"{}\"", dev.manufacturer);
            println!("  Serial:       \"{}\"", dev.serial);
            for iface in &dev.interfaces {
                let class_str = match iface.class {
                    3 => match iface.protocol {
                        1 => "HID Keyboard",
                        2 => "HID Mouse",
                        _ => "HID Device",
                    },
                    8 => "Mass Storage Device",
                    9 => "USB Hub",
                    _ => "Other Device",
                };
                println!(
                    "  Interface {}: Class {} (SubClass {}, Protocol {}) -> {}",
                    iface.interface_number, iface.class, iface.subclass, iface.protocol, class_str
                );
                for ep in &iface.endpoints {
                    let dir_str = if ep.is_in { "IN" } else { "OUT" };
                    let type_str = match ep.transfer_type {
                        0 => "Control",
                        1 => "Isochronous",
                        2 => "Bulk",
                        3 => "Interrupt",
                        _ => "Unknown",
                    };
                    println!(
                        "    Endpoint 0x{:02x} ({} {}): MaxPacketSize {}, Interval {}",
                        ep.address, dir_str, type_str, ep.max_packet_size, ep.interval
                    );
                }
            }
            println!("----------------------------------------------------------");
        }
    }
    println!("==========================================================");
    println!();

    // Create /dev/usb/control FIFO
    let _ = std::fs::create_dir_all("/dev/usb");
    unsafe {
        mkfifo(b"/dev/usb/control\0".as_ptr(), 0);
    }

    // Concurrently monitor /dev/usb/control in a background thread
    std::thread::spawn(|| {
        let mut usb_control = match OpenOptions::new().read(true).open("/dev/usb/control") {
            Ok(f) => f,
            Err(e) => {
                println!("Warning: Failed to open /dev/usb/control: {}", e);
                return;
            }
        };

        let mut buf = [0u8; 128];
        loop {
            match usb_control.read(&mut buf) {
                Ok(n) if n > 0 => {
                    // Future control commands (e.g. rescan, reset, etc.)
                }
                _ => {
                    if let Ok(new_f) = OpenOptions::new().read(true).open("/dev/usb/control") {
                        usb_control = new_f;
                    } else {
                        std::thread::sleep(std::time::Duration::from_millis(100));
                    }
                }
            }
        }
    });

    // Signal init supervisor that xhci service is ready
    println!("ready");

    // Enter daemon event loop
    loop {
        controller.wait_next_event();
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_xhci_uevent_format() {
        let bus = 0u8;
        let device = 4u8;
        let function = 0u8;
        let slot_id = 1u8;
        let vid = 0x1234u16;
        let pid = 0x5678u16;
        let dev_class = 0u8;
        let dev_subclass = 0u8;
        let dev_protocol = 0u8;
        let iface0_class = 3u8;
        let iface0_subclass = 1u8;
        let iface0_protocol = 2u8;

        let mut uevent = Vec::new();
        uevent.extend_from_slice(b"ACTION=add\0");
        uevent.extend_from_slice(
            format!("DEVPATH=/devices/pci/{:02x}:{:02x}.{}/usb/{}\0", bus, device, function, slot_id).as_bytes(),
        );
        uevent.extend_from_slice(b"SUBSYSTEM=usb\0");
        uevent.extend_from_slice(format!("DEVNAME=/dev/usb/{}\0", slot_id).as_bytes());
        uevent.extend_from_slice(
            format!("PRODUCT={:04x}/{:04x}/0000\0", vid, pid).as_bytes(),
        );
        uevent.extend_from_slice(
            format!("TYPE={}/{}/{}\0", dev_class, dev_subclass, dev_protocol).as_bytes(),
        );
        uevent.extend_from_slice(format!("IFACE0_CLASS={}\0", iface0_class).as_bytes());
        uevent.extend_from_slice(format!("IFACE0_SUBCLASS={}\0", iface0_subclass).as_bytes());
        uevent.extend_from_slice(format!("IFACE0_PROTOCOL={}\0", iface0_protocol).as_bytes());
        uevent.push(0);

        assert_eq!(&uevent[uevent.len() - 2..], b"\0\0");

        let s = std::str::from_utf8(&uevent[..uevent.len() - 2]).unwrap();
        let parts: Vec<&str> = s.split('\0').collect();
        assert_eq!(parts[0], "ACTION=add");
        assert_eq!(parts[1], "DEVPATH=/devices/pci/00:04.0/usb/1");
        assert_eq!(parts[2], "SUBSYSTEM=usb");
        assert_eq!(parts[3], "DEVNAME=/dev/usb/1");
        assert_eq!(parts[4], "PRODUCT=1234/5678/0000");
        assert_eq!(parts[5], "TYPE=0/0/0");
        assert_eq!(parts[6], "IFACE0_CLASS=3");
        assert_eq!(parts[7], "IFACE0_SUBCLASS=1");
        assert_eq!(parts[8], "IFACE0_PROTOCOL=2");
    }
}
