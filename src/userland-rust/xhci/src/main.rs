#![allow(dead_code)]

mod context;
mod dma;
mod pci;
mod registers;
mod ring;
mod usb;
mod xhci;

use pci::PciConfig;
use xhci::XhciController;

fn main() {
    println!("[xhci]: Starting xHCI USB Driver (Rust)...");

    let mut pci = match PciConfig::new() {
        Ok(p) => p,
        Err(e) => {
            println!("[xhci]: Error initializing PCI config: {}", e);
            return;
        }
    };

    let pci_dev = match pci.find_xhci() {
        Ok(dev) => dev,
        Err(e) => {
            println!("[xhci]: Error locating xHCI controller: {}", e);
            return;
        }
    };

    println!(
        "[xhci]: Found xHCI controller at PCI {}:{}.{} (Vendor: 0x{:04x}, Device: 0x{:04x}, IRQ: {})",
        pci_dev.bus, pci_dev.device, pci_dev.function, pci_dev.vendor_id, pci_dev.device_id, pci_dev.irq
    );

    pci.enable_bus_mastering(&pci_dev);
    println!("[xhci]: PCI Bus Mastering and Memory Space enabled.");

    let mut controller = match XhciController::new(pci_dev) {
        Ok(c) => Box::new(c),
        Err(e) => {
            println!("[xhci]: Failed to initialize xHCI controller: {}", e);
            return;
        }
    };

    println!("[xhci]: Scanning root hub ports and enumerating devices...");
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
}
