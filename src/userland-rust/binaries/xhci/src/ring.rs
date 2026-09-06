use honeyos_dma::DmaBuffer;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct Trb {
    pub parameter: u64,
    pub status: u32,
    pub control: u32,
}

pub const TRB_NORMAL: u32 = 1;
pub const TRB_SETUP_STAGE: u32 = 2;
pub const TRB_DATA_STAGE: u32 = 3;
pub const TRB_STATUS_STAGE: u32 = 4;
pub const TRB_LINK: u32 = 6;
pub const TRB_ENABLE_SLOT: u32 = 9;
pub const TRB_DISABLE_SLOT: u32 = 10;
pub const TRB_ADDRESS_DEVICE: u32 = 11;
pub const TRB_CONFIGURE_ENDPOINT: u32 = 12;
pub const TRB_EVALUATE_CONTEXT: u32 = 13;
pub const TRB_RESET_ENDPOINT: u32 = 14;
pub const TRB_STOP_ENDPOINT: u32 = 15;
pub const TRB_TRANSFER_EVENT: u32 = 32;
pub const TRB_COMMAND_COMPLETION_EVENT: u32 = 33;
pub const TRB_PORT_STATUS_CHANGE_EVENT: u32 = 34;

pub const TRB_CYCLE: u32 = 1 << 0;
pub const TRB_TC: u32 = 1 << 1;
pub const TRB_ISP: u32 = 1 << 2;
pub const TRB_CH: u32 = 1 << 4;
pub const TRB_IOC: u32 = 1 << 5;
pub const TRB_IDT: u32 = 1 << 6;

impl Trb {
    #[inline]
    pub fn trb_type(&self) -> u32 {
        (self.control >> 10) & 0x3F
    }

    #[inline]
    pub fn cycle_bit(&self) -> bool {
        (self.control & TRB_CYCLE) != 0
    }

    #[inline]
    pub fn completion_code(&self) -> u8 {
        ((self.status >> 24) & 0xFF) as u8
    }

    #[inline]
    pub fn slot_id(&self) -> u8 {
        ((self.control >> 24) & 0xFF) as u8
    }

    #[inline]
    pub fn endpoint_id(&self) -> u8 {
        ((self.control >> 16) & 0x1F) as u8
    }
}

pub struct TrbRing {
    pub buffer: DmaBuffer,
    pub size: usize,
    pub enqueue: usize,
    pub cycle: bool,
}

impl TrbRing {
    pub fn new(size: usize) -> Result<Self, String> {
        // 1 page = 4096 bytes = 256 TRBs of 16 bytes each
        let pages = ((size * 16) + 4095) / 4096;
        let buffer = DmaBuffer::allocate(pages)?;

        let mut ring = Self {
            buffer,
            size,
            enqueue: 0,
            cycle: true,
        };

        // Initialize Link TRB at the end
        let link_index = size - 1;
        let link_trb = Trb {
            parameter: ring.buffer.phys_addr() as u64,
            status: 0,
            control: (TRB_LINK << 10) | TRB_TC,
        };
        ring.write_trb(link_index, link_trb);

        Ok(ring)
    }

    #[inline]
    pub fn phys_addr(&self) -> u32 {
        self.buffer.phys_addr()
    }

    #[inline]
    fn trb_ptr(&self, index: usize) -> *mut Trb {
        unsafe { (self.buffer.virt_ptr() as *mut Trb).add(index) }
    }

    #[inline]
    pub fn write_trb(&mut self, index: usize, trb: Trb) {
        unsafe {
            std::ptr::write_volatile(self.trb_ptr(index), trb);
        }
    }

    #[inline]
    pub fn read_trb(&self, index: usize) -> Trb {
        unsafe { std::ptr::read_volatile(self.trb_ptr(index)) }
    }

    pub fn enqueue(&mut self, mut trb: Trb) -> u32 {
        // Set cycle bit to current ring cycle
        if self.cycle {
            trb.control |= TRB_CYCLE;
        } else {
            trb.control &= !TRB_CYCLE;
        }

        let curr_index = self.enqueue;
        self.write_trb(curr_index, trb);
        let trb_phys = self.buffer.phys_addr() + (curr_index * 16) as u32;

        self.enqueue += 1;
        if self.enqueue == self.size - 1 {
            // Update Link TRB cycle bit and toggle ring cycle
            let mut link_trb = self.read_trb(self.size - 1);
            if self.cycle {
                link_trb.control |= TRB_CYCLE;
            } else {
                link_trb.control &= !TRB_CYCLE;
            }
            self.write_trb(self.size - 1, link_trb);

            self.cycle = !self.cycle;
            self.enqueue = 0;
        }

        trb_phys
    }
}

pub struct EventRing {
    pub buffer: DmaBuffer,
    pub size: usize,
    pub dequeue: usize,
    pub cycle: bool,
}

impl EventRing {
    pub fn new(size: usize) -> Result<Self, String> {
        let pages = ((size * 16) + 4095) / 4096;
        let buffer = DmaBuffer::allocate(pages)?;

        Ok(Self {
            buffer,
            size,
            dequeue: 0,
            cycle: true,
        })
    }

    #[inline]
    pub fn phys_addr(&self) -> u32 {
        self.buffer.phys_addr()
    }

    #[inline]
    fn trb_ptr(&self, index: usize) -> *const Trb {
        unsafe { (self.buffer.virt_ptr() as *const Trb).add(index) }
    }

    #[inline]
    pub fn current_dequeue_phys(&self) -> u32 {
        self.buffer.phys_addr() + (self.dequeue * 16) as u32
    }

    pub fn fetch_event(&mut self) -> Option<Trb> {
        let trb = unsafe { std::ptr::read_volatile(self.trb_ptr(self.dequeue)) };
        let trb_cycle = (trb.control & TRB_CYCLE) != 0;

        if trb_cycle != self.cycle {
            return None;
        }

        self.dequeue += 1;
        if self.dequeue == self.size {
            self.dequeue = 0;
            self.cycle = !self.cycle;
        }

        Some(trb)
    }
}
