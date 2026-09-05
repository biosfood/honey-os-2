use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::thread;
use std::time::Duration;

/// Unmask an IRQ line by writing to /dev/pic/unmask
pub fn unmask(irq: u8) -> std::io::Result<()> {
    let mut file = OpenOptions::new().write(true).open("/dev/pic/unmask")?;
    // Write both raw byte and newline-terminated ascii string to be flexible
    file.write_all(&[irq])?;
    file.flush()?;
    Ok(())
}

/// Mask an IRQ line by writing to /dev/pic/mask
pub fn mask(irq: u8) -> std::io::Result<()> {
    let mut file = OpenOptions::new().write(true).open("/dev/pic/mask")?;
    file.write_all(&[irq])?;
    file.flush()?;
    Ok(())
}

/// A subscription to a PIC IRQ line backed by a /dev/pic/<irq> FIFO.
pub struct IrqSubscription {
    irq: u8,
    file: File,
}

impl IrqSubscription {
    /// Subscribe to the given IRQ line. Automatically requests the PIC daemon to unmask it.
    pub fn subscribe(irq: u8) -> std::io::Result<Self> {
        let path = format!("/dev/pic/{}", irq);
        // Wait for FIFO to become accessible if needed
        let mut attempts = 0;
        let file = loop {
            match OpenOptions::new().read(true).open(&path) {
                Ok(f) => break f,
                Err(e) => {
                    attempts += 1;
                    if attempts > 50 {
                        return Err(e);
                    }
                    thread::sleep(Duration::from_millis(5));
                }
            }
        };

        // Request unmasking now that the reader descriptor is registered
        let _ = unmask(irq);

        Ok(Self { irq, file })
    }

    /// Block waiting for an interrupt on this IRQ line.
    pub fn wait(&mut self) -> std::io::Result<()> {
        let mut buf = [0u8; 1];
        let n = self.file.read(&mut buf)?;
        if n == 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::UnexpectedEof,
                "IRQ FIFO closed",
            ));
        }
        Ok(())
    }

    pub fn irq(&self) -> u8 {
        self.irq
    }

    pub fn file_mut(&mut self) -> &mut File {
        &mut self.file
    }
}
