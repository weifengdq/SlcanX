use socketcan::{CanAnyFrame, CanFdFrame, CanFdSocket, EmbeddedFrame, Socket};
use std::sync::Arc;
use tokio::io::unix::AsyncFd;
use std::io;

// Helper to open async socket
fn open_async_fd_socket(ifname: &str) -> io::Result<AsyncFd<CanFdSocket>> {
    let sock = CanFdSocket::open(ifname)?;
    sock.set_nonblocking(true)?;
    AsyncFd::new(sock)
}

async fn read_frame(socket: &AsyncFd<CanFdSocket>) -> io::Result<CanAnyFrame> {
    loop {
        let mut guard = socket.readable().await?;
        match guard.try_io(|inner| inner.get_ref().read_frame()) {
            Ok(result) => return result,
            Err(_would_block) => continue,
        }
    }
}

async fn write_frame(socket: &AsyncFd<CanFdSocket>, frame: &CanFdFrame) -> io::Result<()> {
    loop {
        let mut guard = socket.writable().await?;
        match guard.try_io(|inner| inner.get_ref().write_frame(frame)) {
            Ok(result) => return result,
            Err(_would_block) => continue,
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let rx_ifaces = ["can0", "can1", "can2"];
    let tx_iface = "can3";

    println!("cangw_rs_tokio_1: forwarding can0, can1, can2 -> can3 (CAN FD BRS)");

    // Open TX socket
    let tx_socket = Arc::new(open_async_fd_socket(tx_iface)?);

    let mut handles = Vec::new();

    for iface in rx_ifaces {
        let rx_socket = open_async_fd_socket(iface)?;
        let tx_socket = tx_socket.clone();
        let iface_name = iface.to_string();

        let handle = tokio::spawn(async move {
            loop {
                match read_frame(&rx_socket).await {
                    Ok(frame) => {
                        let out_frame = match frame {
                            CanAnyFrame::Normal(f) => {
                                // Convert to FD and set BRS
                                if let Some(mut fd_frame) = CanFdFrame::new(f.id(), f.data()) {
                                    fd_frame.set_brs(true);
                                    Some(fd_frame)
                                } else {
                                    None
                                }
                            }
                            CanAnyFrame::Fd(mut f) => {
                                // Set BRS
                                f.set_brs(true);
                                Some(f)
                            }
                            _ => None, // Ignore Remote and Error frames
                        };

                        if let Some(out_frame) = out_frame {
                            if let Err(e) = write_frame(&tx_socket, &out_frame).await {
                                eprintln!("Error writing to {}: {}", tx_iface, e);
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("Error reading from {}: {}", iface_name, e);
                        break;
                    }
                }
            }
        });
        handles.push(handle);
    }

    // Wait for all tasks (they shouldn't finish unless error)
    for handle in handles {
        let _ = handle.await;
    }

    Ok(())
}
