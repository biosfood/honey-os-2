use std::fs::OpenOptions;
use std::ptr::null_mut;

const PROT_READ: i32 = 1;
const PROT_WRITE: i32 = 2;
const MAP_SHARED: i32 = 1;

extern "C" {
    fn mmap(
        addr: *mut u8,
        length: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> *mut u8;
    fn munmap(addr: *mut u8, length: usize) -> i32;
}

pub struct XhciRegisters {
    base: *mut u8,
    size: usize,
    op_base: *mut u8,
    rt_base: *mut u8,
    db_base: *mut u8,
}

unsafe impl Send for XhciRegisters {}
unsafe impl Sync for XhciRegisters {}

impl XhciRegisters {
    pub fn map(phys_base: u32, size: u32) -> Result<Self, String> {
        let path = format!("/kernel/mem/0x{:x}+0x{:x}", phys_base, size);
        println!("[xhci-mmio]: Opening MMIO path: {}", path);
        let mut is_raw = false;
        let file = match OpenOptions::new().read(true).write(true).open(&path) {
            Ok(f) => {
                println!("[xhci-mmio]: Successfully opened {}", path);
                f
            }
            Err(e) => {
                println!("[xhci-mmio]: Could not open {} ({}), trying /kernel/mem...", path, e);
                is_raw = true;
                OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open("/kernel/mem")
                    .map_err(|e2| format!("Failed to open {} ({}) and /kernel/mem ({})", path, e, e2))?
            }
        };

        use std::os::unix::io::AsRawFd;
        let fd = file.as_raw_fd();
        let mmap_offset: i64 = if is_raw { phys_base as i64 } else { 0i64 };
        println!("[xhci-mmio]: Calling mmap (fd: {}, offset: 0x{:x}, size: 0x{:x})...", fd, mmap_offset, size);

        let virt = unsafe {
            mmap(
                null_mut(),
                size as usize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                mmap_offset,
            )
        };

        println!("[xhci-mmio]: mmap returned virt: {:p}", virt);

        if virt == (-1isize as *mut u8) || virt.is_null() {
            return Err(format!("mmap failed for MMIO at 0x{:x}", phys_base));
        }

        let cap_length = unsafe { std::ptr::read_volatile(virt) };
        let dboff = unsafe { std::ptr::read_volatile(virt.add(0x14) as *const u32) } & !0x3;
        let rtsoff = unsafe { std::ptr::read_volatile(virt.add(0x18) as *const u32) } & !0x1F;

        let op_base = unsafe { virt.add(cap_length as usize) };
        let db_base = unsafe { virt.add(dboff as usize) };
        let rt_base = unsafe { virt.add(rtsoff as usize) };

        Ok(Self {
            base: virt,
            size: size as usize,
            op_base,
            rt_base,
            db_base,
        })
    }

    // --- Capability Registers ---

    #[inline]
    pub fn cap_length(&self) -> u8 {
        unsafe { std::ptr::read_volatile(self.base) }
    }

    #[inline]
    pub fn hci_version(&self) -> u16 {
        unsafe { std::ptr::read_volatile(self.base.add(0x02) as *const u16) }
    }

    #[inline]
    pub fn hcsparams1(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.base.add(0x04) as *const u32) }
    }

    #[inline]
    pub fn max_slots(&self) -> u8 {
        (self.hcsparams1() & 0xFF) as u8
    }

    #[inline]
    pub fn max_intrs(&self) -> u16 {
        ((self.hcsparams1() >> 8) & 0x7FF) as u16
    }

    #[inline]
    pub fn max_ports(&self) -> u8 {
        ((self.hcsparams1() >> 24) & 0xFF) as u8
    }

    #[inline]
    pub fn hcsparams2(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.base.add(0x08) as *const u32) }
    }

    #[inline]
    pub fn max_scratchpad_buffers(&self) -> u32 {
        let p2 = self.hcsparams2();
        let hi = (p2 >> 21) & 0x1F;
        let lo = (p2 >> 27) & 0x1F;
        (hi << 5) | lo
    }

    #[inline]
    pub fn hccparams1(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.base.add(0x10) as *const u32) }
    }

    #[inline]
    pub fn xecp(&self) -> u16 {
        ((self.hccparams1() >> 16) & 0xFFFF) as u16
    }

    // --- Operational Registers ---

    #[inline]
    pub fn read_usbcmd(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.op_base as *const u32) }
    }

    #[inline]
    pub fn write_usbcmd(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.op_base as *mut u32, val) }
    }

    #[inline]
    pub fn read_usbsts(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.op_base.add(0x04) as *const u32) }
    }

    #[inline]
    pub fn write_usbsts(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.op_base.add(0x04) as *mut u32, val) }
    }

    #[inline]
    pub fn read_pagesize(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.op_base.add(0x08) as *const u32) }
    }

    #[inline]
    pub fn write_crcr(&self, val: u64) {
        unsafe {
            let p_low = self.op_base.add(0x18) as *mut u32;
            let p_high = self.op_base.add(0x1C) as *mut u32;
            std::ptr::write_volatile(p_low, val as u32);
            std::ptr::write_volatile(p_high, (val >> 32) as u32);
        }
    }

    #[inline]
    pub fn write_dcbaap(&self, val: u64) {
        unsafe {
            let p_low = self.op_base.add(0x30) as *mut u32;
            let p_high = self.op_base.add(0x34) as *mut u32;
            std::ptr::write_volatile(p_low, val as u32);
            std::ptr::write_volatile(p_high, (val >> 32) as u32);
        }
    }

    #[inline]
    pub fn read_config(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.op_base.add(0x38) as *const u32) }
    }

    #[inline]
    pub fn write_config(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.op_base.add(0x38) as *mut u32, val) }
    }

    // PORTSC for 1-based port index
    #[inline]
    pub fn portsc_ptr(&self, port: u8) -> *mut u32 {
        assert!(port >= 1);
        let offset = 0x400 + ((port as usize - 1) * 0x10);
        unsafe { self.op_base.add(offset) as *mut u32 }
    }

    #[inline]
    pub fn read_portsc(&self, port: u8) -> u32 {
        unsafe { std::ptr::read_volatile(self.portsc_ptr(port)) }
    }

    #[inline]
    pub fn write_portsc(&self, port: u8, val: u32) {
        unsafe { std::ptr::write_volatile(self.portsc_ptr(port), val) }
    }

    // --- Runtime Registers (Interrupter 0) ---

    #[inline]
    pub fn interrupter0_ptr(&self) -> *mut u8 {
        unsafe { self.rt_base.add(0x20) }
    }

    #[inline]
    pub fn read_iman(&self) -> u32 {
        unsafe { std::ptr::read_volatile(self.interrupter0_ptr() as *const u32) }
    }

    #[inline]
    pub fn write_iman(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.interrupter0_ptr() as *mut u32, val) }
    }

    #[inline]
    pub fn write_imod(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.interrupter0_ptr().add(0x04) as *mut u32, val) }
    }

    #[inline]
    pub fn write_erstsz(&self, val: u32) {
        unsafe { std::ptr::write_volatile(self.interrupter0_ptr().add(0x08) as *mut u32, val) }
    }

    #[inline]
    pub fn write_erstba(&self, val: u64) {
        unsafe {
            let p_low = self.interrupter0_ptr().add(0x10) as *mut u32;
            let p_high = self.interrupter0_ptr().add(0x14) as *mut u32;
            std::ptr::write_volatile(p_low, val as u32);
            std::ptr::write_volatile(p_high, (val >> 32) as u32);
        }
    }

    #[inline]
    pub fn read_erdp(&self) -> u64 {
        unsafe {
            let p_low = self.interrupter0_ptr().add(0x18) as *const u32;
            let p_high = self.interrupter0_ptr().add(0x1C) as *const u32;
            let low = std::ptr::read_volatile(p_low);
            let high = std::ptr::read_volatile(p_high);
            (low as u64) | ((high as u64) << 32)
        }
    }

    #[inline]
    pub fn write_erdp(&self, val: u64) {
        unsafe {
            let p_low = self.interrupter0_ptr().add(0x18) as *mut u32;
            let p_high = self.interrupter0_ptr().add(0x1C) as *mut u32;
            std::ptr::write_volatile(p_low, val as u32);
            std::ptr::write_volatile(p_high, (val >> 32) as u32);
        }
    }

    // --- Doorbell Registers ---

    #[inline]
    pub fn ring_doorbell(&self, slot: u8, target: u32) {
        let ptr = unsafe { self.db_base.add((slot as usize) * 4) as *mut u32 };
        unsafe { std::ptr::write_volatile(ptr, target) }
    }
}

impl Drop for XhciRegisters {
    fn drop(&mut self) {
        unsafe {
            munmap(self.base, self.size);
        }
    }
}
