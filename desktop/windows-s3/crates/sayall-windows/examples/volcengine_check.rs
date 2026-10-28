fn main() {
    #[cfg(windows)]
    {
        if std::env::args().nth(1).as_deref() == Some("--fixture") {
            let result = (|| -> Result<(), String> {
                let path = std::env::args().nth(2).ok_or("missing fixture")?;
                let wave = std::fs::read(path).map_err(|_| "fixture read failed")?;
                if wave.get(..4) != Some(b"RIFF") || wave.get(8..12) != Some(b"WAVE") {
                    return Err("invalid fixture".into());
                }
                let mut offset = 12;
                let mut pcm = None;
                while offset + 8 <= wave.len() {
                    let length =
                        u32::from_le_bytes(wave[offset + 4..offset + 8].try_into().unwrap())
                            as usize;
                    let body = wave
                        .get(offset + 8..offset + 8 + length)
                        .ok_or("invalid WAV chunk")?;
                    if &wave[offset..offset + 4] == b"data" {
                        pcm = Some(body);
                        break;
                    }
                    offset += 8 + length + (length % 2);
                }
                let samples: Vec<i16> = pcm
                    .ok_or("missing PCM")?
                    .chunks_exact(2)
                    .map(|b| i16::from_le_bytes([b[0], b[1]]))
                    .collect();
                let mut encoder =
                    opus::Encoder::new(16000, opus::Channels::Mono, opus::Application::Voip)
                        .map_err(|_| "opus encoder")?;
                let mut decoder =
                    opus::Decoder::new(16000, opus::Channels::Mono).map_err(|_| "opus decoder")?;
                let mut session = sayall_windows::volcengine::Session::begin()?;
                let chunks = samples.chunks(960).collect::<Vec<_>>();
                for (index, chunk) in chunks.iter().enumerate() {
                    let mut padded = vec![0i16; 960];
                    padded[..chunk.len()].copy_from_slice(chunk);
                    let mut packet = vec![0u8; 1024];
                    let len = encoder
                        .encode(&padded, &mut packet)
                        .map_err(|_| "opus encode failed")?;
                    let mut decoded = vec![0i16; 1920];
                    let count = decoder
                        .decode(&packet[..len], &mut decoded, false)
                        .map_err(|_| "opus decode failed")?;
                    decoded.truncate(count);
                    session.audio(decoded, index + 1 == chunks.len())?;
                    std::thread::sleep(std::time::Duration::from_millis(60));
                }
                for _ in 0..350 {
                    if let Some(result) = session.poll() {
                        result?;
                        let status = sayall_windows::volcengine::status();
                        println!(
                            "fixture_recognized={} output_phase={}",
                            !status.text.is_empty(),
                            status.phase
                        );
                        return Ok(());
                    }
                    std::thread::sleep(std::time::Duration::from_millis(100));
                }
                Err("fixture timed out".into())
            })();
            if let Err(error) = result {
                eprintln!("{error}");
                std::process::exit(1)
            }
            return;
        }
        match sayall_windows::volcengine::config_snapshot() {
            Ok(config) => println!("key_configured={}", config.key_configured),
            Err(error) => {
                eprintln!("{error}");
                std::process::exit(1)
            }
        }
        match sayall_windows::volcengine::check_connection() {
            Ok(()) => println!("api_final_response=passed audio=silence injection=disabled"),
            Err(error) => {
                eprintln!("{error}");
                std::process::exit(1)
            }
        }
    }
}
