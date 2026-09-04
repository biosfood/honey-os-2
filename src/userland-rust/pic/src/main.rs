use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::sync::Arc;
use std::thread;

const OFFSET: u8 = 0x20;

extern "C" {
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

struct IrqFifos {
    dec: Vec<File>,
    hex: Vec<File>,
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

    // Unmask serial (IRQ 4) and PIT (IRQ 0)
    let d: u8 = !((1 << 4) | (1 << 0));
    data_master.write_all(&[d]).unwrap();

    // Mask all slave IRQs
    data_slave.write_all(&[0xFF]).unwrap();

    // Set master PIC to read ISR
    control_master.write_all(&[0x0B]).unwrap();

    // Initial EOI
    control_master.write_all(&[0x20]).unwrap();
    control_slave.write_all(&[0x20]).unwrap();

    drop(control_master);
    drop(data_master);
    drop(control_slave);
    drop(data_slave);

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

    let mut handles = Vec::with_capacity(16);
    for i in 0..16 {
        let fifos_clone = Arc::clone(&fifos);
        let handle = thread::Builder::new()
            .stack_size(2048)
            .name(format!("irq-{}", i))
            .spawn(move || {
                handler(i as u8, fifos_clone);
            })
            .unwrap();
        handles.push(handle);
    }

    // Notify the init system that the PIC driver is ready
    println!("ready");

    for handle in handles {
        let _ = handle.join();
    }
}
