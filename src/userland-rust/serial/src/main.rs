use std::fs::File;
use std::io::Write;
use std::thread;
use std::io::Read;

fn main() {
    let mut out_file = File::open("/dev/serial/out").unwrap();

    let mut rbr = File::create("/dev/port/1016").unwrap();
    let mut ier = File::create("/dev/port/1017").unwrap();
    let mut irr = File::create("/dev/port/1018").unwrap();
    let mut lcr = File::create("/dev/port/1019").unwrap();
    let mut mcr = File::create("/dev/port/1020").unwrap();

    let data: [u8; 1] = [1];
    ier.write(&data).unwrap();
    let data: [u8; 1] = [0x80];
    lcr.write(&data).unwrap();
    let data: [u8; 1] = [3];
    rbr.write(&data).unwrap();
    let data: [u8; 1] = [0];
    ier.write(&data).unwrap();
    let data: [u8; 1] = [3];
    lcr.write(&data).unwrap();
    let data: [u8; 1] = [0xC7];
    irr.write(&data).unwrap();
    let data: [u8; 1] = [0x0B];
    mcr.write(&data).unwrap();

    drop(ier);
    drop(irr);
    drop(lcr);
    drop(mcr);
    // notify the init system we are ready!
    File::create("/dev/tty1/in").unwrap().write("\n".as_bytes()).unwrap();
    thread::spawn(|| {
        let mut data_file = File::open("/dev/port/0x3F8").unwrap();

        let mut write_file = File::create("/dev/serial/in").unwrap();

        let mut irq_file = File::open("/dev/pic/4");
        while irq_file.is_err() {
            irq_file = File::open("/dev/pic/4");
        }
        let mut irq_file = irq_file.unwrap();
        let mut buf: [u8; 1] = [0];
        loop {
            data_file.read(&mut buf).unwrap();
            while buf[0] != 0 {
                write_file.write(&buf).unwrap();
                data_file.read(&mut buf).unwrap();
            }
            irq_file.read(&mut buf).unwrap();
        }
    });
    loop {
        let mut buffer: [u8; 1024] = [0; 1024];
        let count = out_file.read(&mut buffer).unwrap();
        for i in 0..count {
            rbr.write(&[buffer[i]]).unwrap();
        }
    }
}