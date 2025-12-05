use clap::Parser;
use slcanx::{Slcanx, CanFrame};
use std::time::Duration;
use anyhow::Result;

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "COM3")]
    port: String,
    #[arg(short, long, default_value_t = 0)]
    channel: u8,
}

fn main() -> Result<()> {
    env_logger::init();
    let args = Args::parse();

    let slcan = Slcanx::new(&args.port, 115200, Duration::from_micros(125))?;
    let channel = slcan.get_channel(args.channel);

    channel.close()?;

    // Arb 500K, Data 2M
    channel.set_bitrate(500000)?;
    channel.set_data_bitrate(2000000)?;
    channel.set_sample_point(Some(80.0), Some(80.0))?;
    
    channel.open()?;

    // Send FD frame
    let data = vec![0xAA; 64];
    let frame = CanFrame::new_fd(0x123, &data, true); // BRS enabled
    println!("Sending FD frame...");
    channel.send(frame)?;

    println!("Listening...");
    loop {
        if let Some((ch, msg)) = slcan.recv_timeout(Duration::from_secs(1))? {
            if ch == args.channel {
                println!("Received: {:?}", msg);
            }
        }
    }
}
