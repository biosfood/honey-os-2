use std::ptr::null_mut;

const PROT_READ: i32 = 1;
const PROT_WRITE: i32 = 2;
const MAP_SHARED: i32 = 1;
const MAP_ANONYMOUS: i32 = 0x20;

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
    fn open(path: *const u8, flags: i32, ...) -> i32;
    fn pread(fd: i32, buf: *mut u8, count: usize, offset: i64) -> isize;
    fn close(fd: i32) -> i32;
}

pub struct DmaBuffer {
    virt: *mut u8,
    phys: u32,
    size: usize,
}

// DmaBuffer points to uniquely owned memory allocated via mmap
unsafe impl Send for DmaBuffer {}
unsafe impl Sync for DmaBuffer {}

impl DmaBuffer {
    pub fn allocate(pages: usize) -> Result<Self, String> {
        let size = pages * 4096;
        let virt = unsafe {
            mmap(
                null_mut(),
                size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS,
                -1,
                0i64,
            )
        };

        if virt == (-1isize as *mut u8) || virt.is_null() {
            return Err("mmap failed for DMA buffer".to_string());
        }

        // Zero the memory
        unsafe {
            std::ptr::write_bytes(virt, 0, size);
        }

        // Query physical address of first page via /proc/self/pagemap using pread
        let virt_addr = virt as usize;
        let page_index = virt_addr / 4096;
        let pagemap_offset = (page_index * 8) as i64;

        let path = b"/proc/self/pagemap\0";
        let fd = unsafe { open(path.as_ptr(), 0) }; // O_RDONLY = 0
        if fd < 0 {
            unsafe { munmap(virt, size) };
            return Err(format!("Failed to open /proc/self/pagemap: fd={}", fd));
        }

        let mut entry: u64 = 0;
        let bytes_read = unsafe {
            pread(
                fd,
                &mut entry as *mut u64 as *mut u8,
                core::mem::size_of::<u64>(),
                pagemap_offset,
            )
        };
        unsafe { close(fd) };

        if bytes_read != 8 {
            unsafe { munmap(virt, size) };
            return Err(format!(
                "Failed to read pagemap at offset {}: bytes_read={}",
                pagemap_offset, bytes_read
            ));
        }

        let present = (entry >> 63) & 1;
        if present == 0 {
            unsafe { munmap(virt, size) };
            return Err(format!(
                "DMA page not present in pagemap (entry=0x{:016x}, virt={:p}, offset={})",
                entry, virt, pagemap_offset
            ));
        }

        let pfn = (entry & 0x7F_FFFF_FFFF_FFFF) as u32;
        let phys = pfn * 4096;

        println!(
            "[xhci-dma] Allocated DMA buffer: virt={:p}, phys=0x{:08x}, size=0x{:x}",
            virt, phys, size
        );

        Ok(DmaBuffer { virt, phys, size })
    }

    #[inline]
    pub fn virt_ptr(&self) -> *mut u8 {
        self.virt
    }

    #[inline]
    pub fn phys_addr(&self) -> u32 {
        self.phys
    }

    #[inline]
    pub fn size(&self) -> usize {
        self.size
    }

    #[inline]
    pub fn as_slice(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.virt, self.size) }
    }

    #[inline]
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut(self.virt, self.size) }
    }

    pub fn zero(&mut self) {
        unsafe {
            std::ptr::write_bytes(self.virt, 0, self.size);
        }
    }
}

impl Drop for DmaBuffer {
    fn drop(&mut self) {
        unsafe {
            munmap(self.virt, self.size);
        }
    }
}
