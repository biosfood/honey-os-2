use std::fs::{self, OpenOptions};
use std::io::Write;
use honeyos_pic::IrqSubscription;

const PIT_SCALE: u32 = 1193180;
const CMD_BINARY: u8 = 0x00;
const CMD_MODE3: u8 = 0x06;
const CMD_RW_BOTH: u8 = 0x30;
const CMD_COUNTER0: u8 = 0x00;

extern "C" {
    fn mkfifo(path: *const u8, mode: u32) -> i32;
}

fn main() {
    let _ = fs::create_dir_all("/dev/pic");

    let fifo_path = b"/dev/pic/time_update\0";
    unsafe {
        mkfifo(fifo_path.as_ptr(), 0);
    }

    // Configure PIT frequency to 1000 Hz
    let hz: u32 = 1000;
    let divisor = (PIT_SCALE / hz) as u16;

    let mut fd_control = OpenOptions::new()
        .write(true)
        .open("/dev/port/0x43")
        .expect("Failed to open PIT control port");
    let control_word = CMD_BINARY | CMD_MODE3 | CMD_RW_BOTH | CMD_COUNTER0;
    fd_control.write_all(&[control_word]).unwrap();
    drop(fd_control);

    let mut fd_a = OpenOptions::new()
        .write(true)
        .open("/dev/port/0x40")
        .expect("Failed to open PIT data port");
    fd_a.write_all(&[divisor as u8]).unwrap();
    fd_a.write_all(&[(divisor >> 8) as u8]).unwrap();
    drop(fd_a);

    // Subscribe to IRQ 0 via honeyos-pic (automatically unmasks IRQ 0)
    let mut irq0 = IrqSubscription::subscribe(0).expect("Failed to subscribe to IRQ 0");

    // Signal init supervisor that PIT daemon is ready
    println!("ready");

    let mut system_time: u32 = 0;
    loop {
        if irq0.wait().is_ok() {
            system_time = system_time.wrapping_add(1);
        }
    }
}
