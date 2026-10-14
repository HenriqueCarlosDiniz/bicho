#ifndef VOICE_SYNTH_WORKER_H
#define VOICE_SYNTH_WORKER_H

#include <stdint.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "hal_sensors.h"

class OracleBrain;

// =============================================================================
// Constantes do Worker de Síntese de Voz (Padrão UPPER_CASE)
// =============================================================================
constexpr uint32_t VOICE_SYNTH_MAX_WAIT_MS = 9000U;       // Espera máxima pela voz neural (Gemini TTS leva ~6 s) antes do fallback procedural
constexpr uint32_t VOICE_SYNTH_TASK_STACK_BYTES = 20480U; // Pilha no ESP32-S3 (TLS do Gemini TTS + SD)

// =============================================================================
// Classe VoiceSynthWorker: síntese de voz (cache SD / edge-tts / Gemini TTS) fora do loop de 60 FPS
// =============================================================================
class VoiceSynthWorker {
public:
    // Pós-processamento aplicado ao WAV na própria thread do worker (DSP emocional Sci-Fi)
    using WavDspFn = void (*)(std::vector<uint8_t>& target_wav, PetEmotionState emotion_state);

    VoiceSynthWorker();
    ~VoiceSynthWorker();

    // Cria a thread persistente do worker (Core 0 no ESP32-S3)
    void start(const OracleBrain* brain, WavDspFn dsp_fn);

    // Agenda a síntese da frase; um pedido mais novo substitui o anterior ainda não iniciado
    uint32_t request(const char* text_utf8, PetEmotionState emotion_state);

    // Invalida o pedido em andamento (o resultado, quando chegar, será descartado)
    void cancel();

    // Retorna true quando o job 'job_id' terminou; out_ok indica se há WAV válido em out_wav
    bool poll(uint32_t job_id, bool& out_ok, std::vector<uint8_t>& out_wav);

private:
    void runLoop();
    bool synthesizeWithCache(const std::string& text_utf8, std::vector<uint8_t>& out_wav) const;

    const OracleBrain* oracle_brain;
    WavDspFn dsp_fn;
    std::thread worker_thread;
    std::mutex job_mutex;
    std::condition_variable job_cv;
    bool stop_requested;
    bool has_request;
    std::string request_text;
    PetEmotionState request_emotion;
    uint32_t request_id;
    uint32_t next_job_id;
    uint32_t done_id;
    bool done_ok;
    std::vector<uint8_t> done_wav;
};

#endif // VOICE_SYNTH_WORKER_H
