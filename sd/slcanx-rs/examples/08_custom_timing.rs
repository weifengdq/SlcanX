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

    // Custom timing
    // Arb: Pre 4, Seg1 31, Seg2 8, SJW 8 -> a80_4_31_8_8_0
    // Data: Pre 2, Seg1 15, Seg2 4, SJW 4, TDC on -> A80_2_15_4_4_1
    channel.send_cmd("a80_4_31_8_8_0")?;
    channel.send_cmd("A80_2_15_4_4_1")?;
    
    channel.open()?;

    let frame = CanFrame::new_fd(0x600, &[0xCC; 8], true);
    println!("Sending custom timing frame...");
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
