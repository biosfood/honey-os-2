use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::sync::{Arc, Mutex};
use std::thread;

const OFFSET: u8 = 0x20;

extern "C" {
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

struct IrqFifos {
    dec: Vec<File>,
    hex: Vec<File>,
}

struct PicDataPorts {
    master: File,
    slave: File,
}

fn do_unmask(ports: &Mutex<PicDataPorts>, irq: u8) {
    let mut p = ports.lock().unwrap();
    if irq < 8 {
        let mut mask = [0u8; 1];
        let _ = p.master.read_exact(&mut mask);
        mask[0] &= !(1 << irq);
        let _ = p.master.write_all(&mask);
    } else {
        // Ensure cascade (bit 2) on master is unmasked
        let mut mask = [0u8; 1];
        let _ = p.master.read_exact(&mut mask);
        mask[0] &= !(1 << 2);
        let _ = p.master.write_all(&mask);

        // Unmask on slave
        let _ = p.slave.read_exact(&mut mask);
        mask[0] &= !(1 << (irq - 8));
        let _ = p.slave.write_all(&mask);
    }
}

fn do_mask(ports: &Mutex<PicDataPorts>, irq: u8) {
    let mut p = ports.lock().unwrap();
    if irq < 8 {
        let mut mask = [0u8; 1];
        let _ = p.master.read_exact(&mut mask);
        mask[0] |= 1 << irq;
        let _ = p.master.write_all(&mask);
    } else {
        let mut mask = [0u8; 1];
        let _ = p.slave.read_exact(&mut mask);
        mask[0] |= 1 << (irq - 8);
        let _ = p.slave.write_all(&mask);
    }
}

fn handle_unmask_buffer(ports: &Mutex<PicDataPorts>, buf: &[u8]) {
    if buf.is_empty() {
        return;
    }
    // If single raw byte < 16
    if buf.len() == 1 && buf[0] < 16 {
        do_unmask(ports, buf[0]);
        return;
    }
    // Try parsing as text / whitespace separated
    if let Ok(s) = std::str::from_utf8(buf) {
        for word in s.split_whitespace() {
            let irq = if word.starts_with("0x") || word.starts_with("0X") {
                u8::from_str_radix(&word[2..], 16).ok()
            } else {
                word.parse::<u8>().ok()
            };
            if let Some(i) = irq {
                if i < 16 {
                    do_unmask(ports, i);
                }
            }
        }
    }
}

fn handle_mask_buffer(ports: &Mutex<PicDataPorts>, buf: &[u8]) {
    if buf.is_empty() {
        return;
    }
    if buf.len() == 1 && buf[0] < 16 {
        do_mask(ports, buf[0]);
        return;
    }
    if let Ok(s) = std::str::from_utf8(buf) {
        for word in s.split_whitespace() {
            let irq = if word.starts_with("0x") || word.starts_with("0X") {
                u8::from_str_radix(&word[2..], 16).ok()
            } else {
                word.parse::<u8>().ok()
            };
            if let Some(i) = irq {
                if i < 16 {
                    do_mask(ports, i);
                }
            }
        }
    }
}

fn handler(irq: u8, fifos: Arc<IrqFifos>) {
    let interrupt_path = format!("/dev/interrupt/{}\0", OFFSET + irq);
    let mut interrupt_file =
        File::open(&interrupt_path[..interrupt_path.len() - 1]).unwrap();

    let mut control_master = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0x20")
        .unwrap();

    let mut control_slave = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0xA0")
        .unwrap();

    let mut buf = [0u8; 1];
    loop {
        if interrupt_file.read(&mut buf).unwrap_or(0) == 0 {
            continue;
        }

        // Read ISR from master and slave PIC
        let command = [0x0Bu8];
        control_master.write_all(&command).unwrap();
        let mut master_isr = [0u8; 1];
        control_master.read_exact(&mut master_isr).unwrap();

        control_slave.write_all(&command).unwrap();
        let mut slave_isr = [0u8; 1];
        control_slave.read_exact(&mut slave_isr).unwrap();

        let isr = ((slave_isr[0] as u16) << 8) | (master_isr[0] as u16);
        let mut send_pic2_eoi = false;

        for index in 0..16 {
            if (isr & (1 << index)) == 0 {
                continue;
            }
            if index >= 8 {
                send_pic2_eoi = true;
            }
            let signal = [b'1'];
            let _ = (&fifos.hex[index]).write_all(&signal);
            let _ = (&fifos.dec[index]).write_all(&signal);
        }

        if isr != 0 {
            let eoi = [0x20u8];
            control_master.write_all(&eoi).unwrap();
            if send_pic2_eoi {
                control_slave.write_all(&eoi).unwrap();
            }
        }
    }
}

fn main() {
    let _ = std::fs::create_dir_all("/dev/pic");

    let mut control_master = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0x20")
        .unwrap();
    let mut data_master = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0x21")
        .unwrap();
    let mut control_slave = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0xA0")
        .unwrap();
    let mut data_slave = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/port/0xA1")
        .unwrap();

    // Minimal initial IRQ set: unmask serial (IRQ 4) and cascade (IRQ 2) only
    let d: u8 = !((1 << 4) | (1 << 2));
    data_master.write_all(&[d]).unwrap();

    // All slave IRQs initially masked
    data_slave.write_all(&[0xFF]).unwrap();

    // Set master PIC to read ISR
    control_master.write_all(&[0x0B]).unwrap();

    // Initial EOI
    control_master.write_all(&[0x20]).unwrap();
    control_slave.write_all(&[0x20]).unwrap();

    drop(control_master);
    drop(control_slave);

    let ports = Arc::new(Mutex::new(PicDataPorts {
        master: data_master,
        slave: data_slave,
    }));

    // Create unmask and mask control FIFOs
    unsafe {
        mkfifo(b"/dev/pic/unmask\0".as_ptr(), 0);
        mkfifo(b"/dev/pic/mask\0".as_ptr(), 0);
    }

    // Open unmask and mask readers BEFORE spawning any threads or signaling ready
    let unmask_reader = OpenOptions::new().read(true).open("/dev/pic/unmask").unwrap();
    let mask_reader = OpenOptions::new().read(true).open("/dev/pic/mask").unwrap();

    // Create all 16 decimal and hex FIFOs
    for i in 0..16 {
        let dec_path = format!("/dev/pic/{}\0", i);
        let hex_path = format!("/dev/pic/0x{:x}\0", i);
        unsafe {
            mkfifo(dec_path.as_ptr(), 0);
            mkfifo(hex_path.as_ptr(), 0);
        }
    }

    let mut dec_files = Vec::with_capacity(16);
    let mut hex_files = Vec::with_capacity(16);
    for i in 0..16 {
        let dec_path = format!("/dev/pic/{}", i);
        let hex_path = format!("/dev/pic/0x{:x}", i);
        dec_files.push(OpenOptions::new().write(true).open(&dec_path).unwrap());
        hex_files.push(OpenOptions::new().write(true).open(&hex_path).unwrap());
    }

    let fifos = Arc::new(IrqFifos {
        dec: dec_files,
        hex: hex_files,
    });

    // Spawn 16 IRQ listener threads with 16KB stack
    for i in 0..16 {
        let fifos_clone = Arc::clone(&fifos);
        thread::Builder::new()
            .stack_size(16384)
            .name(format!("irq-{}", i))
            .spawn(move || {
                handler(i as u8, fifos_clone);
            })
            .unwrap();
    }

    // Spawn thread to handle /dev/pic/mask with 32KB stack
    let ports_mask = Arc::clone(&ports);
    thread::Builder::new()
        .stack_size(32768)
        .name("pic-mask".to_string())
        .spawn(move || {
            let mut f = mask_reader;
            let mut buf = [0u8; 16];
            loop {
                match f.read(&mut buf) {
                    Ok(n) if n > 0 => {
                        handle_mask_buffer(&ports_mask, &buf[..n]);
                    }
                    _ => {
                        if let Ok(new_f) = OpenOptions::new().read(true).open("/dev/pic/mask") {
                            f = new_f;
                        }
                    }
                }
            }
        })
        .unwrap();

    // Signal init supervisor that PIC driver is ready
    println!("ready");

    // Main thread processes /dev/pic/unmask on the main thread's full stack
    let mut f = unmask_reader;
    let mut buf = [0u8; 16];
    loop {
        match f.read(&mut buf) {
            Ok(n) if n > 0 => {
                handle_unmask_buffer(&ports, &buf[..n]);
            }
            _ => {
                if let Ok(new_f) = OpenOptions::new().read(true).open("/dev/pic/unmask") {
                    f = new_f;
                }
            }
        }
    }
}
