use anyhow::{Context, Result};
use socketcan::{CanFdFrame, CanFdSocket, Socket, EmbeddedFrame};
use std::process::Command;
use std::thread;

fn main() -> Result<()> {
    // Cleanup existing rules and background jobs
    let _ = Command::new("sudo")
        .args(["cangw", "-F"])
        .output();
    let _ = Command::new("pkill")
        .args(["-f", "candump -L can0 can1 can2"])
        .output();

    println!("cangw_rs_0: Forwarding can0, can1, can2 -> can3 (CAN FD BRS)");

    let src_interfaces = vec!["can0", "can1", "can2"];
    let dst_interface = "can3";

    // We can share the destination socket or create one per thread.
    // Creating one per thread is safer and simpler to avoid locking contention.
    // However, to be efficient, let's try to share if possible, but CanFdSocket might not be Sync.
    // Let's just open a tx socket in each thread.

    let mut handles = vec![];

    for src_if in src_interfaces {
        let src_if = src_if.to_string();
        let dst_if = dst_interface.to_string();

        let handle = thread::spawn(move || -> Result<()> {
            let rx_socket = CanFdSocket::open(&src_if)
                .with_context(|| format!("Failed to open {}", src_if))?;
            
            // Enable FD frames on RX is default for CanFdSocket? 
            // We need to ensure we can receive both Classical and FD frames.
            // socketcan crate's CanFdSocket should handle this.
            
            // We also need a socket to send to can3.
            let tx_socket = CanFdSocket::open(&dst_if)
                .with_context(|| format!("Failed to open {}", dst_if))?;

            loop {
                // Read frame
                // CanFdSocket::read_frame returns a CanAnyFrame (enum of CanFrame or CanFdFrame) in newer versions?
                // Or just read_frame() returns `impl Frame`?
                // In socketcan 3.0, `read_frame` on `CanFdSocket` returns `socketcan::CanAnyFrame`.
                
                match rx_socket.read_frame() {
                    Ok(frame) => {
                        let out_frame = match frame {
                            socketcan::CanAnyFrame::Normal(classic_frame) => {
                                // Convert Classical CAN to CAN FD
                                // ID, Data, DLC -> Len
                                let id = classic_frame.id();
                                let data = classic_frame.data();
                                
                                // Create FD frame with BRS
                                // Note: CanFdFrame::new(id, data) automatically handles length
                                // We need to set BRS.
                                let mut fd_frame = CanFdFrame::new(id, data)
                                    .context("Failed to create FD frame from Classical CAN")?;
                                fd_frame.set_brs(true);
                                fd_frame
                            },
                            socketcan::CanAnyFrame::Fd(fd_frame) => {
                                // Already FD, just ensure BRS is set
                                let mut new_fd_frame = fd_frame;
                                new_fd_frame.set_brs(true);
                                new_fd_frame
                            },
                            socketcan::CanAnyFrame::Remote(rtr_frame) => {
                                // Remote frames are not supported in CAN FD (usually converted to data frames or ignored)
                                // Let's convert to empty data frame with same ID
                                let id = rtr_frame.id();
                                let mut fd_frame = CanFdFrame::new(id, &[])
                                    .context("Failed to create FD frame from RTR")?;
                                fd_frame.set_brs(true);
                                fd_frame
                            },
                            socketcan::CanAnyFrame::Error(_) => {
                                continue;
                            }
                        };

                        // Send
                        if let Err(e) = tx_socket.write_frame(&out_frame) {
                            eprintln!("Failed to send frame to {}: {}", dst_if, e);
                        }
                    }
                    Err(e) => {
                        eprintln!("Error reading from {}: {}", src_if, e);
                        // Don't exit loop on temporary errors, but maybe sleep?
                        // For now just continue.
                    }
                }
            }
        });
        handles.push(handle);
    }

    for handle in handles {
        let _ = handle.join();
    }

    Ok(())
}
