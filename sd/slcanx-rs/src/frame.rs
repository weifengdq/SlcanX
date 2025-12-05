use bitflags::bitflags;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameType {
    Standard, // 11-bit ID
    Extended, // 29-bit ID
}

#[derive(Debug, Clone)]
pub struct CanFrame {
    pub id: u32,
    pub data: Vec<u8>,
    pub ext: bool,
    pub rtr: bool,
    pub fd: bool,
    pub brs: bool,
}

impl CanFrame {
    pub fn new_std(id: u32, data: &[u8]) -> Self {
        Self {
            id,
            data: data.to_vec(),
            ext: false,
            rtr: false,
            fd: false,
            brs: false,
        }
    }

    pub fn new_ext(id: u32, data: &[u8]) -> Self {
        Self {
            id,
            data: data.to_vec(),
            ext: true,
            rtr: false,
            fd: false,
            brs: false,
        }
    }

    pub fn new_fd(id: u32, data: &[u8], brs: bool) -> Self {
        Self {
            id,
            data: data.to_vec(),
            ext: false,
            rtr: false,
            fd: true,
            brs,
        }
    }
}

// Helper to convert DLC to length and vice versa
pub fn dlc_to_len(dlc: u8) -> usize {
    match dlc {
        0..=8 => dlc as usize,
        9 => 12,
        10 => 16,
        11 => 20,
        12 => 24,
        13 => 32,
        14 => 48,
        15 => 64,
        _ => 64,
    }
}

pub fn len_to_dlc(len: usize) -> u8 {
    match len {
        0..=8 => len as u8,
        9..=12 => 9,
        13..=16 => 10,
        17..=20 => 11,
        21..=24 => 12,
        25..=32 => 13,
        33..=48 => 14,
        _ => 15,
    }
}
