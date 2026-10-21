#include "ime_voice_win.h"
#include "log.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <msctf.h>
#include <wrl/client.h>
#include <opus.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <cctype>

namespace voicestick {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
namespace {
std::string Utf8(const wchar_t* text) {
    if (!text) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), n, nullptr, nullptr);
    if (!result.empty()) result.pop_back();
    return result;
}
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring result(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n);
    return result;
}
void Check(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(message);
}
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
std::string EndpointName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    Check(device->OpenPropertyStore(STGM_READ, &properties), "无法读取音频设备名称");
    PROPVARIANT value{};
    Check(properties->GetValue(PKEY_Device_FriendlyName, &value), "无法读取音频设备名称");
    auto name = value.vt == VT_LPWSTR ? Utf8(value.pwszVal) : std::string();
    PropVariantClear(&value);
    return name;
}
bool IsCable(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)tolower(c); });
    return name.find("vb-audio virtual cable") != std::string::npos ||
           name.find("cable input") != std::string::npos;
}
constexpr GUID kDoubaoClsid = {0x9d2b2e2b,0x3c93,0x4d2f,{0x9d,0x35,0x6e,0xeb,0x85,0xf0,0xd2,0xb0}};
constexpr GUID kDoubaoProfile = {0x2b4d4b3a,0x4d4f,0x4c0a,{0x8e,0x66,0x7f,0x77,0x1a,0x2b,0x9c,0x10}};
}

std::vector<ImeAudioEndpoint> ListImeAudioEndpoints() {
    Apartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)), "无法枚举音频设备");
    ComPtr<IMMDeviceCollection> devices;
    Check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices), "无法枚举音频设备");
    UINT count = 0;
    Check(devices->GetCount(&count), "无法读取音频设备列表");
    std::vector<ImeAudioEndpoint> result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        auto name = EndpointName(device.Get());
        if (!IsCable(name)) continue;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            result.push_back({Utf8(id), name});
            CoTaskMemFree(id);
        }
    }
    return result;
}

bool ActivateDoubaoIme(std::string* error) {
    // TSF session activation must run in an STA; the audio worker is an MTA.
    bool activated = false;
    std::thread sta([&] {
        auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return;
        {
            ComPtr<ITfInputProcessorProfileMgr> manager;
            hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&manager));
            if (SUCCEEDED(hr)) {
                TF_INPUTPROCESSORPROFILE active{};
                hr = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active);
                if (SUCCEEDED(hr) && active.clsid == kDoubaoClsid && active.guidProfile == kDoubaoProfile) {
                    activated = true;
                } else {
                    hr = manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804,
                        kDoubaoClsid, kDoubaoProfile, nullptr, TF_IPPMF_FORSESSION);
                    if (SUCCEEDED(hr)) {
                        // The foreground application's input context rebinds asynchronously.
                        for (int i = 0; i < 20; ++i) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(25));
                            if (SUCCEEDED(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active)) &&
                                active.clsid == kDoubaoClsid && active.guidProfile == kDoubaoProfile) {
                                activated = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        CoUninitialize();
    });
    sta.join();
    if (!activated && error) *error = "无法启用豆包输入法，请先安装并在目标输入框切换到豆包";
    return activated;
}

bool IsDoubaoVoiceWindowVisible() {
    bool visible = false;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        wchar_t name[96]{};
        GetClassNameW(hwnd, name, 96);
        if (wcscmp(name, L"OimeVoiceWaveWindow") == 0 && IsWindowVisible(hwnd)) {
            *reinterpret_cast<bool*>(data) = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&visible));
    return visible;
}

bool SendRightAlt(bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = 0x38;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | (down ? 0 : KEYEVENTF_KEYUP);
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool SendVoiceShortcut(std::string_view shortcut, bool down) {
    if (shortcut == "right_alt") return SendRightAlt(down);
    if (shortcut != "left_ctrl_win") return false;
    auto send = [](WORD scan, bool extended, bool up) {
        INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wScan = scan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
        return SendInput(1, &input, sizeof(input)) == 1;
    };
    // Keep modifier edges separate, matching a physical chord. Always attempt
    // both releases, even if the first release fails.
    if (down) {
        if (!send(0x1D, false, false)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (send(0x5B, true, false)) return true;
        send(0x1D, false, true);
        return false;
    }
    bool win = send(0x5B, true, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    bool ctrl = send(0x1D, false, true);
    return win && ctrl;
}

struct ImeVoiceWin::Impl {
    struct Message {
        enum Kind { Begin, Audio, State, End, Cancel, Quit } kind;
        std::uint64_t epoch = 0;
        AppConfig config;
        std::string device;
        std::uint32_t session = 0;
        AudioFrame frame;
        StateEvent state;
        std::function<void(std::string, std::uint32_t, std::string)> completion;
    };
    Control control;
    Dispatch dispatch;
    std::shared_ptr<std::atomic_uint64_t> epoch = std::make_shared<std::atomic_uint64_t>(0);
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Message> messages;
    std::thread worker;
    bool overflow = false;
    std::uint64_t active_epoch = 0;
    std::string device;
    std::string shortcut;
    std::uint32_t session = 0;
    bool active = false, finishing = false, key_ready = false, awaiting_up = false, mic_ready = false;
    Clock::time_point started, last_audio, last_renew, up_requested;
    std::optional<std::uint32_t> last_seq;
    std::function<void(std::string, std::uint32_t, std::string)> completion;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder{nullptr, opus_decoder_destroy};
    UINT32 buffer_size = 0;
    std::deque<opus_int16> samples;

    Impl(Control c, Dispatch d) : control(std::move(c)), dispatch(std::move(d)) {
        // Start only after all state and COM holders have been initialized.
        worker = std::thread([this] { Run(); });
    }
    ~Impl() {
        epoch->fetch_add(1);
        { std::lock_guard lock(mutex); messages.clear(); messages.push_back({Message::Quit}); }
        cv.notify_one();
        worker.join();
    }
    void Queue(Message message) {
        std::lock_guard lock(mutex);
        if (messages.size() >= 128 && message.kind == Message::Audio) overflow = true;
        else messages.push_back(std::move(message));
        cv.notify_one();
    }
    void Key(bool down, bool cancel = false) {
        try {
            control(device, "{\"event\":\"voice_key\",\"session_id\":" + std::to_string(session) +
                ",\"down\":" + (down ? "1" : "0") + ",\"cancel\":" + (cancel ? "1" : "0") +
                ",\"shortcut\":" + (shortcut == "left_ctrl_win" ? "2" : "1") + "}");
        } catch (...) { LogApp("ime_voice control=unavailable"); }
    }
    void Close(bool notify, const std::string& error = {}) {
        if (!active) return;
        // Also retry UP on failure after a previous UP request was lost.
        if (!awaiting_up || !error.empty()) Key(false, true);
        if (client) client->Stop();
        render.Reset(); client.Reset(); decoder.reset(); samples.clear(); last_seq.reset();
        active = false;
        LogApp("ime_voice phase=closed result=" + std::string(error.empty() ? "completed" : "failed"));
        if (notify && completion) {
            dispatch([guard = epoch, token = active_epoch, callback = completion, d = device, s = session, error] {
                if (guard->load() == token) callback(d, s, error);
            });
        }
        completion = {};
    }
    void Open(const Message& message) {
        Close(false);
        active_epoch = message.epoch; device = message.device; session = message.session;
        shortcut = message.config.ime_voice_shortcut;
        completion = message.completion; active = true;
        if (shortcut != "left_ctrl_win" && shortcut != "right_alt")
            throw std::runtime_error("不支持当前语音快捷键，请在设置中重新选择");
        finishing = key_ready = awaiting_up = mic_ready = false;
        started = last_audio = last_renew = Clock::now();
        auto endpoints = ListImeAudioEndpoints();
        auto selected = std::find_if(endpoints.begin(), endpoints.end(), [&](const auto& endpoint) {
            return endpoint.id == message.config.ime_audio_endpoint;
        });
        if (selected == endpoints.end()) throw std::runtime_error("请安装 VB-CABLE，并在 VoiceStick 设置中选择虚拟麦克风通道");
        ComPtr<IMMDeviceEnumerator> enumerator;
        Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)), "无法访问音频设备");
        ComPtr<IMMDevice> endpoint;
        Check(enumerator->GetDevice(Wide(selected->id).c_str(), &endpoint), "所选音频设备已不可用");
        Check(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "无法打开虚拟麦克风通道");
        ComPtr<IAudioEndpointVolume> volume;
        Check(endpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume), "无法读取虚拟麦克风音量");
        Check(volume->SetMute(FALSE, nullptr), "无法取消虚拟麦克风静音");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 1; format.nSamplesPerSec = 16000;
        format.wBitsPerSample = 16; format.nBlockAlign = 2; format.nAvgBytesPerSec = 32000;
        Check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, &format, nullptr), "虚拟麦克风通道初始化失败");
        Check(client->GetBufferSize(&buffer_size), "无法读取音频缓冲区");
        Check(client->GetService(IID_PPV_ARGS(&render)), "无法取得音频输出通道");
        ComPtr<ISimpleAudioVolume> session_volume;
        Check(client->GetService(IID_PPV_ARGS(&session_volume)), "无法取得音频会话音量");
        Check(session_volume->SetMute(FALSE, nullptr), "无法取消语音会话静音");
        Check(session_volume->SetMasterVolume(1.0f, nullptr), "无法设置语音会话音量");
        int opus_error = 0;
        decoder.reset(opus_decoder_create(16000, 1, &opus_error));
        if (!decoder || opus_error != OPUS_OK) throw std::runtime_error("Opus 解码器初始化失败");
        std::string error;
        if (!ActivateDoubaoIme(&error)) throw std::runtime_error(error);
        if (IsDoubaoVoiceWindowVisible()) throw std::runtime_error("豆包正在录音，请先结束当前语音再使用设备");
        if (epoch->load() != active_epoch) { Close(false); return; }
        Check(client->Start(), "无法启动虚拟麦克风通道");
        started = last_audio = last_renew = Clock::now();
        Key(true);
        LogApp("ime_voice phase=waiting_for_hardware_key");
    }
    void Decode(const AudioFrame& frame) {
        if (frame.session_id != session || finishing) return;
        if (last_seq && frame.seq == *last_seq) return;
        if (last_seq && frame.seq != *last_seq + 1) {
            // Duplicate/stale callbacks must not replay speech. Do not silently
            // compress a gap in a live microphone stream.
            throw std::runtime_error("蓝牙音频丢包，本次豆包语音已停止");
        }
        last_seq = frame.seq;
        last_audio = Clock::now();
        if (frame.payload.empty()) return;
        opus_int16 pcm[1920];
        int n = opus_decode(decoder.get(), frame.payload.data(), (opus_int32)frame.payload.size(), pcm, 1920, 0);
        if (n < 0) throw std::runtime_error("设备音频解码失败");
        if (samples.size() + n > 64000) throw std::runtime_error("豆包语音音频积压，请重新录音");
        samples.insert(samples.end(), pcm, pcm + n);
    }
    void Tick() {
        auto now = Clock::now();
        if (epoch->load() != active_epoch) { Close(false); return; }
        if (awaiting_up) {
            if (now - up_requested > std::chrono::seconds(2))
                throw std::runtime_error("设备未确认释放语音键，已停止音频；请检查设备连接");
            return;
        }
        if (now - last_renew > std::chrono::milliseconds(500)) { Key(true); last_renew = now; }
        if (!key_ready && now - started > std::chrono::seconds(2))
            throw std::runtime_error("设备蓝牙键盘通道未就绪，请检查 Windows 中的 VS 设备键盘连接；已配对时请重新连接设备后重试");
        if (key_ready && !mic_ready) {
            mic_ready = IsDoubaoVoiceWindowVisible();
            if (mic_ready) LogApp("ime_voice phase=microphone_ready");
            else if (now - started > std::chrono::seconds(3))
                throw std::runtime_error("豆包未开始语音，请检查语音快捷键设置、网络和输入法状态");
        }
        if (now - last_audio > std::chrono::seconds(10)) throw std::runtime_error("设备音频接收超时");
        UINT32 padding = 0;
        Check(client->GetCurrentPadding(&padding), "音频设备已断开");
        if (finishing && mic_ready && samples.empty() && padding == 0) {
            // Hold the hardware key through the final WASAPI padding.
            Key(false); awaiting_up = true; up_requested = now;
            LogApp("ime_voice phase=waiting_for_key_release");
            return;
        }
        UINT32 count = mic_ready ? (UINT32)(std::min)(samples.size(), (size_t)(buffer_size - padding)) : 0;
        if (!count) return;
        BYTE* target = nullptr;
        Check(render->GetBuffer(count, &target), "无法写入虚拟麦克风");
        auto pcm = reinterpret_cast<opus_int16*>(target);
        for (UINT32 i = 0; i < count; ++i) { pcm[i] = samples.front(); samples.pop_front(); }
        Check(render->ReleaseBuffer(count, 0), "虚拟麦克风输出失败");
    }
    void Run() {
        Apartment apartment;
        for (;;) {
            Message message{Message::Cancel};
            bool have_message = false, queue_overflow = false;
            {
                std::unique_lock lock(mutex);
                if (active) cv.wait_for(lock, std::chrono::milliseconds(5), [&] { return !messages.empty(); });
                else cv.wait(lock, [&] { return !messages.empty(); });
                if (!messages.empty()) { message = std::move(messages.front()); messages.pop_front(); have_message = true; }
                queue_overflow = std::exchange(overflow, false);
            }
            try {
                if (have_message) {
                    if (message.kind == Message::Quit) { Close(false); return; }
                    if (message.epoch != epoch->load()) continue;
                    if (message.kind == Message::Cancel) Close(false);
                    else if (message.kind == Message::Begin) Open(message);
                    else if (active && message.kind == Message::Audio) Decode(message.frame);
                    else if (active && message.kind == Message::End) finishing = true;
                    else if (active && message.kind == Message::State && message.device == device &&
                             message.state.session_id == session && message.state.event == "voice_key") {
                        if (message.state.key_ok != 1) throw std::runtime_error("设备未能按下语音快捷键，请检查蓝牙键盘连接；已配对时请重新连接设备后重试");
                        if (message.state.key_down == 1) key_ready = true;
                        else if (awaiting_up) Close(true);
                        else throw std::runtime_error("设备提前释放了语音键，本次录音已停止");
                    }
                }
                if (queue_overflow && active) throw std::runtime_error("音频接收队列已满，本次语音已停止");
                if (active) Tick();
            } catch (const std::exception& error) { Close(true, error.what()); }
        }
    }
};

ImeVoiceWin::ImeVoiceWin(Control control, Dispatch dispatch)
    : impl_(std::make_unique<Impl>(std::move(control), std::move(dispatch))) {}
ImeVoiceWin::~ImeVoiceWin() = default;
void ImeVoiceWin::Begin(const AppConfig& config, const std::string& device, std::uint32_t session) {
    Impl::Message message{Impl::Message::Begin};
    message.epoch = impl_->epoch->fetch_add(1) + 1;
    message.config = config; message.device = device; message.session = session; message.completion = on_complete;
    impl_->Queue(std::move(message));
}
void ImeVoiceWin::Push(const AudioFrame& frame) {
    Impl::Message message{Impl::Message::Audio}; message.epoch = impl_->epoch->load(); message.frame = frame;
    impl_->Queue(std::move(message));
}
void ImeVoiceWin::HandleState(const std::string& device, const StateEvent& state) {
    if (state.event != "voice_key") return;
    Impl::Message message{Impl::Message::State}; message.epoch = impl_->epoch->load();
    message.device = device; message.state = state; impl_->Queue(std::move(message));
}
void ImeVoiceWin::Finish() {
    Impl::Message message{Impl::Message::End}; message.epoch = impl_->epoch->load(); impl_->Queue(std::move(message));
}
void ImeVoiceWin::Cancel() {
    Impl::Message message{Impl::Message::Cancel}; message.epoch = impl_->epoch->fetch_add(1) + 1;
    impl_->Queue(std::move(message));
}
} // namespace voicestick
