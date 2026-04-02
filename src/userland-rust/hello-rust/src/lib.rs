#![no_std]
#![no_main]

use core::panic::PanicInfo;

extern "C" {
    fn printf(format: *const u8, ...) -> i32;
    fn exit(status: i32) -> !;
}

#[no_mangle]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> isize {
    unsafe {
        printf(b"Hello from Rust on honey-os-2!
\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { exit(1) }
}
