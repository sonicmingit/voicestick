use crate::{
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

fn serve(
    socket: &mut TcpStream,
    token: &[u8],
    mut active: impl FnMut() -> bool,
) -> Result<(), Box<dyn std::error::Error>> {
    // Winsock inherits the listener's nonblocking mode on accept. Frame reads
    // must wait for the next microphone packet, subject to the timeouts below.
    socket.set_nonblocking(false)?;
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
        if len > 4096 || (!matches!(header[0], b'A' | b'K') && len != 0) {
            return Err("无效的数据帧".into());
        }
        if !active() {
            return Err("手表已断开".into());
        }
        let mut data = vec![0; len];
        socket.read_exact(&mut data)?;
        let value = match header[0] {
            b'K' if voice.is_none() && len == 2 && data.iter().all(|n| *n < 3) => {
                let reported = [data[0], data[1]];
                let mut keys = KEYS.lock().unwrap();
                keys.reported = Some(reported);
                if keys.pending == Some(reported) {
                    keys.pending = None;
                }
                json!({"phase":"ready","keys":keys.pending})
            }
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
                    if !active() {
                        return Err("手表已断开".into());
                    }
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
        if let Err(error) = serve(&mut socket, token.as_bytes(), || true) {
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
        let result = serve(&mut server, TOKEN, || true)
            .err()
            .map(|e| e.to_string())
            .unwrap_or_default();
        server.shutdown(std::net::Shutdown::Write).unwrap();
        let mut data = Vec::new();
        client.read_to_end(&mut data).unwrap();
        (result, data)
    }

    #[test]
    fn integrated_listener_waits_for_delayed_frame_data() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        listener.set_nonblocking(true).unwrap();
        let mut client = TcpStream::connect(listener.local_addr().unwrap()).unwrap();
        let (mut server, _) = listener.accept().unwrap();
        client.write_all(TOKEN).unwrap();
        client.write_all(b"P").unwrap();
        let mut delayed = client.try_clone().unwrap();
        let sender = thread::spawn(move || {
            thread::sleep(Duration::from_millis(100));
            delayed.write_all(b"\0\0").unwrap();
        });
        let result = serve(&mut server, TOKEN, || true);
        sender.join().unwrap();
        result.unwrap();
        let mut line = String::new();
        std::io::BufRead::read_line(&mut std::io::BufReader::new(client), &mut line).unwrap();
        assert_eq!(
            serde_json::from_str::<Value>(&line).unwrap()["phase"],
            "ready"
        );
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
    fn key_change_requires_watch_readback() {
        KEYS.lock().unwrap().pending = Some([2, 0]);
        let (_, first) = exchange(TOKEN, b"K\0\x02\0\x01");
        assert_eq!(
            serde_json::from_slice::<Value>(&first).unwrap()["keys"],
            json!([2, 0])
        );
        exchange(TOKEN, b"K\0\x02\x02\0");
        let state = KEYS.lock().unwrap();
        assert_eq!(state.pending, None);
        assert_eq!(state.reported, Some([2, 0]));
    }

    #[test]
    fn rejects_unknown_peers_and_audio_without_session() {
        assert_eq!(exchange(&[0; 32], b"").0, "配对已过期");
        assert_eq!(exchange(TOKEN, b"A\0\x02\0\0").0, "没有录音会话");
        assert_eq!(exchange(TOKEN, b"A\xff\xff").0, "无效的数据帧");
    }
}

use serde::{Deserialize, Serialize};
use std::{
    os::windows::process::CommandExt,
    path::PathBuf,
    process::Command,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::Instant,
};
const CLIENT_PORT: u16 = 38466;
#[derive(Clone, Serialize, Deserialize)]
struct Pair {
    serial: String,
    token: String,
}
struct Receiver {
    serial: String,
    running: Arc<AtomicBool>,
    seen: Arc<Mutex<Option<Instant>>>,
}
static RECEIVER: Mutex<Option<Receiver>> = Mutex::new(None);
static SETUP: Mutex<()> = Mutex::new(());
#[derive(Default)]
struct Keys {
    reported: Option<[u8; 2]>,
    pending: Option<[u8; 2]>,
}
static KEYS: Mutex<Keys> = Mutex::new(Keys {
    reported: None,
    pending: None,
});
pub fn set_keys(keys: [u8; 2]) -> Result<(), String> {
    let _setup = SETUP.lock().unwrap();
    if !status().online || keys.iter().any(|k| *k > 2) {
        return Err("请先连接手表".into());
    }
    let mut state = KEYS.lock().unwrap();
    if state.reported.is_none() {
        return Err("请更新手表应用".into());
    }
    state.pending = Some(keys);
    Ok(())
}
#[derive(Serialize)]
pub struct WatchStatus {
    pub enabled: bool,
    pub online: bool,
    pub serial: String,
    pub keys: Option<[u8; 2]>,
    pub pending: bool,
}
pub fn status() -> WatchStatus {
    let receiver = RECEIVER.lock().unwrap();
    let keys = KEYS.lock().unwrap();
    match receiver.as_ref() {
        Some(r) => WatchStatus {
            enabled: true,
            keys: keys.reported,
            pending: keys.pending.is_some(),
            serial: r.serial.clone(),
            online: r
                .seen
                .lock()
                .unwrap()
                .is_some_and(|v| v.elapsed() < Duration::from_secs(15)),
        },
        None => WatchStatus {
            enabled: false,
            keys: None,
            pending: false,
            online: false,
            serial: String::new(),
        },
    }
}
fn config_path() -> Result<PathBuf, String> {
    Ok(
        PathBuf::from(std::env::var_os("LOCALAPPDATA").ok_or("无法读取本地配置目录")?)
            .join("VoiceStickS3")
            .join("watch-pair.json"),
    )
}
fn start(listener: TcpListener, pair: Pair) -> Result<(), String> {
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    let running = Arc::new(AtomicBool::new(true));
    let seen = Arc::new(Mutex::new(None));
    let state = Receiver {
        serial: pair.serial,
        running: running.clone(),
        seen: seen.clone(),
    };
    thread::Builder::new()
        .name("watch5-input".into())
        .spawn(move || {
            while running.load(Ordering::Acquire) {
                match listener.accept() {
                    Ok((mut stream, _)) => {
                        let result = serve(&mut stream, pair.token.as_bytes(), || {
                            *seen.lock().unwrap() = Some(Instant::now());
                            running.load(Ordering::Acquire)
                        });
                        if let Err(e) = result {
                            let _ = reply(
                                &mut stream,
                                json!({"phase":"error","lastError":e.to_string()}),
                            );
                        }
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                        thread::sleep(Duration::from_millis(50))
                    }
                    Err(_) => break,
                }
            }
        })
        .map_err(|e| e.to_string())?;
    *KEYS.lock().unwrap() = Keys::default();
    *RECEIVER.lock().unwrap() = Some(state);
    Ok(())
}
fn bind() -> Result<TcpListener, String> {
    TcpListener::bind(("127.0.0.1", CLIENT_PORT))
        .map_err(|_| "手表接收端口被占用，请稍后重试".to_string())
}
pub fn restore() -> Result<(), String> {
    let _setup = SETUP.lock().unwrap();
    let path = config_path()?;
    if path.is_file() && !status().enabled {
        let pair = serde_json::from_slice(&std::fs::read(path).map_err(|e| e.to_string())?)
            .map_err(|_| "手表配置异常，请重新连接")?;
        start(bind()?, pair)?;
    }
    Ok(())
}
fn adb(args: &[&str]) -> Result<String, String> {
    let out = Command::new("adb.exe")
        .creation_flags(0x08000000)
        .args(args)
        .output()
        .map_err(|_| "找不到 adb.exe，请安装 Android Platform Tools 并加入 PATH")?;
    if !out.status.success() {
        return Err(format!(
            "ADB 操作失败：{}",
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}
pub fn connect(address: String) -> Result<WatchStatus, String> {
    let _setup = SETUP.lock().unwrap();
    let addr = address.trim();
    if !addr.is_empty() {
        let endpoint: std::net::SocketAddr = addr
            .parse()
            .map_err(|_| "请输入无线调试地址和端口，例如 192.168.1.10:39000")?;
        adb(&["connect", &endpoint.to_string()])?;
    }
    let devices = adb(&["devices"])?;
    let mut watches = Vec::new();
    for line in devices.lines().skip(1) {
        let mut fields = line.split_whitespace();
        let Some(serial) = fields.next() else {
            continue;
        };
        if fields.next() != Some("device") || (!addr.is_empty() && addr != serial) {
            continue;
        }
        if adb(&["-s", serial, "shell", "getprop", "ro.product.model"])
            .is_ok_and(|m| m == "M2505W1")
        {
            watches.push(serial);
        }
    }
    if watches.len() != 1 {
        return Err(if watches.is_empty() {
            "没有找到已授权的 Watch 5，请先配对无线调试，再输入连接地址"
        } else {
            "检测到多块手表，请输入目标手表的无线调试地址"
        }
        .into());
    }
    let serial = watches[0];
    let path = config_path()?;
    let current = status();
    if current.enabled && current.serial != serial {
        return Err("请先断开已配置的手表".into());
    }
    let listener = if current.enabled { None } else { Some(bind()?) };
    let pair: Pair = if current.enabled {
        serde_json::from_slice(&std::fs::read(&path).map_err(|e| e.to_string())?)
            .map_err(|_| "请先断开并重新连接")?
    } else {
        Pair {
            serial: serial.into(),
            token: uuid::Uuid::new_v4().simple().to_string(),
        }
    };
    let info = adb(&[
        "-s",
        serial,
        "shell",
        "dumpsys",
        "package",
        "cn.sonic.voicestick.watch",
    ])?;
    let version = info
        .split_whitespace()
        .find_map(|v| v.strip_prefix("versionCode=")?.parse::<u32>().ok())
        .unwrap_or(0);
    if version < 2 {
        let apk = std::env::current_exe()
            .map_err(|e| e.to_string())?
            .with_file_name("VoiceStick-Watch-0.1.1.apk");
        if !apk.is_file() {
            return Err("请将 VoiceStick-Watch-0.1.1.apk 放到客户端同一目录后重试".into());
        }
        adb(&[
            "-s",
            serial,
            "install",
            "-r",
            apk.to_str().ok_or("APK 路径无效")?,
        ])?;
    }
    adb(&[
        "-s",
        serial,
        "reverse",
        "tcp:38465",
        &format!("tcp:{CLIENT_PORT}"),
    ])?;
    let launch = adb(&[
        "-s",
        serial,
        "shell",
        "am",
        "start",
        "-n",
        "cn.sonic.voicestick.watch/.WatchActivity",
        "--es",
        "pair",
        &pair.token,
    ])?;
    if launch.contains("Error") {
        return Err("未能打开手表应用".into());
    }
    if let Some(listener) = listener {
        std::fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
        std::fs::write(path, serde_json::to_vec(&pair).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
        start(listener, pair)?;
    }
    Ok(status())
}
pub fn disconnect() -> Result<WatchStatus, String> {
    let _setup = SETUP.lock().unwrap();
    let path = config_path()?;
    if path.exists() {
        std::fs::remove_file(path).map_err(|e| e.to_string())?;
    }
    if let Some(r) = RECEIVER.lock().unwrap().take() {
        r.running.store(false, Ordering::Release);
    }
    Ok(status())
}
