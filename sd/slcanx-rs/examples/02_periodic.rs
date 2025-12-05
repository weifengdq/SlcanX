use clap::Parser;
use slcanx::{Slcanx, CanFrame};
use std::time::{Duration, Instant};
use anyhow::Result;
use std::thread;

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "COM3")]
    port: String,
    #[arg(short, long, default_value_t = 500000)]
    bitrate: u32,
    #[arg(short, long, default_value_t = 0)]
    channel: u8,
}

fn main() -> Result<()> {
    env_logger::init();
    let args = Args::parse();

    let slcan = Slcanx::new(&args.port, 115200, Duration::from_micros(125))?;
    let channel = slcan.get_channel(args.channel);

    channel.close()?;
    channel.set_bitrate(args.bitrate)?;
    channel.open()?;

    let tx_channel = channel.clone();
    
    // Periodic sender thread
    thread::spawn(move || {
        let mut cnt = 0u8;
        loop {
            let frame = CanFrame::new_std(0x100, &[0, 1, 2, cnt]);
            if let Err(e) = tx_channel.send(frame) {
                eprintln!("Send error: {}", e);
                break;
            }
            cnt = cnt.wrapping_add(1);
            thread::sleep(Duration::from_secs(1));
        }
    });

    println!("Listening... (Ctrl+C to exit)");
    loop {
        if let Some((ch, msg)) = slcan.recv_timeout(Duration::from_millis(100))? {
            if ch == args.channel {
                println!("Received: {:?}", msg);
            }
        }
    }
}
