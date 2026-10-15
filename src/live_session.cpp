#include "live_session.h"
#include <algorithm>
#include <chrono>

namespace {
constexpr size_t MIC_CHUNK_SAMPLES = LIVE_INPUT_SAMPLE_RATE / 10;    // 100 ms por mensagem
constexpr size_t MIC_PENDING_MAX = LIVE_INPUT_SAMPLE_RATE * 4;      // Descarta atraso acima de 4 s
constexpr size_t AUDIO_OUT_MAX = LIVE_OUTPUT_SAMPLE_RATE * 60;      // Teto de segurança da fila de saída
} // namespace

LiveSession::LiveSession() : running_(false), ready_(false), stop_requested_(false) {}

LiveSession::~LiveSession() {
    stop();
}

bool LiveSession::start(const std::string& api_key, const LiveSetupOptions& options,
                        const std::vector<int16_t>& initial_pcm16k) {
    if (running_) {
        return false;
    }
    stop(); // Garante threads antigas finalizadas
    transport_ = createLiveTransport();
    if (!transport_ || api_key.empty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mic_pending_ = initial_pcm16k;
        audio_out_.clear();
        events_.clear();
    }
    stop_requested_ = false;
    ready_ = false;
    running_ = true;
    receiver_thread_ = std::thread(&LiveSession::receiverLoop, this, api_key, options);
    sender_thread_ = std::thread(&LiveSession::senderLoop, this);
    return true;
}

void LiveSession::stop() {
    stop_requested_ = true;
    cv_.notify_all();
    if (transport_) {
        transport_->close(); // Desbloqueia o receive() da outra thread
    }
    if (receiver_thread_.joinable()) receiver_thread_.join();
    if (sender_thread_.joinable()) sender_thread_.join();
    transport_.reset();
    running_ = false;
    ready_ = false;
}

void LiveSession::pushMicAudio(const int16_t* samples, size_t count) {
    if (!running_ || samples == nullptr || count == 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mic_pending_.insert(mic_pending_.end(), samples, samples + count);
        if (mic_pending_.size() > MIC_PENDING_MAX) {
            mic_pending_.erase(mic_pending_.begin(), mic_pending_.end() - MIC_PENDING_MAX);
        }
    }
    cv_.notify_one();
}

bool LiveSession::popAudio(std::vector<int16_t>& out_pcm24k) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audio_out_.empty()) {
        return false;
    }
    out_pcm24k.swap(audio_out_);
    audio_out_.clear();
    return true;
}

bool LiveSession::popEvent(LiveEvent& out_event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) {
        return false;
    }
    out_event = events_.front();
    events_.pop_front();
    return true;
}

void LiveSession::pushEvent(LiveEventType type, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back({type, text});
}

void LiveSession::receiverLoop(std::string api_key, LiveSetupOptions options) {
    // A chave vai num header (não na URL), como nas chamadas HTTP do Oráculo
    const std::string headers = "x-goog-api-key: " + api_key + "\r\n";
    if (!transport_->connect(LIVE_API_HOST, LIVE_API_PATH, headers)) {
        pushEvent(LIVE_EVENT_CLOSED, "falha ao conectar: " + transport_->lastError());
        running_ = false;
        cv_.notify_all();
        return;
    }
    if (!transport_->sendText(buildLiveSetupMessage(options))) {
        pushEvent(LIVE_EVENT_CLOSED, "falha ao enviar setup: " + transport_->lastError());
        running_ = false;
        cv_.notify_all();
        return;
    }

    std::string text;
    LiveServerMessage msg;
    std::string close_reason = "encerrada";
    while (!stop_requested_) {
        if (!transport_->receiveText(text)) {
            close_reason = stop_requested_ ? "encerrada" : transport_->lastError();
            break;
        }
        if (!parseLiveServerMessage(text, msg)) {
            continue;
        }
        if (!msg.error.empty()) {
            close_reason = msg.error;
            break;
        }
        if (msg.setup_complete && !ready_) {
            ready_ = true;
            pushEvent(LIVE_EVENT_READY, "");
            cv_.notify_all();
        }
        if (!msg.input_transcript.empty()) pushEvent(LIVE_EVENT_INPUT_TEXT, msg.input_transcript);
        if (!msg.output_transcript.empty()) pushEvent(LIVE_EVENT_OUTPUT_TEXT, msg.output_transcript);
        if (!msg.audio.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (audio_out_.size() + msg.audio.size() <= AUDIO_OUT_MAX) {
                audio_out_.insert(audio_out_.end(), msg.audio.begin(), msg.audio.end());
            }
        }
        if (msg.interrupted) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_out_.clear();
            }
            pushEvent(LIVE_EVENT_INTERRUPTED, "");
        }
        if (msg.turn_complete) pushEvent(LIVE_EVENT_TURN_COMPLETE, "");
        if (msg.go_away) {
            close_reason = "servidor pediu para encerrar (goAway)";
            break;
        }
    }
    pushEvent(LIVE_EVENT_CLOSED, close_reason);
    running_ = false;
    cv_.notify_all();
}

void LiveSession::senderLoop() {
    std::vector<int16_t> chunk;
    while (!stop_requested_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return stop_requested_ || !running_ || (ready_ && mic_pending_.size() >= MIC_CHUNK_SAMPLES);
            });
            if (stop_requested_ || !running_) {
                break;
            }
            if (!ready_ || mic_pending_.empty()) {
                continue;
            }
            size_t n = std::min(mic_pending_.size(), MIC_CHUNK_SAMPLES * 4); // Recupera atraso com blocos maiores
            chunk.assign(mic_pending_.begin(), mic_pending_.begin() + static_cast<std::ptrdiff_t>(n));
            mic_pending_.erase(mic_pending_.begin(), mic_pending_.begin() + static_cast<std::ptrdiff_t>(n));
        }
        if (!transport_->sendText(buildLiveAudioMessage(chunk.data(), chunk.size()))) {
            break; // A thread de recepção reporta o fechamento
        }
    }
}
