use anyhow::{Context, Result};
use futures::future::join_all;
use socketcan::{CanFdFrame, CanFdSocket, Socket, EmbeddedFrame, CanAnyFrame};
use std::process::Command;
use tokio::io::unix::AsyncFd;

#[tokio::main]
async fn main() -> Result<()> {
    // Cleanup existing rules and background jobs
    let _ = Command::new("sudo")
        .args(["cangw", "-F"])
        .output();
    let _ = Command::new("pkill")
        .args(["-f", "candump -L can0 can1 can2"])
        .output();

    println!("cangw_rs_tokio: Forwarding can0, can1, can2 -> can3 (CAN FD BRS)");

    let src_interfaces = vec!["can0", "can1", "can2"];
    let dst_interface = "can3";

    let mut tasks = vec![];

    for src_if in src_interfaces {
        let src_if = src_if.to_string();
        let dst_if = dst_interface.to_string();

        tasks.push(tokio::spawn(async move {
            // Open RX socket
            let rx_socket = CanFdSocket::open(&src_if)
                .with_context(|| format!("Failed to open {}", src_if))?;
            rx_socket.set_nonblocking(true)?;
            let rx_async = AsyncFd::new(rx_socket)?;

            // Open TX socket
            let tx_socket = CanFdSocket::open(&dst_if)
                .with_context(|| format!("Failed to open {}", dst_if))?;
            tx_socket.set_nonblocking(true)?;
            let tx_async = AsyncFd::new(tx_socket)?;

            loop {
                // Wait for readability
                let mut guard = rx_async.readable().await?;

                // Try to read frame
                let frame_res = guard.try_io(|inner| {
                    inner.get_ref().read_frame()
                });

                match frame_res {
                    Ok(Ok(frame)) => {
                        // Transform frame
                        let out_frame = match frame {
                            CanAnyFrame::Normal(classic_frame) => {
                                let id = classic_frame.id();
                                let data = classic_frame.data();
                                let mut fd = CanFdFrame::new(id, data).unwrap();
                                fd.set_brs(true);
                                fd
                            },
                            CanAnyFrame::Fd(fd_frame) => {
                                let mut fd = fd_frame;
                                fd.set_brs(true);
                                fd
                            },
                            CanAnyFrame::Remote(rtr) => {
                                let id = rtr.id();
                                let mut fd = CanFdFrame::new(id, &[]).unwrap();
                                fd.set_brs(true);
                                fd
                            },
                            CanAnyFrame::Error(_) => continue,
                        };

                        // Send frame (wait for writability)
                        loop {
                            let mut write_guard = tx_async.writable().await?;
                            let write_res = write_guard.try_io(|inner| {
                                inner.get_ref().write_frame(&out_frame)
                            });

                            match write_res {
                                Ok(Ok(_)) => break,
                                Ok(Err(e)) => {
                                    eprintln!("Write error on {}: {}", dst_if, e);
                                    break;
                                }
                                Err(_would_block) => continue,
                            }
                        }
                    },
                    Ok(Err(e)) => {
                        eprintln!("Read error on {}: {}", src_if, e);
                    },
                    Err(_would_block) => continue,
                }
            }
            #[allow(unreachable_code)]
            Ok::<(), anyhow::Error>(())
        }));
    }

    join_all(tasks).await;
    Ok(())
}
