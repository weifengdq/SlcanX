use clap::Parser;
use slcanx::{Slcanx, CanFrame};
use std::time::Duration;
use anyhow::Result;

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

    println!("Opening {} Channel {} @ {}bps", args.port, args.channel, args.bitrate);

    // Grouping window 125us
    let slcan = Slcanx::new(&args.port, 115200, Duration::from_micros(125))?;
    let channel = slcan.get_channel(args.channel);

    channel.close()?;
    channel.set_bitrate(args.bitrate)?;
    channel.open()?;

    // Send a frame
    let frame = CanFrame::new_std(0x123, &[0x11, 0x22, 0x33, 0x44]);
    println!("Sending: {:?}", frame);
    channel.send(frame)?;

    println!("Listening... (Ctrl+C to exit)");
    loop {
        if let Some((ch, msg)) = slcan.recv_timeout(Duration::from_secs(1))? {
            if ch == args.channel {
                println!("Received: {:?}", msg);
            }
        }
    }
}
