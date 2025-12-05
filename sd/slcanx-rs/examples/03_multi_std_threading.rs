use clap::Parser;
use slcanx::{Slcanx, CanFrame};
use std::time::Duration;
use anyhow::Result;
use std::thread;
use std::sync::Arc;

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "COM3")]
    port: String,
    #[arg(short, long, default_value_t = 500000)]
    bitrate: u32,
}

fn main() -> Result<()> {
    env_logger::init();
    let args = Args::parse();

    // Use Arc to share slcan instance if needed, though Slcanx handles are cloneable? 
    // No, Slcanx is not Clone, but we can wrap it in Arc.
    let slcan = Arc::new(Slcanx::new(&args.port, 115200, Duration::from_micros(125))?);

    // Start 4 channels
    for i in 0..4 {
        let ch = slcan.get_channel(i);
        ch.close()?;
        ch.set_bitrate(args.bitrate)?;
        ch.open()?;

        let tx_ch = ch.clone();
        thread::spawn(move || {
            let mut cnt = 0u8;
            loop {
                let frame = CanFrame::new_std(0x200 + i as u32, &[i, cnt]);
                let _ = tx_ch.send(frame);
                cnt = cnt.wrapping_add(1);
                thread::sleep(Duration::from_secs(1));
            }
        });
    }

    println!("Listening on all 4 channels...");
    loop {
        if let Some((ch, msg)) = slcan.recv_timeout(Duration::from_millis(100))? {
            println!("[Ch{}] Rx: {:?}", ch, msg);
        }
    }
}
