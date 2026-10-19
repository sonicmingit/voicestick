#include "ime_voice_win.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

using namespace voicestick;
int main() {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::function<void()>> posted;
    int downs = 0, completed = 0;
    ImeVoiceWin session([&](const std::string&, const std::string& command) {
        if (command.find("\"down\":1") != std::string::npos) ++downs;
    }, [&](std::function<void()> action) {
        std::lock_guard lock(mutex); posted.push_back(std::move(action)); cv.notify_one();
    });
    session.on_complete = [&](std::string device, std::uint32_t id, std::string error) {
        assert(device == "TEST" && id == 1 && !error.empty()); ++completed;
    };
    auto config = AppConfig::Defaults();
    config.ime_audio_endpoint = "nonexistent-voicestick-test-endpoint";
    session.Begin(config, "TEST", 1);
    {
        std::unique_lock lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(5), [&] { return posted.size() == 1; }));
    }
    assert(downs == 0); // Missing endpoint fails before starting dictation.
    session.Cancel();
    posted.front()();
    assert(completed == 0); // A queued result cannot revive a cancelled session.
    posted.clear();
    session.Begin(config, "TEST", 1);
    {
        std::unique_lock lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(5), [&] { return posted.size() == 1; }));
    }
    posted.front()();
    assert(completed == 1 && downs == 0);
}
