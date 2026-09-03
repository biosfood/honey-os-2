use std::fs::File;
use std::io::{Read, Write};
use std::thread;

fn main() {
    let mut in_fd_raw = File::open("/dev/serial/in").unwrap();
    let mut in_fd_processed = File::create("/dev/tty1/in").unwrap();

    let mut out_fd_raw = File::create("/dev/serial/out").unwrap();
    let mut out_fd_processed = File::open("/dev/tty1/out").unwrap();

    // Notify the init system that we are ready
    File::create("/dev/tty1/in")
        .unwrap()
        .write_all(b"\n")
        .unwrap();

    // Forward processed output from programs to the raw serial device
    thread::spawn(move || {
        let mut buffer = [0u8; 256];
        loop {
            let size = out_fd_processed.read(&mut buffer).unwrap_or(0);
            if size > 0 {
                let _ = out_fd_raw.write_all(&buffer[..size]);
            }
        }
    });

    // In the main thread, handle terminal line discipline
    let mut out_fd_raw_echo = File::create("/dev/serial/out").unwrap();
    let mut buffer = [0u8; 1024];
    let mut buffer_len = 0;

    loop {
        let mut read_byte = [0u8; 1];
        let size = in_fd_raw.read(&mut read_byte).unwrap_or(0);
        if size == 0 {
            continue;
        }

        let byte = read_byte[0];
        if byte == b'\n' {
            continue;
        }

        if byte == 0x0D {
            // Carriage return: echo \r\n, terminate line, and flush buffer to tty1/in
            let _ = out_fd_raw_echo.write_all(b"\r\n");

            if buffer_len < buffer.len() {
                buffer[buffer_len] = b'\n';
                buffer_len += 1;
            }
            let _ = in_fd_processed.write_all(&buffer[..buffer_len]);
            buffer_len = 0;
        } else if byte == b'\x08' || byte == 0x7F {
            // Backspace: echo \b \b and remove char from buffer if not empty
            let _ = out_fd_raw_echo.write_all(b"\x08 \x08");
            if buffer_len > 0 {
                buffer_len -= 1;
            }
        } else {
            // Normal character: echo and buffer
            let _ = out_fd_raw_echo.write_all(&[byte]);
            if buffer_len < buffer.len() {
                buffer[buffer_len] = byte;
                buffer_len += 1;
            }
        }
    }
}
