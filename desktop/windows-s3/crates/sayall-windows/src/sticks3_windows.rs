//! StickS3 BLE transport with direct Volcengine API speech recognition.
//! Firmware owns pre-roll and gestures; speech never emits a hardware voice chord.
use crate::{
    audio::AudioRuntime,
    ble::{self, gatt_note, wait_async, WorkerMessage},
    button_mapping::{ButtonMappingRuntime, EngineMessage, FiredGesture},
    power::PowerNotifications,
    send_input::{KeyChord, KeyCode},
    send_input_windows::SendInputRuntime,
    sticks3,
    transcript_capture::TranscriptCaptureRuntime,
    ConnectionPhase, ConnectionSnapshot, PlatformError, RemoteModel, UsageCounters,
};
use opus::{Channels, Decoder};
use sayall_core::{process_pcm, VoiceInputTool, VoiceSessionState};
use std::{
    collections::VecDeque,
    sync::{
        mpsc::{self, Receiver, Sender},
        Arc, Mutex,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};
use windows::{
    core::{IInspectable, GUID},
    Devices::Bluetooth::GenericAttributeProfile::{
        GattCharacteristic, GattClientCharacteristicConfigurationDescriptorValue as Cccd,
        GattCommunicationStatus, GattDeviceService, GattValueChangedEventArgs, GattWriteOption,
    },
    Devices::Bluetooth::{BluetoothCacheMode, BluetoothConnectionStatus, BluetoothLEDevice},
    Foundation::TypedEventHandler,
    Storage::Streams::{DataReader, DataWriter},
    Win32::System::WinRT::{RoInitialize, RoUninitialize, RO_INIT_MULTITHREADED},
};

const IO_TIMEOUT: Duration = Duration::from_secs(5);
fn lock<T>(value: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    value.lock().unwrap_or_else(|e| e.into_inner())
}
fn win(error: windows::core::Error) -> PlatformError {
    PlatformError::WindowsApi(error.to_string())
}
fn protocol(error: impl Into<String>) -> PlatformError {
    PlatformError::Protocol(error.into())
}

pub struct StickRuntime {
    sender: Sender<WorkerMessage>,
    state: Arc<Mutex<ConnectionSnapshot>>,
    worker: Mutex<Option<JoinHandle<()>>>,
    power: Mutex<Option<PowerNotifications>>,
}

impl StickRuntime {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        audio: Arc<AudioRuntime>,
        usage: Arc<UsageCounters>,
        input: Arc<SendInputRuntime>,
        chord: Arc<Mutex<Option<KeyChord>>>,
        tool: Arc<Mutex<Option<VoiceInputTool>>>,
        gain: Arc<Mutex<f32>>,
        capture: Arc<TranscriptCaptureRuntime>,
        mappings: Arc<ButtonMappingRuntime>,
    ) -> Self {
        let (sender, receiver) = mpsc::channel();
        let state = Arc::new(Mutex::new(ConnectionSnapshot::default()));
        let context = Context {
            state: state.clone(),
            sender: sender.clone(),
            usage,
            gain,
            mappings,
            capture,
            audio,
            input,
            chord,
            tool,
        };
        let worker = thread::Builder::new()
            .name("sticks3-ble".into())
            .spawn(move || run(receiver, context));
        if worker.is_err() {
            lock(&state).last_error = Some("无法启动设备连接".into());
        }
        let power = PowerNotifications::register(sender.clone()).ok();
        Self {
            sender,
            state,
            worker: Mutex::new(worker.ok()),
            power: Mutex::new(power),
        }
    }
    pub fn snapshot(&self) -> ConnectionSnapshot {
        let mut value = lock(&self.state).clone();
        value.power_notifications_available = lock(&self.power).is_some();
        value
    }
    fn request(
        &self,
        make: impl FnOnce(Sender<Result<ConnectionSnapshot, PlatformError>>) -> WorkerMessage,
    ) -> Result<ConnectionSnapshot, PlatformError> {
        let (sender, receiver) = mpsc::channel();
        self.sender
            .send(make(sender))
            .map_err(|_| PlatformError::WorkerUnavailable)?;
        receiver
            .recv_timeout(Duration::from_secs(30))
            .map_err(|_| PlatformError::OperationTimedOut)?
    }
    pub fn connect(&self, device_id: String) -> Result<ConnectionSnapshot, PlatformError> {
        self.request(|reply| WorkerMessage::Connect { device_id, reply })
    }
    pub fn restore(&self, device_id: String) -> Result<ConnectionSnapshot, PlatformError> {
        self.request(|reply| WorkerMessage::Restore { device_id, reply })
    }
    pub fn disconnect(&self) -> Result<ConnectionSnapshot, PlatformError> {
        self.request(|reply| WorkerMessage::Disconnect { reply })
    }
    pub fn wake_reconnect(&self) {
        let _ = self.sender.send(WorkerMessage::WakeReconnect);
    }
    pub fn notify_remote_device_arrived(&self) {
        self.wake_reconnect();
    }
    pub fn shutdown_blocking(&self, timeout: Duration) -> Result<(), PlatformError> {
        let (sender, receiver) = mpsc::channel();
        self.sender
            .send(WorkerMessage::Shutdown { ack: Some(sender) })
            .map_err(|_| PlatformError::WorkerUnavailable)?;
        receiver
            .recv_timeout(timeout)
            .map_err(|_| PlatformError::OperationTimedOut)
    }
}
impl Drop for StickRuntime {
    fn drop(&mut self) {
        lock(&self.power).take();
        let _ = self.sender.send(WorkerMessage::Shutdown { ack: None });
        // Explicit shutdown handles the bounded wait; Drop must not block Tauri teardown.
        lock(&self.worker).take();
    }
}

struct Context {
    state: Arc<Mutex<ConnectionSnapshot>>,
    sender: Sender<WorkerMessage>,
    usage: Arc<UsageCounters>,
    gain: Arc<Mutex<f32>>,
    mappings: Arc<ButtonMappingRuntime>,
    capture: Arc<TranscriptCaptureRuntime>,
    audio: Arc<AudioRuntime>,
    input: Arc<SendInputRuntime>,
    chord: Arc<Mutex<Option<KeyChord>>>,
    tool: Arc<Mutex<Option<VoiceInputTool>>>,
}

struct Link {
    device: BluetoothLEDevice,
    service: Option<GattDeviceService>,
    audio: Option<GattCharacteristic>,
    state: Option<GattCharacteristic>,
    control: Option<GattCharacteristic>,
    audio_token: Option<i64>,
    state_token: Option<i64>,
    connection_token: Option<i64>,
    generation: u64,
    rev: u32,
    map: String,
    ready: bool,
    hid: bool,
    deadline: Instant,
}
impl Link {
    fn open(id: &str, generation: u64, context: &Context) -> Result<Self, PlatformError> {
        let address =
            ble::bluetooth_address_from_device_id(id).ok_or_else(|| protocol("设备标识无效"))?;
        let device = wait_async(
            BluetoothLEDevice::FromBluetoothAddressAsync(address).map_err(win)?,
            Duration::from_secs(10),
            "sticks3_create",
        )?;
        let mut link = Self {
            device,
            service: None,
            audio: None,
            state: None,
            control: None,
            audio_token: None,
            state_token: None,
            connection_token: None,
            generation,
            rev: 0,
            map: String::new(),
            ready: false,
            hid: false,
            deadline: Instant::now() + Duration::from_secs(12),
        };
        let name = link.device.Name().map_err(win)?.to_string();
        if !sticks3::is_device_name(&name) {
            return Err(protocol("请选择 VS- 开头的 StickS3"));
        }
        {
            let mut state = lock(&context.state);
            state.remote_name = Some(name);
            state.remote_model = RemoteModel::StickS3;
            state.remote_token = Some(crate::device_token(id));
            state.phase = ConnectionPhase::Discovering;
            state.battery_level = None;
        }
        // VoiceStick's Windows transport enumerates the service table before
        // matching its 128-bit UUID. The filtered Xiaomi query fails with
        // ERROR_BAD_COMMAND on this paired ESP32/NimBLE device.
        let result = wait_async(
            link.device
                .GetGattServicesWithCacheModeAsync(BluetoothCacheMode::Uncached)
                .map_err(win)?,
            IO_TIMEOUT,
            "sticks3_services",
        )?;
        if result.Status().map_err(win)? != GattCommunicationStatus::Success {
            return Err(protocol("设备服务暂未就绪"));
        }
        let services = result.Services().map_err(win)?;
        for index in 0..services.Size().map_err(win)? {
            let service = services.GetAt(index).map_err(win)?;
            if service.Uuid().map_err(win)? == GUID::from_u128(sticks3::SERVICE) {
                link.service = Some(service);
            } else {
                let _ = service.Close();
            }
        }
        if link.service.is_none() {
            return Err(PlatformError::VoiceServiceMissing);
        }
        let service = link.service.as_ref().unwrap();
        link.control = Some(ble::find_characteristic(
            service,
            GUID::from_u128(sticks3::CONTROL),
            "sticks3_control",
        )?);
        link.state = Some(ble::find_characteristic(
            service,
            GUID::from_u128(sticks3::STATE),
            "sticks3_state",
        )?);
        link.audio = Some(ble::find_characteristic(
            service,
            GUID::from_u128(sticks3::AUDIO),
            "sticks3_audio",
        )?);
        // Subscribe audio first. Firmware must never be marked ready with only state notifications.
        link.audio_token = Some(subscribe(
            link.audio.as_ref().unwrap(),
            context.sender.clone(),
            generation,
            true,
        )?);
        link.state_token = Some(subscribe(
            link.state.as_ref().unwrap(),
            context.sender.clone(),
            generation,
            false,
        )?);
        let sender = context.sender.clone();
        link.connection_token = Some(
            link.device
                .ConnectionStatusChanged(
                    &TypedEventHandler::<BluetoothLEDevice, IInspectable>::new(move |device, _| {
                        if let Some(device) = device.as_ref() {
                            if let Ok(status) = device.ConnectionStatus() {
                                let _ = sender.send(WorkerMessage::ConnectionChanged {
                                    connection_generation: generation,
                                    status,
                                });
                            }
                        }
                        Ok(())
                    }),
                )
                .map_err(win)?,
        );
        let _ = link.write(serde_json::json!({"event":"battery_request"}));
        link.write(serde_json::json!({"event":"buttons_off"}))?;
        link.write(serde_json::json!({"event":"ui_state","state":"ready","text":""}))?;
        // ui_state:ready causes device_info on existing VoiceStick firmware.
        lock(&context.state).phase = ConnectionPhase::AwaitingCapabilities;
        gatt_note(format!(
            "sticks3_connect phase=subscribed generation={generation}"
        ));
        Ok(link)
    }
    fn write(&self, json: serde_json::Value) -> Result<(), PlatformError> {
        let bytes = serde_json::to_vec(&json).map_err(|_| protocol("control_encode_failed"))?;
        let writer = DataWriter::new().map_err(win)?;
        writer.WriteBytes(&bytes).map_err(win)?;
        let buffer = writer.DetachBuffer().map_err(win)?;
        let _ = writer.Close();
        let control = self
            .control
            .as_ref()
            .ok_or_else(|| protocol("control_not_ready"))?;
        let status = wait_async(
            control
                .WriteValueWithOptionAsync(&buffer, GattWriteOption::WriteWithoutResponse)
                .map_err(win)?,
            IO_TIMEOUT,
            "sticks3_write",
        )?;
        if status != GattCommunicationStatus::Success {
            return Err(protocol("设备控制通道暂时不可用"));
        }
        Ok(())
    }
    fn configure(&mut self, context: &Context) -> Result<(), PlatformError> {
        self.rev = self.rev.wrapping_add(1).max(1);
        self.ready = false;
        self.map = sticks3::button_map(&context.mappings.mappings());
        self.deadline = Instant::now() + Duration::from_secs(8);
        self.write(serde_json::json!({"event":"buttons","rev":self.rev,"long_ms":500,"double_ms":300,"map":self.map}))?;
        gatt_note(format!(
            "sticks3_buttons phase=requested generation={} rev={}",
            self.generation, self.rev
        ));
        Ok(())
    }
    fn key(&self, voice: &ImeVoice, down: bool, cancel: bool) -> Result<(), PlatformError> {
        self.write(
            serde_json::json!({"event":"voice_key","session_id":voice.id,"down":u8::from(down),
            "cancel":u8::from(cancel),"shortcut":voice.hardware.unwrap_or(1)}),
        )
    }
    fn ui(&self, state: &str) {
        let _ = self.write(serde_json::json!({"event":"ui_state","state":state,"text":""}));
    }
}
impl Drop for Link {
    fn drop(&mut self) {
        if let Some(token) = self.audio_token.take() {
            if let Some(audio) = &self.audio {
                let _ = audio.RemoveValueChanged(token);
            }
        }
        if let Some(token) = self.state_token.take() {
            if let Some(state) = &self.state {
                let _ = state.RemoveValueChanged(token);
            }
        }
        if let Some(token) = self.connection_token.take() {
            let _ = self.device.RemoveConnectionStatusChanged(token);
        }
        for characteristic in [&self.audio, &self.state].into_iter().flatten() {
            if let Ok(operation) =
                characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(Cccd::None)
            {
                let _ = wait_async(operation, Duration::from_secs(1), "sticks3_unsubscribe");
            }
        }
        if let Some(service) = self.service.take() {
            let _ = service.Close();
        }
        let _ = self.device.Close();
        gatt_note(format!(
            "sticks3_connect phase=closed generation={}",
            self.generation
        ));
    }
}
fn subscribe(
    characteristic: &GattCharacteristic,
    sender: Sender<WorkerMessage>,
    generation: u64,
    audio: bool,
) -> Result<i64, PlatformError> {
    let token = characteristic
        .ValueChanged(&TypedEventHandler::<
            GattCharacteristic,
            GattValueChangedEventArgs,
        >::new(move |_, args| {
            if let Some(args) = args.as_ref() {
                if let Ok(buffer) = args.CharacteristicValue() {
                    let result = (|| -> windows::core::Result<Vec<u8>> {
                        let mut bytes = vec![0; buffer.Length()? as usize];
                        let reader = DataReader::FromBuffer(&buffer)?;
                        reader.ReadBytes(&mut bytes)?;
                        let _ = reader.Close();
                        Ok(bytes)
                    })();
                    match result {
                        Ok(bytes) => {
                            let _ = sender.send(if audio {
                                WorkerMessage::Audio {
                                    connection_generation: generation,
                                    bytes,
                                }
                            } else {
                                WorkerMessage::Control {
                                    connection_generation: generation,
                                    bytes,
                                }
                            });
                        }
                        Err(_) => {
                            let _ = sender.send(WorkerMessage::CallbackError {
                                connection_generation: generation,
                                error: "notification_read_failed".into(),
                            });
                        }
                    }
                }
            }
            Ok(())
        }))
        .map_err(win)?;
    let result = (|| {
        for value in [Cccd::None, Cccd::Notify] {
            let status = wait_async(
                characteristic
                    .WriteClientCharacteristicConfigurationDescriptorAsync(value)
                    .map_err(win)?,
                IO_TIMEOUT,
                "sticks3_subscribe",
            )?;
            if status != GattCommunicationStatus::Success {
                return Err(protocol("设备通知订阅失败"));
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        let _ = characteristic.RemoveValueChanged(token);
        return Err(error);
    }
    Ok(token)
}

struct ApiVoice {
    id: u32,
    generation: u64,
    decoder: Decoder,
    sequence: sticks3::StreamSequence,
    samples: u64,
    started: Instant,
    last_audio: Instant,
    stop_deadline: Option<Instant>,
    end_received: bool,
    asr: crate::volcengine::Session,
}
impl ApiVoice {
    fn begin(
        id: u32,
        generation: u64,
        link: &Link,
        context: &Context,
    ) -> Result<Self, PlatformError> {
        let decoder =
            Decoder::new(16_000, Channels::Mono).map_err(|_| protocol("opus_init_failed"))?;
        let asr = crate::volcengine::Session::begin().map_err(protocol)?;
        let now = chrono::Utc::now().timestamp_millis();
        let capture_id = crate::new_session_id();
        context.capture.begin_pending(now);
        context.capture.bind_session(capture_id.clone());
        context
            .capture
            .start_audio_recording(&capture_id, now, None);
        context
            .usage
            .begin_voice_session(crate::foreground::foreground_application_name());
        link.ui("recording");
        gatt_note(format!(
            "sticks3_voice phase=starting session={id} generation={generation} provider=volcengine"
        ));
        Ok(Self {
            id,
            generation,
            decoder,
            sequence: sticks3::StreamSequence::new(id),
            samples: 0,
            started: Instant::now(),
            last_audio: Instant::now(),
            stop_deadline: None,
            end_received: false,
            asr,
        })
    }
    fn audio(
        &mut self,
        frame: sticks3::AudioFrame<'_>,
        context: &Context,
    ) -> Result<bool, PlatformError> {
        if !self.sequence.accept(&frame).map_err(protocol)? {
            return Ok(false);
        }
        self.last_audio = Instant::now();
        let mut samples = vec![];
        if !frame.payload.is_empty() {
            samples.resize(1920, 0);
            let count = self
                .decoder
                .decode(frame.payload, &mut samples, false)
                .map_err(|_| protocol("opus_decode_failed"))?;
            samples.truncate(count);
            samples = process_pcm(&samples, *lock(&context.gain));
            self.samples += samples.len() as u64;
            context.capture.append_audio(&samples);
        }
        self.asr.audio(samples, frame.end).map_err(protocol)?;
        self.end_received |= frame.end;
        Ok(frame.end)
    }
}
struct ImeVoice {
    id: u32,
    generation: u64,
    decoder: Decoder,
    sequence: sticks3::StreamSequence,
    samples: u64,
    enqueued: u64,
    hardware: Option<u8>,
    software: Option<KeyChord>,
    down: bool,
    releasing: bool,
    started: Instant,
    last_audio: Instant,
    heartbeat: Instant,
    stop_deadline: Option<Instant>,
    pending: VecDeque<Vec<i16>>,
    pending_samples: usize,
    end_received: bool,
    requires_window: bool,
    microphone_ready: bool,
    drain: Option<Receiver<Result<crate::AudioSnapshot, PlatformError>>>,
}
fn hardware_shortcut(chord: &KeyChord) -> Option<u8> {
    match chord.keys.as_slice() {
        [KeyCode::RightAlt] => Some(1),
        [KeyCode::LeftControl, KeyCode::LeftWindows] | [KeyCode::Control, KeyCode::LeftWindows] => {
            Some(2)
        }
        _ => None,
    }
}

impl ImeVoice {
    fn begin(
        id: u32,
        generation: u64,
        link: &Link,
        context: &Context,
    ) -> Result<Self, PlatformError> {
        let decoder =
            Decoder::new(16_000, Channels::Mono).map_err(|_| protocol("opus_init_failed"))?;
        context.audio.begin_session(generation)?;
        let mut voice = Self {
            id,
            generation,
            decoder,
            sequence: sticks3::StreamSequence::new(id),
            samples: 0,
            enqueued: 0,
            hardware: None,
            software: None,
            down: false,
            releasing: false,
            started: Instant::now(),
            last_audio: Instant::now(),
            heartbeat: Instant::now(),
            stop_deadline: None,
            pending: VecDeque::new(),
            pending_samples: 0,
            end_received: false,
            drain: None,
            requires_window: *lock(&context.tool) == Some(VoiceInputTool::Doubao),
            microphone_ready: false,
        };
        let result = (|| {
            if voice.requires_window && doubao_window_visible() {
                return Err(protocol("豆包仍在处理上一段语音，请稍候再试"));
            }
            if let Some(tool) = *lock(&context.tool) {
                crate::ime::ensure_session_ime(tool, crate::ime::ImeSwitchScope::VoicePress)
                    .map_err(protocol)?;
            }
            let now = chrono::Utc::now().timestamp_millis();
            let capture_id = crate::new_session_id();
            context.capture.begin_pending(now);
            context.capture.bind_session(capture_id.clone());
            context
                .capture
                .start_audio_recording(&capture_id, now, None);
            let chord = lock(&context.chord).clone();
            let is_doubao = *lock(&context.tool) == Some(VoiceInputTool::Doubao);
            if let Some(chord) = chord {
                if is_doubao && link.hid {
                    voice.hardware = hardware_shortcut(&chord);
                }
                if is_doubao && voice.hardware.is_none() {
                    return Err(protocol(
                        "豆包需要设备键盘通道，请将语音快捷键设为右 Alt 或左 Ctrl + 左 Win",
                    ));
                }
                if voice.hardware.is_some() {
                    link.key(&voice, true, false)?;
                } else {
                    voice.software = Some(chord.clone());
                    context.input.press(&chord)?;
                    voice.down = true;
                }
            } else if is_doubao {
                return Err(protocol("请先选择豆包的按住说话快捷键"));
            } else {
                voice.down = true;
            }
            Ok(())
        })();
        if let Err(error) = result {
            cancel_voice(Some(Voice::Ime(voice)), Some(link), context, "start_failed");
            return Err(error);
        }
        context
            .usage
            .begin_voice_session(crate::foreground::foreground_application_name());
        gatt_note(format!(
            "sticks3_voice phase=starting session={id} generation={generation} hardware={}",
            voice.hardware.is_some()
        ));
        link.ui("recording");
        Ok(voice)
    }
    fn flush(&mut self, context: &Context) -> Result<(), PlatformError> {
        let submitted = context.audio.snapshot().submitted_samples;
        while let Some(front) = self.pending.front() {
            // Pace firmware pre-roll/cold-start buffering. Never dump several seconds
            // into SayAll's two-second WASAPI queue in one callback.
            if self.enqueued.saturating_sub(submitted) + front.len() as u64 > 16_000 {
                break;
            }
            let samples = self.pending.pop_front().unwrap();
            self.pending_samples -= samples.len();
            self.enqueued += samples.len() as u64;
            context.audio.enqueue_samples(self.generation, samples)?;
        }
        Ok(())
    }
    fn audio(
        &mut self,
        frame: sticks3::AudioFrame<'_>,
        context: &Context,
    ) -> Result<bool, PlatformError> {
        if self.releasing || !self.sequence.accept(&frame).map_err(protocol)? {
            return Ok(false);
        }
        self.last_audio = Instant::now();
        if !frame.payload.is_empty() {
            let mut samples = vec![0i16; 1920];
            let len = self
                .decoder
                .decode(frame.payload, &mut samples, false)
                .map_err(|_| protocol("opus_decode_failed"))?;
            samples.truncate(len);
            let samples = process_pcm(&samples, *lock(&context.gain));
            self.samples += samples.len() as u64;
            context.capture.append_audio(&samples);
            self.pending_samples += samples.len();
            self.pending.push_back(samples);
            if self.pending_samples > 160_000 {
                return Err(protocol("语音缓冲已满，请重试"));
            }
        }
        if self.microphone_ready {
            self.flush(context)?;
        }
        self.end_received |= frame.end;
        Ok(frame.end)
    }
}

enum Voice {
    Api(ApiVoice),
    Ime(ImeVoice),
}
impl Voice {
    fn begin(
        id: u32,
        generation: u64,
        link: &Link,
        context: &Context,
    ) -> Result<Self, PlatformError> {
        if lock(&context.tool).is_some() {
            ImeVoice::begin(id, generation, link, context).map(Self::Ime)
        } else {
            ApiVoice::begin(id, generation, link, context).map(Self::Api)
        }
    }
    fn id(&self) -> u32 {
        match self {
            Self::Api(v) => v.id,
            Self::Ime(v) => v.id,
        }
    }
    fn set_stop_deadline(&mut self, duration: Duration) {
        match self {
            Self::Api(v) => v.stop_deadline = Some(Instant::now() + duration),
            Self::Ime(v) => v.stop_deadline = Some(Instant::now() + duration),
        }
    }
    fn audio(
        &mut self,
        frame: sticks3::AudioFrame<'_>,
        context: &Context,
    ) -> Result<bool, PlatformError> {
        match self {
            Self::Api(v) => v.audio(frame, context),
            Self::Ime(v) => v.audio(frame, context),
        }
    }
    fn poll(&mut self, link: &Link, context: &Context) -> Result<bool, PlatformError> {
        match self {
            Self::Api(v) => {
                if v.stop_deadline
                    .is_some_and(|deadline| Instant::now() >= deadline)
                    || (!v.end_received && v.last_audio.elapsed() > Duration::from_secs(8))
                {
                    return Err(protocol("语音会话超时，请重试"));
                }
                match v.asr.poll() {
                    Some(Ok(())) => Ok(true),
                    Some(Err(error)) => Err(protocol(error)),
                    None => Ok(false),
                }
            }
            Self::Ime(v) => v.poll(link, context),
        }
    }
    fn key_ack(&mut self, event: &sticks3::StateEvent) -> Result<bool, PlatformError> {
        let Self::Ime(v) = self else { return Ok(false) };
        if event.session_id != Some(v.id) || v.hardware.is_none() {
            return Ok(false);
        }
        if event.ok != Some(1) {
            return Err(protocol(
                "设备键盘通道未就绪，无法启动输入法语音；可选择火山 API",
            ));
        }
        if event.down == Some(1) && !v.releasing {
            v.down = true;
        }
        if event.down == Some(0) {
            if v.releasing {
                return Ok(true);
            }
            return Err(protocol("设备语音键已提前释放"));
        }
        Ok(false)
    }
}
impl ImeVoice {
    fn poll(&mut self, link: &Link, context: &Context) -> Result<bool, PlatformError> {
        let now = Instant::now();
        if self.stop_deadline.is_some_and(|deadline| now >= deadline)
            || (!self.microphone_ready && self.started.elapsed() > Duration::from_secs(3))
            || (!self.releasing && self.last_audio.elapsed() > Duration::from_secs(8))
        {
            return Err(protocol("输入法语音会话超时"));
        }
        if self.hardware.is_some()
            && !self.releasing
            && self.heartbeat.elapsed() >= Duration::from_millis(500)
        {
            link.key(self, true, false)?;
            self.heartbeat = now;
        }
        if self.down && !self.microphone_ready && (!self.requires_window || doubao_window_visible())
        {
            self.microphone_ready = true;
            self.flush(context)?;
        }
        if self.microphone_ready && !self.releasing {
            self.flush(context)?;
        }
        if self.microphone_ready && !self.releasing && self.end_received {
            if self.drain.is_none() && self.pending.is_empty() {
                self.drain = Some(context.audio.request_finish_session(self.generation)?);
            }
            let drained = match self.drain.as_ref().map(|reply| reply.try_recv()) {
                Some(Ok(Ok(_))) => true,
                Some(Ok(Err(error))) => return Err(error),
                Some(Err(mpsc::TryRecvError::Disconnected)) => {
                    return Err(PlatformError::AudioWorkerUnavailable)
                }
                _ => false,
            };
            if drained {
                self.releasing = true;
                self.stop_deadline = Some(now + Duration::from_secs(3));
                if self.hardware.is_some() {
                    link.key(self, false, false)?;
                } else {
                    return Ok(true);
                }
            }
        }
        if let Some(error) = context.audio.failure() {
            return Err(protocol(error));
        }
        Ok(false)
    }
}
fn doubao_window_visible() -> bool {
    use windows::{
        core::w,
        Win32::UI::WindowsAndMessaging::{FindWindowW, IsWindowVisible},
    };
    unsafe {
        FindWindowW(w!("OimeVoiceWaveWindow"), None)
            .is_ok_and(|window| IsWindowVisible(window).as_bool())
    }
}

fn cancel_voice(
    voice: Option<Voice>,
    link: Option<&Link>,
    context: &Context,
    reason: &'static str,
) {
    if let Some(voice) = voice {
        let id = voice.id();
        if let Voice::Ime(v) = &voice {
            if v.hardware.is_some() {
                if let Some(link) = link {
                    let _ = link.key(v, false, true);
                }
            }
            if let Some(chord) = &v.software {
                let _ = context.input.release(chord);
            }
            let _ = context.audio.interrupt_session();
        }
        context.capture.cancel(reason);
        context.capture.cancel_audio();
        context.usage.finish_voice_session();
        if let Some(link) = link {
            let _ = link.write(serde_json::json!({"event":"record_cancel"}));
            link.ui("ready");
        }
        gatt_note(format!(
            "sticks3_voice phase=closed result=cancelled reason={reason} session={id}"
        ));
    }
    let mut state = lock(&context.state);
    state.voice_state = VoiceSessionState::Idle;
    if matches!(
        state.phase,
        ConnectionPhase::Streaming | ConnectionPhase::Draining
    ) {
        state.phase = ConnectionPhase::Ready;
    }
}

fn run(receiver: Receiver<WorkerMessage>, context: Context) {
    if unsafe { RoInitialize(RO_INIT_MULTITHREADED) }.is_err() {
        lock(&context.state).last_error = Some("蓝牙初始化失败".into());
        return;
    }
    struct Apartment;
    impl Drop for Apartment {
        fn drop(&mut self) {
            unsafe {
                RoUninitialize();
            }
        }
    }
    let _apartment = Apartment;
    let mut link: Option<Link> = None;
    let mut voice: Option<Voice> = None;
    let mut preferred: Option<String> = None;
    let mut suspended = false;
    let mut generation = 0u64;
    let mut audio_generation = 0u64;
    let mut last_session: Option<u32> = None;
    let mut retry_at = Instant::now();
    let mut retry_count = 0u32;
    loop {
        let message = match receiver.recv_timeout(Duration::from_millis(20)) {
            Ok(message) => Some(message),
            Err(mpsc::RecvTimeoutError::Timeout) => None,
            Err(_) => break,
        };
        let mut failure: Option<PlatformError> = None;
        let mut voice_failure: Option<PlatformError> = None;
        if let Some(message) = message {
            match message {
                WorkerMessage::Connect { device_id, reply }
                | WorkerMessage::Restore { device_id, reply } => {
                    cancel_voice(voice.take(), link.as_ref(), &context, "reconnect");
                    link.take();
                    preferred = Some(device_id.clone());
                    suspended = false;
                    generation += 1;
                    last_session = None;
                    lock(&context.state).phase = ConnectionPhase::Connecting;
                    match Link::open(&device_id, generation, &context) {
                        Ok(value) => {
                            link = Some(value);
                            retry_count = 0;
                            let _ = reply.send(Ok(lock(&context.state).clone()));
                        }
                        Err(error) => {
                            let _ = reply.send(Err(protocol("连接暂未就绪，正在自动重试")));
                            failure = Some(error);
                        }
                    }
                }
                WorkerMessage::Disconnect { reply } => {
                    preferred = None;
                    cancel_voice(voice.take(), link.as_ref(), &context, "disconnect");
                    link.take();
                    {
                        let mut state = lock(&context.state);
                        state.phase = ConnectionPhase::Disconnected;
                        state.battery_level = None;
                    }
                    let _ = reply.send(Ok(lock(&context.state).clone()));
                }
                WorkerMessage::Shutdown { ack } => {
                    cancel_voice(voice.take(), link.as_ref(), &context, "exit");
                    link.take();
                    if let Some(ack) = ack {
                        let _ = ack.send(());
                    }
                    break;
                }
                WorkerMessage::SystemSuspended => {
                    suspended = true;
                    cancel_voice(voice.take(), link.as_ref(), &context, "sleep");
                    link.take();
                    {
                        let mut state = lock(&context.state);
                        state.phase = ConnectionPhase::Suspended;
                        state.battery_level = None;
                    }
                }
                WorkerMessage::SystemResumed => {
                    suspended = false;
                    retry_at = Instant::now();
                    retry_count = 0;
                }
                WorkerMessage::WakeReconnect | WorkerMessage::RemoteDeviceArrived => {
                    // Never reset a failed attempt's quiet window in response to its own PnP events.
                    if retry_count == 0 {
                        retry_at = Instant::now();
                    }
                }
                WorkerMessage::ConnectionChanged {
                    connection_generation,
                    status,
                } if connection_generation == generation => {
                    if status == BluetoothConnectionStatus::Disconnected {
                        failure = Some(protocol("设备连接已断开，正在恢复"));
                    }
                }
                WorkerMessage::CallbackError {
                    connection_generation,
                    ..
                } if connection_generation == generation => {
                    failure = Some(protocol("设备通知读取失败"));
                }
                WorkerMessage::Control {
                    connection_generation,
                    bytes,
                } if connection_generation == generation => {
                    if let Some(active) = link.as_mut() {
                        let mut voice_event = false;
                        let result = (|| -> Result<(), PlatformError> {
                            let event = sticks3::parse_state(&bytes).map_err(protocol)?;
                            voice_event = matches!(
                                event.event.as_str(),
                                "record_start" | "record_stop" | "voice_key"
                            );
                            match event.event.as_str() {
                                "battery" => {
                                    if let Some(level) = sticks3::battery_level(&event) {
                                        lock(&context.state).battery_level = Some(level);
                                        gatt_note(format!("sticks3_battery result=updated generation={generation} level={level}"));
                                    } else {
                                        gatt_note(format!("sticks3_battery result=ignored reason=invalid generation={generation}"));
                                    }
                                }
                                "device_info" => {
                                    if event.hardware.as_deref() != Some("stick_s3")
                                        || event.button_gestures != Some(1)
                                    {
                                        return Err(protocol(
                                            "请升级到支持自定义按键的 StickS3 固件",
                                        ));
                                    }
                                    active.hid = event.hid_voice == Some(1);
                                    // Duplicate device_info must not invalidate a running recording.
                                    if active.rev == 0 {
                                        active.configure(&context)?;
                                    }
                                }
                                "buttons_ready"
                                    if active.rev > 0 && event.rev == Some(active.rev) =>
                                {
                                    active.ready = true;
                                    retry_count = 0;
                                    let mut state = lock(&context.state);
                                    state.phase = ConnectionPhase::Ready;
                                    state.last_error = None;
                                    gatt_note(format!("sticks3_buttons phase=ready generation={generation} rev={}", active.rev));
                                }
                                "button_gesture"
                                    if active.ready && event.rev == Some(active.rev) =>
                                {
                                    if let Some(slot) = event.slot {
                                        if let Some((button, trigger)) = sticks3::slot_target(slot)
                                        {
                                            let _ = context.mappings.sender().send(
                                                EngineMessage::FirmwareGesture(FiredGesture {
                                                    button,
                                                    trigger,
                                                }),
                                            );
                                        }
                                    }
                                }
                                "record_start" if active.ready && event.rev == Some(active.rev) => {
                                    if let Some(id) = event.session_id {
                                        if last_session == Some(id) {
                                            return Ok(());
                                        }
                                        let valid_slot =
                                            event.slot.filter(|slot| *slot < 6).is_some_and(
                                                |slot| active.map.as_bytes()[slot] == b'1',
                                            );
                                        if !valid_slot {
                                            return Err(protocol("record_slot_mismatch"));
                                        }
                                        last_session = Some(id);
                                        cancel_voice(
                                            voice.take(),
                                            Some(active),
                                            &context,
                                            "replaced",
                                        );
                                        audio_generation += 1;
                                        voice = Some(Voice::begin(
                                            id,
                                            audio_generation,
                                            active,
                                            &context,
                                        )?);
                                        let mut state = lock(&context.state);
                                        state.phase = ConnectionPhase::Streaming;
                                        state.voice_state = VoiceSessionState::Streaming;
                                    }
                                }
                                "record_stop" if event.rev == Some(active.rev) => {
                                    if let Some(voice) =
                                        voice.as_mut().filter(|v| Some(v.id()) == event.session_id)
                                    {
                                        // STOP precedes trailing Opus packets. END is the only normal drain trigger.
                                        voice.set_stop_deadline(Duration::from_secs(5));
                                    }
                                }
                                "voice_key" => {
                                    if let Some(current) = voice.as_mut() {
                                        if current.key_ack(&event)? {
                                            let finished = voice.take().unwrap();
                                            finish_voice(finished, active, &context);
                                        }
                                    }
                                }
                                _ => {}
                            }
                            Ok(())
                        })();
                        if let Err(error) = result {
                            if voice_event {
                                voice_failure = Some(error);
                            } else {
                                failure = Some(error);
                            }
                        }
                    }
                }
                WorkerMessage::Audio {
                    connection_generation,
                    bytes,
                } if connection_generation == generation => {
                    if let (Some(active), Some(current)) = (link.as_ref(), voice.as_mut()) {
                        let result = sticks3::parse_audio(&bytes)
                            .map_err(protocol)
                            .and_then(|frame| current.audio(frame, &context));
                        match result {
                            Ok(true) => {
                                lock(&context.state).phase = ConnectionPhase::Draining;
                                current.set_stop_deadline(Duration::from_secs(25));
                                active.ui("thinking");
                            }
                            Ok(false) => {}
                            Err(error) => voice_failure = Some(error),
                        }
                    }
                }
                _ => {} // stale callbacks, Xiaomi-only messages, optional telemetry
            }
        }
        if failure.is_none() && voice_failure.is_none() {
            if let Some(active) = link.as_mut() {
                if !active.ready && Instant::now() >= active.deadline {
                    failure = Some(protocol("设备初始化超时"));
                }
                if active.ready
                    && voice.is_none()
                    && active.map != sticks3::button_map(&context.mappings.mappings())
                {
                    if let Err(error) = active.configure(&context) {
                        failure = Some(error);
                    }
                }
                if let Some(current) = voice.as_mut() {
                    match current.poll(active, &context) {
                        Ok(true) => {
                            let finished = voice.take().unwrap();
                            finish_voice(finished, active, &context);
                        }
                        Ok(false) => {}
                        Err(error) => voice_failure = Some(error),
                    }
                }
            }
        }
        if let Some(error) = voice_failure {
            cancel_voice(voice.take(), link.as_ref(), &context, "voice_failed");
            if let Some(active) = link.as_ref() {
                let _ = active.write(serde_json::json!({"event":"record_cancel"}));
                active.ui("ready");
            }
            recover_voice_snapshot(&mut lock(&context.state), error.to_string());
            gatt_note(format!("sticks3_voice result=failed connection_preserved=true generation={generation} error={error}"));
        }
        if let Some(error) = failure {
            gatt_note(format!(
                "sticks3_runtime result=failed generation={generation} error={error}"
            ));
            cancel_voice(voice.take(), link.as_ref(), &context, "failure");
            link.take();
            let mut state = lock(&context.state);
            state.battery_level = None;
            state.last_error = Some(error.to_string());
            state.phase = if preferred.is_some() {
                ConnectionPhase::Reconnecting
            } else {
                ConnectionPhase::Failed
            };
            retry_count = retry_count.saturating_add(1);
            state.reconnect_attempt = retry_count;
            retry_at = Instant::now()
                + Duration::from_secs((2u64 << retry_count.min(4).saturating_sub(1)).min(30));
        }
        if link.is_none() && !suspended && Instant::now() >= retry_at {
            if let Some(id) = preferred.as_ref() {
                generation += 1;
                lock(&context.state).generation = generation;
                last_session = None;
                match Link::open(id, generation, &context) {
                    Ok(value) => link = Some(value),
                    Err(_) => {
                        retry_count = retry_count.saturating_add(1);
                        lock(&context.state).reconnect_attempt = retry_count;
                        retry_at = Instant::now()
                            + Duration::from_secs((2u64 << retry_count.min(4)).min(30));
                        gatt_note(format!("sticks3_connect phase=retry attempt={retry_count} generation={generation}"));
                    }
                }
            }
        }
    }
    cancel_voice(voice.take(), link.as_ref(), &context, "worker_closed");
}

fn recover_voice_snapshot(state: &mut ConnectionSnapshot, error: String) {
    if matches!(
        state.phase,
        ConnectionPhase::Streaming | ConnectionPhase::Draining | ConnectionPhase::Ready
    ) {
        state.phase = ConnectionPhase::Ready;
    }
    state.voice_state = VoiceSessionState::Idle;
    state.last_error = Some(error);
}

fn finish_voice(voice: Voice, link: &Link, context: &Context) {
    let (id, samples, started) = match &voice {
        Voice::Api(v) => (v.id, v.samples, v.started),
        Voice::Ime(v) => {
            if let Some(chord) = &v.software {
                let _ = context.input.release(chord);
            }
            (v.id, v.samples, v.started)
        }
    };
    let now = chrono::Utc::now().timestamp_millis();
    context.capture.finish_audio(now);
    context.capture.finish_session(now);
    context.usage.record_voice_session(samples);
    context.usage.finish_voice_session();
    let mut state = lock(&context.state);
    state.phase = ConnectionPhase::Ready;
    state.voice_state = VoiceSessionState::Idle;
    state.decoded_samples = state.decoded_samples.saturating_add(samples);
    state.last_error = None;
    drop(state);
    link.ui("ready");
    gatt_note(format!(
        "sticks3_voice phase=closed result=completed session={id} samples={samples} duration_ms={}",
        started.elapsed().as_millis()
    ));
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn a_failed_voice_session_preserves_the_bluetooth_connection_and_generation() {
        let mut state = ConnectionSnapshot::default();
        state.phase = ConnectionPhase::Streaming;
        state.voice_state = VoiceSessionState::Streaming;
        state.generation = 73;
        state.remote_name = Some("VS-1234".into());
        recover_voice_snapshot(&mut state, "keyboard_not_ready".into());
        assert_eq!(state.phase, ConnectionPhase::Ready);
        assert_eq!(state.voice_state, VoiceSessionState::Idle);
        assert_eq!(state.generation, 73);
        assert_eq!(state.remote_name.as_deref(), Some("VS-1234"));
        assert_eq!(state.last_error.as_deref(), Some("keyboard_not_ready"));
    }
    #[test]
    fn opus_round_trip_matches_sticks3_sixty_ms_mono_frames() {
        let mut encoder =
            opus::Encoder::new(16_000, Channels::Mono, opus::Application::Voip).unwrap();
        let input: Vec<i16> = (0..960)
            .map(|n| ((n as f32 * 0.17).sin() * 8_000.0) as i16)
            .collect();
        let mut packet = [0u8; 1275];
        let len = encoder.encode(&input, &mut packet).unwrap();
        let mut decoder = Decoder::new(16_000, Channels::Mono).unwrap();
        let mut output = [0i16; 1920];
        assert_eq!(
            decoder.decode(&packet[..len], &mut output, false).unwrap(),
            960
        );
        assert!(output.iter().any(|sample| *sample != 0));
    }
}
