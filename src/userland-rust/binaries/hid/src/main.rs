use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::path::Path;
use std::time::Duration;

use honeyos_hid::{HidEvent, Key, ReportDecoder, ReportDescriptorParser};

extern "C" {
    fn mkdir(path: *const u8, mode: u32) -> i32;
    fn mkfifo(path: *const u8, mode: u32) -> i32;
    #[allow(dead_code)]
    fn unlink(path: *const u8) -> i32;
    fn exit(status: i32) -> !;
}

pub fn extract_slot_dir(arg: &str) -> String {
    let clean = arg.trim_end_matches('/');
    if let Some(pos) = clean.rfind("/usb/") {
        let suffix = &clean[pos + "/usb/".len()..];
        format!("/dev/usb/{}", suffix.trim_matches('/'))
    } else if clean.chars().all(|c| c.is_ascii_digit()) && !clean.is_empty() {
        format!("/dev/usb/{}", clean)
    } else {
        clean.to_string()
    }
}

pub fn scan_for_usb_slot() -> String {
    if let Ok(entries) = std::fs::read_dir("/dev/usb") {
        let mut subdirs = Vec::new();
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                if path.join("report_desc").exists() {
                    return path.to_string_lossy().into_owned();
                }
                subdirs.push(path);
            }
        }
        for candidate in ["/dev/usb/2", "/dev/usb/3"] {
            if Path::new(candidate).exists() {
                return candidate.to_string();
            }
        }
        if let Some(first) = subdirs.first() {
            return first.to_string_lossy().into_owned();
        }
    }

    if Path::new("/dev/usb/2").exists() {
        "/dev/usb/2".to_string()
    } else if Path::new("/dev/usb/3").exists() {
        "/dev/usb/3".to_string()
    } else {
        "/dev/usb/2".to_string()
    }
}

pub fn resolve_slot_dir() -> String {
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        extract_slot_dir(&args[1])
    } else {
        scan_for_usb_slot()
    }
}

fn write_fifo(fifo: &mut Option<File>, path: &str, line: &str) {
    if fifo.is_none() {
        *fifo = OpenOptions::new().write(true).open(path).ok();
    }
    if let Some(ref mut f) = fifo {
        if writeln!(f, "{}", line).is_err() || f.flush().is_err() {
            *fifo = None;
        }
    }
}

fn write_tty(tty: &mut Option<File>, bytes: &[u8]) {
    if tty.is_none() {
        *tty = OpenOptions::new().write(true).open("/dev/tty1/in").ok();
    }
    if let Some(ref mut f) = tty {
        if f.write_all(bytes).is_err() || f.flush().is_err() {
            *tty = None;
        }
    }
}

fn main() {
    let slot_dir = resolve_slot_dir();
    eprintln!("Starting driver for slot: {}", slot_dir);

    // Ensure /dev/hid exists
    unsafe {
        mkdir(b"/dev/hid\0".as_ptr(), 0);
    }

    // Create standard FIFOs
    unsafe {
        mkfifo(b"/dev/hid/events\0".as_ptr(), 0);
        mkfifo(b"/dev/hid/mouse\0".as_ptr(), 0);
        mkfifo(b"/dev/hid/keyboard\0".as_ptr(), 0);
    }

    // Read report descriptor - strictly required, no fallbacks
    let desc_path = format!("{}/report_desc", slot_dir);
    let mut bytes = Vec::new();
    for _ in 0..30 {
        if let Ok(b) = std::fs::read(&desc_path) {
            if !b.is_empty() {
                bytes = b;
                break;
            }
        }
        std::thread::sleep(Duration::from_millis(50));
    }

    if bytes.is_empty() {
        eprintln!("Failed to read report descriptor at {}", desc_path);
        unsafe {
            exit(1);
        }
    }

    let descriptor = ReportDescriptorParser::parse(&bytes);
    let mut decoder = ReportDecoder::new(descriptor);

    // Open endpoint FIFO
    let ep_path = format!("{}/ep01_in", slot_dir);
    let mut ep_in = {
        let mut file_opt = None;
        for _ in 0..50 {
            if let Ok(f) = File::open(&ep_path) {
                file_opt = Some(f);
                break;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        match file_opt {
            Some(f) => f,
            None => {
                eprintln!("Failed to open endpoint FIFO at {}", ep_path);
                unsafe {
                    exit(1);
                }
            }
        }
    };

    // Open /dev/tty1/in and FIFOs
    let mut tty1_in = OpenOptions::new().write(true).open("/dev/tty1/in").ok();
    let mut events_fifo = OpenOptions::new().write(true).open("/dev/hid/events").ok();
    let mut mouse_fifo = OpenOptions::new().write(true).open("/dev/hid/mouse").ok();
    let mut keyboard_fifo = OpenOptions::new().write(true).open("/dev/hid/keyboard").ok();

    // Signal readiness
    println!("ready");

    // Event streaming loop
    let mut buf = [0u8; 64];
    loop {
        let n = match ep_in.read(&mut buf) {
            Ok(0) => {
                eprintln!("Device disconnected (EOF) at {}", slot_dir);
                break;
            }
            Ok(n) => n,
            Err(e) => {
                if e.kind() == std::io::ErrorKind::BrokenPipe
                    || e.kind() == std::io::ErrorKind::UnexpectedEof
                {
                    eprintln!("Pipe closed: {}", e);
                    break;
                }
                std::thread::sleep(Duration::from_millis(50));
                continue;
            }
        };

        let events = decoder.decode_packet(&buf[..n]);
        for event in &events {
            let line = event.to_line();
            write_fifo(&mut events_fifo, "/dev/hid/events", &line);

            match event {
                HidEvent::MouseMove { .. }
                | HidEvent::MouseButton { .. }
                | HidEvent::MouseWheel { .. } => {
                    write_fifo(&mut mouse_fifo, "/dev/hid/mouse", &line);
                }
                HidEvent::KeyDown { key, character, .. } => {
                    write_fifo(&mut keyboard_fifo, "/dev/hid/keyboard", &line);
                    if *key == Key::Return || *key == Key::KeypadEnter {
                        write_tty(&mut tty1_in, b"\n");
                    } else if *key == Key::Backspace {
                        write_tty(&mut tty1_in, b"\x08");
                    } else if let Some(c) = character {
                        let mut c_buf = [0u8; 4];
                        let s = c.encode_utf8(&mut c_buf);
                        write_tty(&mut tty1_in, s.as_bytes());
                    }
                }
                HidEvent::KeyUp { .. } => {
                    write_fifo(&mut keyboard_fifo, "/dev/hid/keyboard", &line);
                }
            }
        }
    }

    unsafe {
        exit(0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_slot_dir() {
        assert_eq!(
            extract_slot_dir("/devices/pci/00:04.0/usb/2"),
            "/dev/usb/2"
        );
        assert_eq!(extract_slot_dir("/dev/usb/2"), "/dev/usb/2");
        assert_eq!(extract_slot_dir("/dev/usb/2/"), "/dev/usb/2");
        assert_eq!(
            extract_slot_dir("/devices/pci/00:04.0/usb/3"),
            "/dev/usb/3"
        );
        assert_eq!(extract_slot_dir("2"), "/dev/usb/2");
        assert_eq!(extract_slot_dir("/custom/device/path"), "/custom/device/path");
    }

    #[test]
    fn test_keyboard_events_decoding_and_dispatch() {
        let descriptor = ReportDescriptorParser::parse(KEYBOARD_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(descriptor);

        // Packet with 'a' (keycode 0x04) pressed
        let packet = [0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00];
        let events = decoder.decode_packet(&packet);
        assert_eq!(events.len(), 1);
        match &events[0] {
            HidEvent::KeyDown {
                keycode,
                key,
                character,
            } => {
                assert_eq!(*keycode, 4);
                assert_eq!(*key, Key::A);
                assert_eq!(*character, Some('a'));
            }
            _ => panic!("Expected KeyDown"),
        }
    }

    #[test]
    fn test_mouse_events_decoding() {
        let descriptor = ReportDescriptorParser::parse(MOUSE_REPORT_DESCRIPTOR);
        let mut decoder = ReportDecoder::new(descriptor);

        let packet = [0x01, 10, (-5i8) as u8, 0];
        let events = decoder.decode_packet(&packet);
        assert_eq!(events.len(), 2);
        assert!(events.contains(&HidEvent::MouseMove { dx: 10, dy: -5 }));
        assert!(events.contains(&HidEvent::MouseButton {
            button: 1,
            pressed: true
        }));
    }
}
