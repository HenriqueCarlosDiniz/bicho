#ifndef LIVE_SESSION_H
#define LIVE_SESSION_H

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "live_protocol.h"
#include "live_transport.h"

// =============================================================================
// Sessão de conversa em tempo real com a API Live do Gemini.
// Thread de recepção: conecta, envia o setup e entrega áudio/eventos do servidor.
// Thread de envio: transmite o microfone em blocos de ~100 ms.
// O loop principal só chama pushMicAudio/popAudio/popEvent (nada bloqueia a animação).
// =============================================================================

enum LiveEventType {
    LIVE_EVENT_READY = 0,      // Setup aceito: já pode conversar
    LIVE_EVENT_INPUT_TEXT,     // Pedaço da transcrição do humano
    LIVE_EVENT_OUTPUT_TEXT,    // Pedaço da transcrição da fala do Bicho
    LIVE_EVENT_TURN_COMPLETE,  // O Bicho terminou de responder
    LIVE_EVENT_INTERRUPTED,    // O humano interrompeu: descartar o áudio pendente
    LIVE_EVENT_CLOSED          // Sessão encerrada (text = motivo/erro)
};

struct LiveEvent {
    LiveEventType type;
    std::string text;
};

class LiveSession {
public:
    LiveSession();
    ~LiveSession();

    // Abre a sessão em background; initial_pcm16k (ex.: a frase que acordou o Bicho) é enviada logo após o setup
    bool start(const std::string& api_key, const LiveSetupOptions& options, const std::vector<int16_t>& initial_pcm16k);
    void stop();

    void pushMicAudio(const int16_t* samples, size_t count);
    bool popAudio(std::vector<int16_t>& out_pcm24k);
    bool popEvent(LiveEvent& out_event);

    bool isRunning() const { return running_; }
    bool isReady() const { return ready_; }

private:
    void receiverLoop(std::string api_key, LiveSetupOptions options);
    void senderLoop();
    void pushEvent(LiveEventType type, const std::string& text);

    std::unique_ptr<LiveTransport> transport_;
    std::thread receiver_thread_;
    std::thread sender_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> ready_;
    std::atomic<bool> stop_requested_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<int16_t> mic_pending_;
    std::vector<int16_t> audio_out_;
    std::deque<LiveEvent> events_;
};

#endif // LIVE_SESSION_H
