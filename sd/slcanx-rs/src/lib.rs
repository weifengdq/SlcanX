mod frame;
mod serial_worker;

pub use frame::CanFrame;
use serial_worker::{SerialWorker, Command, Event};

use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use crossbeam_channel::{unbounded, Sender, Receiver};
use anyhow::Result;

pub struct Slcanx {
    cmd_tx: Sender<Command>,
    event_rx: Receiver<Event>,
    worker_thread: Option<thread::JoinHandle<()>>,
}

impl Slcanx {
    /// Open the SLCANX device.
    /// `group_window`: Time window to group frames before sending (e.g. 125us).
    pub fn new(port: &str, baudrate: u32, group_window: Duration) -> Result<Self> {
        let (cmd_tx, cmd_rx) = unbounded();
        let (event_tx, event_rx) = unbounded();

        let worker = SerialWorker::new(
            port.to_string(),
            baudrate,
            cmd_rx,
            event_tx,
            group_window,
        );

        let handle = thread::spawn(move || {
            worker.run();
        });

        Ok(Self {
            cmd_tx,
            event_rx,
            worker_thread: Some(handle),
        })
    }

    pub fn get_channel(&self, channel_id: u8) -> Channel {
        Channel {
            id: channel_id,
            cmd_tx: self.cmd_tx.clone(),
            event_rx: self.event_rx.clone(), // Note: This shares the receiver! 
            // Sharing receiver is problematic for multi-channel if we want to filter.
            // We need a dispatcher if we want separate receivers per channel.
            // For simplicity in this example, we'll expose a method to recv that filters.
        }
    }
    
    // Better approach: The Slcanx struct owns the receiver and dispatches to channels?
    // Or we just let the user use the main Slcanx to recv and check channel ID.
    // Let's implement a simple `recv` on Slcanx that returns (channel, frame).
    
    pub fn recv(&self) -> Result<(u8, CanFrame)> {
        loop {
            match self.event_rx.recv()? {
                Event::FrameReceived(ch, frame) => return Ok((ch, frame)),
                Event::Error(e) => return Err(anyhow::anyhow!("Device error: {}", e)),
            }
        }
    }
    
    pub fn recv_timeout(&self, timeout: Duration) -> Result<Option<(u8, CanFrame)>> {
        match self.event_rx.recv_timeout(timeout) {
            Ok(Event::FrameReceived(ch, frame)) => Ok(Some((ch, frame))),
            Ok(Event::Error(e)) => Err(anyhow::anyhow!("Device error: {}", e)),
            Err(crossbeam_channel::RecvTimeoutError::Timeout) => Ok(None),
            Err(crossbeam_channel::RecvTimeoutError::Disconnected) => Err(anyhow::anyhow!("Disconnected")),
        }
    }

    pub fn shutdown(&self) {
        let _ = self.cmd_tx.send(Command::Shutdown);
    }
}

impl Drop for Slcanx {
    fn drop(&mut self) {
        self.shutdown();
        if let Some(handle) = self.worker_thread.take() {
            let _ = handle.join();
        }
    }
}

#[derive(Clone)]
pub struct Channel {
    id: u8,
    cmd_tx: Sender<Command>,
    event_rx: Receiver<Event>, // Shared receiver, be careful. 
    // Actually, for a proper multi-channel API where each channel has its own recv, 
    // we would need a dispatcher thread or a broadcast channel.
    // Given the complexity, let's keep it simple: Channel is mostly for sending.
    // Receiving should be done via the main Slcanx object or we implement a filter here (which consumes messages for others).
    // To fix this properly: Slcanx should have a `subscribe(channel)` method that returns a dedicated receiver.
}

impl Channel {
    pub fn send(&self, frame: CanFrame) -> Result<()> {
        self.cmd_tx.send(Command::SendFrame(self.id, frame))
            .map_err(|_| anyhow::anyhow!("Failed to send command"))
    }

    pub fn send_cmd(&self, cmd: &str) -> Result<()> {
        self.cmd_tx.send(Command::SendRaw(self.id, cmd.to_string()))
            .map_err(|_| anyhow::anyhow!("Failed to send command"))
    }

    pub fn set_bitrate(&self, bitrate: u32) -> Result<()> {
        // Sx or yNNNNN
        let idx = match bitrate {
            10000 => Some(0),
            20000 => Some(1),
            50000 => Some(2),
            100000 => Some(3),
            125000 => Some(4),
            250000 => Some(5),
            500000 => Some(6),
            800000 => Some(7),
            1000000 => Some(8),
            _ => None,
        };

        if let Some(i) = idx {
            self.send_cmd(&format!("S{}", i))
        } else {
            self.send_cmd(&format!("y{}", bitrate))
        }
    }

    pub fn set_data_bitrate(&self, bitrate: u32) -> Result<()> {
        if bitrate % 1000000 == 0 {
            let idx = bitrate / 1000000;
            if idx >= 1 && idx <= 15 {
                return self.send_cmd(&format!("Y{}", idx));
            }
        }
        Err(anyhow::anyhow!("Invalid data bitrate"))
    }

    pub fn set_sample_point(&self, nominal: Option<f32>, data: Option<f32>) -> Result<()> {
        if let Some(n) = nominal {
            self.send_cmd(&format!("p{}", (n * 10.0) as u32))?;
        }
        if let Some(d) = data {
            self.send_cmd(&format!("P{}", (d * 10.0) as u32))?;
        }
        Ok(())
    }

    pub fn open(&self) -> Result<()> {
        self.send_cmd("O")
    }

    pub fn close(&self) -> Result<()> {
        self.send_cmd("C")
    }
}
