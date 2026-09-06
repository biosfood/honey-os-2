mod rules;

use std::collections::{BTreeMap, HashMap};
use std::ffi::CString;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::sync::{Arc, Mutex};
use std::thread;

use rules::{split_command, substitute_vars, Rule};

extern "C" {
    fn mkdir(path: *const u8, mode: u32) -> i32;
    fn mkfifo(path: *const u8, mode: u32) -> i32;
    fn fork() -> i32;
    fn execv(path: *const u8, argv: *const *const u8) -> i32;
    fn exit(status: i32) -> !;
}

#[derive(Debug, Clone)]
pub struct DeviceRecord {
    pub devpath: String,
    pub pid: i32,
    pub subsystem: String,
    pub env: HashMap<String, String>,
}

fn matches_devpath_prefix(devpath: &str, prefix: &str) -> bool {
    let clean_prefix = prefix.trim_end_matches('/');
    if devpath == clean_prefix {
        return true;
    }
    let slash_prefix = format!("{}/", clean_prefix);
    devpath.starts_with(&slash_prefix)
}

fn dump_state(registry: &BTreeMap<String, DeviceRecord>) {
    let mut content = String::new();
    for (devpath, record) in registry {
        content.push_str(&format!(
            "DEVPATH={} SUBSYSTEM={} PID={}",
            devpath, record.subsystem, record.pid
        ));
        let mut keys: Vec<&String> = record.env.keys().collect();
        keys.sort();
        for k in keys {
            if k != "DEVPATH" && k != "SUBSYSTEM" && k != "ACTION" && k != "PID" {
                if let Some(v) = record.env.get(k) {
                    content.push(' ');
                    content.push_str(k);
                    content.push('=');
                    content.push_str(v);
                }
            }
        }
        content.push('\n');
    }

    if let Ok(mut f) = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open("/run/devd/state")
    {
        let _ = f.write_all(content.as_bytes());
        let _ = f.flush();
    }
}

pub fn extract_next_event(buf: &mut Vec<u8>) -> Option<HashMap<String, String>> {
    let non_zero_pos = buf.iter().position(|&b| b != 0);
    match non_zero_pos {
        Some(0) => {}
        Some(pos) => {
            buf.drain(..pos);
        }
        None => {
            buf.clear();
            return None;
        }
    }

    let mut end_idx = None;
    for i in 0..buf.len().saturating_sub(1) {
        if buf[i] == 0 && buf[i + 1] == 0 {
            end_idx = Some(i);
            break;
        }
    }

    let end_idx = end_idx?;
    let payload = buf[..end_idx].to_vec();
    buf.drain(..end_idx + 2);

    let mut event = HashMap::new();
    for part in payload.split(|&b| b == 0) {
        if part.is_empty() {
            continue;
        }
        if let Ok(s) = std::str::from_utf8(part) {
            if let Some(eq_idx) = s.find('=') {
                let key = s[..eq_idx].trim().to_string();
                let val = s[eq_idx + 1..].trim().to_string();
                event.insert(key, val);
            }
        }
    }

    Some(event)
}

fn handle_event(
    event: &HashMap<String, String>,
    rules: &[Rule],
    registry: &Arc<Mutex<BTreeMap<String, DeviceRecord>>>,
) {
    let action = match event.get("ACTION") {
        Some(a) => a.as_str(),
        None => return,
    };
    let devpath = match event.get("DEVPATH") {
        Some(d) => d.as_str(),
        None => return,
    };

    println!("Event: ACTION={} DEVPATH={}", action, devpath);
    println!("{:?}", event);

    if action == "add" {
        for rule in rules {
            if rule.matches(event) {
                if let Some(cmd_template) = &rule.run {
                    let cmd = substitute_vars(cmd_template, event);
                    let cmd = cmd.trim();
                    if cmd.is_empty() {
                        // Explicitly configured to run nothing
                        break;
                    }

                    println!("Rule matched: running '{}'", cmd);
                    let args = split_command(cmd);
                    if args.is_empty() {
                        break;
                    }

                    let exec_path = &args[0];
                    let c_path = match CString::new(exec_path.as_str()) {
                        Ok(s) => s,
                        Err(e) => {
                            eprintln!("Failed to create CString for exec path: {}", e);
                            break;
                        }
                    };

                    let mut c_args = Vec::new();
                    let mut valid = true;
                    for a in &args {
                        match CString::new(a.as_str()) {
                            Ok(s) => c_args.push(s),
                            Err(e) => {
                                eprintln!("Failed to create CString for arg: {}", e);
                                valid = false;
                                break;
                            }
                        }
                    }
                    if !valid {
                        break;
                    }

                    let mut c_argv: Vec<*const u8> =
                        c_args.iter().map(|s| s.as_ptr() as *const u8).collect();
                    c_argv.push(std::ptr::null());

                    let pid = unsafe { fork() };
                    if pid == 0 {
                        // Child process: execute driver binary
                        unsafe {
                            execv(c_path.as_ptr() as *const u8, c_argv.as_ptr());
                            exit(1);
                        }
                    } else if pid > 0 {
                        println!("Spawned driver PID {} for {}", pid, devpath);
                        let subsystem = event.get("SUBSYSTEM").cloned().unwrap_or_default();
                        let record = DeviceRecord {
                            devpath: devpath.to_string(),
                            pid,
                            subsystem,
                            env: event.clone(),
                        };

                        {
                            let mut map = registry.lock().unwrap();
                            map.insert(devpath.to_string(), record);
                            dump_state(&map);
                        }

                        // Spawn process reaper thread
                        let registry_clone = Arc::clone(registry);
                        let devpath_clone = devpath.to_string();
                        thread::spawn(move || {
                            let status_file = format!("/proc/{}/status", pid);
                            if let Ok(mut f) = File::open(&status_file) {
                                let mut exit_buf = [0u8; 4];
                                if f.read_exact(&mut exit_buf).is_ok() {
                                    let code = i32::from_le_bytes(exit_buf);
                                    println!(
                                        "Driver PID {} ({}) exited with code {}",
                                        pid, devpath_clone, code
                                    );
                                }
                            }

                            let mut map = registry_clone.lock().unwrap();
                            if let Some(r) = map.get(&devpath_clone) {
                                if r.pid == pid {
                                    map.remove(&devpath_clone);
                                    dump_state(&map);
                                }
                            }
                        });
                    } else {
                        eprintln!("fork() failed for {}", devpath);
                    }

                    break;
                }
            }
        }
    } else if action == "remove" {
        println!("Cascading teardown for prefix {}", devpath);
        let mut to_remove = Vec::new();
        {
            let map = registry.lock().unwrap();
            for (path, record) in map.iter() {
                if matches_devpath_prefix(path, devpath) {
                    to_remove.push((path.clone(), record.pid));
                }
            }
        }

        let mut map = registry.lock().unwrap();
        let mut changed = false;
        for (path, pid) in to_remove {
            println!("Tearing down device {} (PID {})", path, pid);
            // Notify process to terminate
            let status_file = format!("/proc/{}/status", pid);
            if let Ok(mut f) = OpenOptions::new().write(true).open(&status_file) {
                let code: i32 = -1;
                let _ = f.write_all(&code.to_le_bytes());
            }

            map.remove(&path);
            changed = true;
        }

        if changed {
            dump_state(&map);
        }
    }

    let _ = std::io::stdout().flush();
}

fn main() {
    // 1. Ensure required directories and FIFO exist
    unsafe {
        mkdir(b"/dev\0".as_ptr(), 0);
        mkfifo(b"/dev/uevent\0".as_ptr(), 0);
        mkdir(b"/run\0".as_ptr(), 0);
        mkdir(b"/run/devd\0".as_ptr(), 0);
    }

    // 2. Ingest rules and initialize device registry
    let rules = rules::load_rules();
    let registry: Arc<Mutex<BTreeMap<String, DeviceRecord>>> =
        Arc::new(Mutex::new(BTreeMap::new()));

    // 3. Atomically dump initial empty state
    {
        let map = registry.lock().unwrap();
        dump_state(&map);
    }

    // 4. Open /dev/uevent FIFO before signaling ready
    let mut uevent_file = match File::open("/dev/uevent") {
        Ok(f) => f,
        Err(e) => {
            eprintln!("Failed to open /dev/uevent: {}", e);
            return;
        }
    };

    // 5. Handshake: Print "ready" and flush
    println!("ready");
    std::io::stdout().flush().unwrap();

    // 6. Event loop
    let mut stream_buf = Vec::new();
    let mut read_buf = [0u8; 4096];

    loop {
        let n = uevent_file.read(&mut read_buf).unwrap_or(0);
        if n > 0 {
            stream_buf.extend_from_slice(&read_buf[..n]);
            while let Some(event) = extract_next_event(&mut stream_buf) {
                if !event.is_empty() {
                    handle_event(&event, &rules, &registry);
                }
            }
        } else {
            thread::yield_now();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_next_event() {
        let mut buf = b"ACTION=add\0DEVPATH=/devices/pci/0:14.0\0SUBSYSTEM=pci\0\0".to_vec();
        let ev = extract_next_event(&mut buf).unwrap();
        assert_eq!(ev.get("ACTION").unwrap(), "add");
        assert_eq!(ev.get("DEVPATH").unwrap(), "/devices/pci/0:14.0");
        assert_eq!(ev.get("SUBSYSTEM").unwrap(), "pci");
        assert!(buf.is_empty());
    }

    #[test]
    fn test_extract_multiple_events() {
        let mut buf = b"ACTION=add\0DEVPATH=foo\0\0ACTION=remove\0DEVPATH=foo\0\0".to_vec();
        let ev1 = extract_next_event(&mut buf).unwrap();
        assert_eq!(ev1.get("ACTION").unwrap(), "add");
        let ev2 = extract_next_event(&mut buf).unwrap();
        assert_eq!(ev2.get("ACTION").unwrap(), "remove");
        assert!(buf.is_empty());
    }

    #[test]
    fn test_matches_devpath_prefix() {
        assert!(matches_devpath_prefix("/devices/pci/0:14.0", "/devices/pci/0:14.0"));
        assert!(matches_devpath_prefix("/devices/pci/0:14.0/usb1", "/devices/pci/0:14.0"));
        assert!(matches_devpath_prefix("/devices/pci/0:14.0/usb1/1-1", "/devices/pci/0:14.0"));
        assert!(!matches_devpath_prefix("/devices/pci/0:14.0_suffix", "/devices/pci/0:14.0"));
        assert!(!matches_devpath_prefix("/devices/pci/0:15.0", "/devices/pci/0:14.0"));
    }
}
