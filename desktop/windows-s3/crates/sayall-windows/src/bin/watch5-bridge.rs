#[cfg(windows)]
mod host {
    use sayall_windows::{
        send_input::{KeyChord, KeyCode},
        send_input_windows::SendInputRuntime,
        volcengine,
    };
    use serde_json::{json, Value};
    use std::{
        io::{Read, Write},
        net::{TcpListener, TcpStream},
        thread,
        time::Duration,
    };

    fn reply(socket: &mut TcpStream, value: Value) -> std::io::Result<()> {
        serde_json::to_writer(&mut *socket, &value)?;
        socket.write_all(b"\n")
    }

    fn serve(socket: &mut TcpStream, token: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        socket.set_nodelay(true)?;
        socket.set_read_timeout(Some(Duration::from_secs(4)))?;
        socket.set_write_timeout(Some(Duration::from_secs(3)))?;
        let mut auth = [0; 32];
        socket.read_exact(&mut auth)?;
        if auth != token {
            return Err("配对已过期".into());
        }
        let mut voice: Option<volcengine::Session> = None;
        loop {
            let mut header = [0; 3];
            socket.read_exact(&mut header)?;
            let len = u16::from_be_bytes([header[1], header[2]]) as usize;
            if len > 4096 || (header[0] != b'A' && len != 0) {
                return Err("无效的数据帧".into());
            }
            let mut data = vec![0; len];
            socket.read_exact(&mut data)?;
            let value = match header[0] {
                b'P' if voice.is_none() => json!({"phase":"ready"}),
                b'S' if voice.is_none() => {
                    voice = Some(volcengine::Session::begin()?);
                    json!({"phase":"listening"})
                }
                b'A' if len > 0 && len % 2 == 0 => {
                    let session = voice.as_mut().ok_or("没有录音会话")?;
                    session.audio(
                        data.chunks_exact(2)
                            .map(|v| i16::from_le_bytes([v[0], v[1]]))
                            .collect(),
                        false,
                    )?;
                    if let Some(result) = session.poll() {
                        result?;
                        return Err("语音会话已提前结束".into());
                    }
                    json!(volcengine::status())
                }
                b'E' => {
                    let mut session = voice.take().ok_or("没有录音会话")?;
                    session.audio(vec![], true)?;
                    loop {
                        if let Some(result) = session.poll() {
                            result?;
                            break;
                        }
                        thread::sleep(Duration::from_millis(30));
                    }
                    json!(volcengine::status())
                }
                key @ (b'R' | b'U' | b'V') if voice.is_none() => {
                    let keys = match key {
                        b'R' => vec![KeyCode::Enter],
                        b'U' => vec![KeyCode::Control, KeyCode::Z],
                        _ => vec![KeyCode::Control, KeyCode::V],
                    };
                    SendInputRuntime::new().tap(KeyChord { keys })?;
                    json!({"phase":"submitted"})
                }
                _ => return Err("无效操作".into()),
            };
            reply(socket, value)?;
            if voice.is_none() {
                return Ok(());
            }
        }
    }
    pub fn run() -> Result<(), Box<dyn std::error::Error>> {
        let token = std::env::var("VOICESTICK_WATCH_TOKEN")?;
        if token.len() != 32 || !token.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err("无效配对码".into());
        }
        let listener = TcpListener::bind(("127.0.0.1", 38465))?;
        for stream in listener.incoming() {
            let mut socket = stream?;
            if let Err(error) = serve(&mut socket, token.as_bytes()) {
                let _ = reply(
                    &mut socket,
                    json!({"phase":"error","lastError":error.to_string()}),
                );
            }
        }
        Ok(())
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        const TOKEN: &[u8; 32] = b"00112233445566778899aabbccddeeff";

        fn exchange(token: &[u8], frame: &[u8]) -> (String, Vec<u8>) {
            let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
            let mut client = TcpStream::connect(listener.local_addr().unwrap()).unwrap();
            client.write_all(token).unwrap();
            client.write_all(frame).unwrap();
            client.shutdown(std::net::Shutdown::Write).unwrap();
            let (mut server, _) = listener.accept().unwrap();
            let result = serve(&mut server, TOKEN)
                .err()
                .map(|e| e.to_string())
                .unwrap_or_default();
            server.shutdown(std::net::Shutdown::Write).unwrap();
            let mut data = Vec::new();
            client.read_to_end(&mut data).unwrap();
            (result, data)
        }

        #[test]
        fn authenticated_ping_replies_ready() {
            let (error, data) = exchange(TOKEN, b"P\0\0");
            assert!(error.is_empty());
            assert_eq!(
                serde_json::from_slice::<Value>(&data).unwrap()["phase"],
                "ready"
            );
        }

        #[test]
        fn rejects_unknown_peers_and_audio_without_session() {
            assert_eq!(exchange(&[0; 32], b"").0, "配对已过期");
            assert_eq!(exchange(TOKEN, b"A\0\x02\0\0").0, "没有录音会话");
            assert_eq!(exchange(TOKEN, b"A\xff\xff").0, "无效的数据帧");
        }
    }
}
fn main() {
    #[cfg(windows)]
    if let Err(error) = host::run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
