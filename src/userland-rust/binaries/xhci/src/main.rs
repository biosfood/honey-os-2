#![allow(dead_code)]

mod context;
mod registers;
mod ring;
mod xhci;

use honeyos_pci::PciScanner;
use std::fs::OpenOptions;
use std::io::Read;
use xhci::XhciController;

extern "C" {
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

fn main() {
    println!("Starting xHCI USB Driver (Rust)...");

    let pci_dev = match PciScanner::find_xhci() {
        Ok(dev) => dev,
        Err(e) => {
            println!("Error locating xHCI controller via /dev/pci: {}", e);
            return;
        }
    };

    println!(
        "Found xHCI controller at PCI {}:{}.{} (Vendor: 0x{:04x}, Device: 0x{:04x}, IRQ: {})",
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
    let mut usb_control = OpenOptions::new()
        .read(true)
        .open("/dev/usb/control")
        .expect("Failed to open /dev/usb/control");

    // Signal init supervisor that xhci service is ready
    println!("ready");

    // Enter daemon event loop
    let mut buf = [0u8; 128];
    loop {
        match usb_control.read(&mut buf) {
            Ok(n) if n > 0 => {
                // Future control commands (e.g. rescan, reset, etc.)
            }
            _ => {
                if let Ok(new_f) = OpenOptions::new().read(true).open("/dev/usb/control") {
                    usb_control = new_f;
                }
            }
        }
    }
}
