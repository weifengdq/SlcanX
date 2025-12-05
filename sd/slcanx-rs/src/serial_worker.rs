use crate::frame::{CanFrame, len_to_dlc, dlc_to_len};
use anyhow::{Result, Context};
use crossbeam_channel::{Receiver, Sender, select};
use log::{debug, error, info, warn};
use std::io::{self, Read, Write};
use std::thread;
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};

pub enum Command {
    SendFrame(u8, CanFrame), // channel, frame
    SendRaw(u8, String),     // channel, raw command string (without \r)
    Shutdown,
}

pub enum Event {
    FrameReceived(u8, CanFrame),
    Error(String),
}

pub struct SerialWorker {
    port_name: String,
    baud_rate: u32,
    cmd_rx: Receiver<Command>,
    event_tx: Sender<Event>,
    group_window: Duration,
}

impl SerialWorker {
    pub fn new(
        port_name: String,
        baud_rate: u32,
        cmd_rx: Receiver<Command>,
        event_tx: Sender<Event>,
        group_window: Duration,
    ) -> Self {
        Self {
            port_name,
            baud_rate,
            cmd_rx,
            event_tx,
            group_window,
        }
    }

    pub fn run(mut self) {
        let mut port = match serialport::new(&self.port_name, self.baud_rate)
            .timeout(Duration::from_millis(10))
            .open()
        {
            Ok(p) => p,
            Err(e) => {
                let _ = self.event_tx.send(Event::Error(format!("Failed to open port: {}", e)));
                return;
            }
        };

        // Enable DTR is important for some CDC implementations
        if let Err(e) = port.write_data_terminal_ready(true) {
             warn!("Failed to set DTR: {}", e);
        }

        let mut read_buf = [0u8; 1024];
        let mut line_buf = Vec::new();
        let mut write_buf = Vec::new();

        loop {
            // 1. Read from Serial
            match port.read(&mut read_buf) {
                Ok(n) if n > 0 => {
                    line_buf.extend_from_slice(&read_buf[..n]);
                    while let Some(pos) = line_buf.iter().position(|&b| b == b'\r') {
                        let line = line_buf.drain(..=pos).collect::<Vec<u8>>();
                        // Remove \r
                        if let Some(line_str) = String::from_utf8(line[..line.len()-1].to_vec()).ok() {
                            self.parse_line(&line_str);
                        }
                    }
                }
                Ok(_) => {} // Timeout
                Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}
                Err(e) => {
                    error!("Serial read error: {}", e);
                    let _ = self.event_tx.send(Event::Error(e.to_string()));
                    break; // Exit on error? Or try to reconnect? For now exit.
                }
            }

            // 2. Check for Commands (Write) with Grouping
            // Try to get the first command
            let start_group = Instant::now();
            let mut got_cmd = false;

            // Non-blocking check first, or short wait if we didn't read anything
            // If we just read data, we might want to process more data, but we also need to send.
            // Let's check the command channel.
            
            loop {
                // If we have data in write_buf, we check if window expired or buffer too large
                if !write_buf.is_empty() {
                    if start_group.elapsed() > self.group_window || write_buf.len() > 1024 {
                        break; // Flush
                    }
                }

                // Try to receive a command
                // If write_buf is empty, we can block for a bit (e.g. 1ms) to save CPU
                // If write_buf is not empty, we use try_recv to fill it up quickly
                let cmd_opt = if write_buf.is_empty() {
                    // Wait up to 1ms for a command
                    select! {
                        recv(self.cmd_rx) -> msg => msg.ok(),
                        default(Duration::from_millis(1)) => None,
                    }
                } else {
                    self.cmd_rx.try_recv().ok()
                };

                match cmd_opt {
                    Some(Command::Shutdown) => return,
                    Some(Command::SendFrame(ch, frame)) => {
                        self.serialize_frame(ch, &frame, &mut write_buf);
                        got_cmd = true;
                    }
                    Some(Command::SendRaw(ch, cmd_str)) => {
                        write_buf.extend_from_slice(format!("{}{}\r", ch, cmd_str).as_bytes());
                        got_cmd = true;
                    }
                    None => {
                        // No more commands currently available
                        if !write_buf.is_empty() {
                            break; // Flush what we have
                        } else {
                            // Nothing to write, go back to read loop
                            break;
                        }
                    }
                }
            }

            if !write_buf.is_empty() {
                if let Err(e) = port.write_all(&write_buf) {
                    error!("Serial write error: {}", e);
                }
                write_buf.clear();
            }
        }
    }

    fn serialize_frame(&self, ch: u8, frame: &CanFrame, buf: &mut Vec<u8>) {
        // [channel]CMDIDDLCDATA\r
        // CMD: t/T (std/ext), r/R (rtr), d/D (fd), b/B (fd+brs)
        let cmd_char = if frame.fd {
            if frame.brs {
                if frame.ext { 'B' } else { 'b' }
            } else {
                if frame.ext { 'D' } else { 'd' }
            }
        } else {
            if frame.rtr {
                if frame.ext { 'R' } else { 'r' }
            } else {
                if frame.ext { 'T' } else { 't' }
            }
        };

        let id_str = if frame.ext {
            format!("{:08X}", frame.id)
        } else {
            format!("{:03X}", frame.id)
        };

        let dlc_code = len_to_dlc(frame.data.len());
        let dlc_str = format!("{:X}", dlc_code);

        let mut data_str = String::new();
        if !frame.rtr {
            for b in &frame.data {
                use std::fmt::Write;
                write!(&mut data_str, "{:02X}", b).unwrap();
            }
        }

        let line = format!("{}{}{}{}{}\r", ch, cmd_char, id_str, dlc_str, data_str);
        buf.extend_from_slice(line.as_bytes());
    }

    fn parse_line(&self, line: &str) {
        if line.is_empty() { return; }
        
        let chars: Vec<char> = line.chars().collect();
        let mut idx = 0;
        
        // Check channel
        let channel = if chars[0] >= '0' && chars[0] <= '3' {
            idx += 1;
            (chars[0] as u8) - b'0'
        } else {
            0
        };

        if idx >= chars.len() { return; }
        let cmd = chars[idx];
        
        match cmd {
            't' | 'T' | 'r' | 'R' | 'd' | 'D' | 'b' | 'B' => {
                if let Ok(frame) = self.parse_frame_content(cmd, &line[idx+1..]) {
                    let _ = self.event_tx.send(Event::FrameReceived(channel, frame));
                }
            }
            'E' => {
                // Error frame, ignore for now or log
                debug!("Error frame on ch{}: {}", channel, line);
            }
            _ => {
                // Other responses
                debug!("Resp on ch{}: {}", channel, line);
            }
        }
    }

    fn parse_frame_content(&self, cmd: char, content: &str) -> Result<CanFrame> {
        let is_ext = matches!(cmd, 'T' | 'R' | 'D' | 'B');
        let is_fd = matches!(cmd, 'd' | 'D' | 'b' | 'B');
        let is_brs = matches!(cmd, 'b' | 'B');
        let is_rtr = matches!(cmd, 'r' | 'R');

        let id_len = if is_ext { 8 } else { 3 };
        if content.len() < id_len + 1 { return Err(anyhow::anyhow!("Line too short")); }

        let id_str = &content[..id_len];
        let id = u32::from_str_radix(id_str, 16)?;

        let dlc_char = content.chars().nth(id_len).unwrap();
        let dlc_val = dlc_char.to_digit(16).ok_or(anyhow::anyhow!("Invalid DLC"))? as u8;
        let len = dlc_to_len(dlc_val);

        let mut data = Vec::new();
        if !is_rtr {
            let data_str = &content[id_len+1..];
            // data_str length should be len * 2
            for i in (0..data_str.len()).step_by(2) {
                if i + 2 <= data_str.len() {
                    let byte_str = &data_str[i..i+2];
                    if let Ok(b) = u8::from_str_radix(byte_str, 16) {
                        data.push(b);
                    }
                }
            }
        }

        Ok(CanFrame {
            id,
            data,
            ext: is_ext,
            rtr: is_rtr,
            fd: is_fd,
            brs: is_brs,
        })
    }
}
