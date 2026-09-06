use std::fs::{File, OpenOptions};
use std::io::{self, BufRead, BufReader, Write};
use std::path::Path;

use crate::keycodes::HidEvent;

/// Default path for the system HID events FIFO.
pub const DEFAULT_EVENTS_PATH: &str = "/dev/hid/events";

/// Client for connecting to and streaming events from the HID subsystem.
pub struct HidClient<R = BufReader<File>> {
    reader: R,
}

impl HidClient<BufReader<File>> {
    /// Connect to the default HID event stream at `/dev/hid/events`.
    pub fn connect() -> io::Result<Self> {
        Self::connect_path(DEFAULT_EVENTS_PATH)
    }

    /// Connect to a specific FIFO or file path.
    pub fn connect_path<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let file = File::open(path)?;
        Ok(Self::new(BufReader::new(file)))
    }
}

impl<R: BufRead> HidClient<R> {
    /// Create a new `HidClient` from an existing buffered reader.
    pub fn new(reader: R) -> Self {
        Self { reader }
    }

    /// Read the next event from the stream.
    ///
    /// Returns:
    /// - `Ok(Some(event))` when an event is read.
    /// - `Ok(None)` on EOF.
    /// - `Err(e)` on I/O error or malformed event.
    pub fn next_event(&mut self) -> io::Result<Option<HidEvent>> {
        let mut line = String::new();
        loop {
            line.clear();
            let n = self.reader.read_line(&mut line)?;
            if n == 0 {
                return Ok(None);
            }
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let event = HidEvent::from_line(trimmed)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
            return Ok(Some(event));
        }
    }
}

/// Helper function to write a formatted `HidEvent` into any writer (e.g. POSIX FIFO)
/// followed by newline and flush.
pub fn write_event<W: Write>(writer: &mut W, event: &HidEvent) -> io::Result<()> {
    writeln!(writer, "{}", event.to_line())?;
    writer.flush()
}

/// Helper function to open a FIFO/file and write an event to it.
pub fn emit_event_to_path<P: AsRef<Path>>(path: P, event: &HidEvent) -> io::Result<()> {
    let mut file = OpenOptions::new().write(true).open(path)?;
    write_event(&mut file, event)
}
