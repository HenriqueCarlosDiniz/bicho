#include "voice_synth_worker.h"
#include "oracle_brain.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#if defined(ARDUINO)
#include <SD.h>
#include <esp_pthread.h>
#endif

VoiceSynthWorker::VoiceSynthWorker()
    : oracle_brain(nullptr),
      dsp_fn(nullptr),
      stop_requested(false),
      has_request(false),
      request_emotion(EMOTION_IDLE),
      request_id(0),
      next_job_id(1),
      done_id(0),
      done_ok(false) {
}

VoiceSynthWorker::~VoiceSynthWorker() {
    {
        std::lock_guard<std::mutex> lock(job_mutex);
        stop_requested = true;
    }
    job_cv.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void VoiceSynthWorker::start(const OracleBrain* brain, WavDspFn dsp) {
    if (worker_thread.joinable()) {
        return;
    }
    oracle_brain = brain;
    dsp_fn = dsp;

#if defined(ARDUINO)
    // std::thread no ESP32 usa pthreads sobre FreeRTOS: define pilha e núcleo antes de criar a thread
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = VOICE_SYNTH_TASK_STACK_BYTES;
    cfg.prio = 1;
    cfg.pin_to_core = 0;
    cfg.thread_name = "VoiceSynthTask";
    esp_pthread_set_cfg(&cfg);
#endif
    worker_thread = std::thread([this]() { runLoop(); });
#if defined(ARDUINO)
    // Restaura a configuração padrão para não afetar outras std::thread criadas depois
    esp_pthread_cfg_t default_cfg = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&default_cfg);
#endif
}

uint32_t VoiceSynthWorker::request(const char* text_utf8, PetEmotionState emotion_state) {
    uint32_t job_id;
    {
        std::lock_guard<std::mutex> lock(job_mutex);
        job_id = next_job_id++;
        request_text = (text_utf8 != nullptr) ? text_utf8 : "";
        request_emotion = emotion_state;
        request_id = job_id;
        has_request = true;
    }
    job_cv.notify_one();
    return job_id;
}

void VoiceSynthWorker::cancel() {
    std::lock_guard<std::mutex> lock(job_mutex);
    has_request = false;
    request_id = 0;
}

bool VoiceSynthWorker::poll(uint32_t job_id, bool& out_ok, std::vector<uint8_t>& out_wav) {
    std::lock_guard<std::mutex> lock(job_mutex);
    if (job_id == 0 || done_id != job_id) {
        return false;
    }
    out_ok = done_ok;
    out_wav.swap(done_wav);
    done_wav.clear();
    done_id = 0;
    return true;
}

void VoiceSynthWorker::runLoop() {
    for (;;) {
        std::string text;
        PetEmotionState emotion;
        uint32_t job_id;
        {
            std::unique_lock<std::mutex> lock(job_mutex);
            job_cv.wait(lock, [this]() { return stop_requested || has_request; });
            if (stop_requested) {
                return;
            }
            text = request_text;
            emotion = request_emotion;
            job_id = request_id;
            has_request = false;
        }

        std::vector<uint8_t> wav;
        uint32_t t0 = M5.millis();
        bool ok = synthesizeWithCache(text, wav);
        uint32_t t1 = M5.millis();
        if (ok && dsp_fn != nullptr) {
            dsp_fn(wav, emotion);
        }
        M5_LOGI("[SD-TTS] Voz pronta: sintese %u ms + DSP %u ms (%u bytes).",
                static_cast<unsigned int>(t1 - t0), static_cast<unsigned int>(M5.millis() - t1),
                static_cast<unsigned int>(wav.size()));

        std::lock_guard<std::mutex> lock(job_mutex);
        // Descarta resultados de pedidos cancelados ou substituídos por uma fala mais nova
        if (job_id == request_id && job_id != 0) {
            done_id = job_id;
            done_ok = ok;
            done_wav.swap(wav);
        }
    }
}

bool VoiceSynthWorker::synthesizeWithCache(const std::string& text_utf8, std::vector<uint8_t>& out_wav) const {
    if (text_utf8.empty() || oracle_brain == nullptr) {
        return false;
    }

    // 1. Hash determinístico FNV-1a 32-bit da frase para o Cache de Voz Neural 24kHz no Cartão SD
    uint32_t fnv_hash = 2166136261U;
    for (char c : text_utf8) {
        fnv_hash ^= static_cast<uint8_t>(c);
        fnv_hash *= 16777619U;
    }

    char cache_path[128];
    std::snprintf(cache_path, sizeof(cache_path), "%s/tts_%08x.wav", SD_TTS_CACHE_DIR, static_cast<unsigned int>(fnv_hash));

    bool loaded_raw_wav = false;

#if defined(ARDUINO)
    // Passo 1 (Caminho 3): WAV 24kHz já em cache no microSD (100% offline)
    if (oracle_brain->isSdStorageReady() && SD.exists(cache_path)) {
        File cached_wav = SD.open(cache_path, FILE_READ);
        if (cached_wav && cached_wav.size() > 44) {
            size_t fsize = cached_wav.size();
            out_wav.resize(fsize);
            cached_wav.read(out_wav.data(), fsize);
            loaded_raw_wav = true;
            M5_LOGI("[SD-TTS] Voz neural 24kHz carregada do cache SD: %s", cache_path);
        }
        if (cached_wav) {
            cached_wav.close();
        }
    }

    // Passo 2 (Caminho 1): Frase inédita -> Gemini 2.5 Flash TTS via Wi-Fi
    if (!loaded_raw_wav && oracle_brain->synthesizeCloudNeuralTtsWav(text_utf8, out_wav)) {
        loaded_raw_wav = true;
        if (oracle_brain->isSdStorageReady()) {
            File out_file = SD.open(cache_path, FILE_WRITE);
            if (out_file) {
                out_file.write(out_wav.data(), out_wav.size());
                out_file.close();
                M5_LOGI("[SD-TTS] Voz Cloud Gemini 24kHz salva no cache microSD: %s", cache_path);
            }
        }
    }
#else
    // Passo 1 (Caminho 3): WAV já salvo no cache do Cartão SD espelhado no PC
    {
        std::ifstream cached_file(cache_path, std::ios::binary | std::ios::ate);
        if (cached_file.is_open()) {
            std::streamsize c_size = cached_file.tellg();
            if (c_size > 44) {
                cached_file.seekg(0, std::ios::beg);
                out_wav.resize(static_cast<size_t>(c_size));
                if (cached_file.read(reinterpret_cast<char*>(out_wav.data()), c_size)) {
                    loaded_raw_wav = true;
                    M5_LOGI("[SD-TTS] Voz neural 24kHz carregada do cache SD: %s", cache_path);
                }
            }
        }
    }

    // Passo 2 (Caminho 1): edge-tts local (se instalado) ou Gemini Cloud TTS, e salva no cache SD
    if (!loaded_raw_wav) {
        static int8_t external_tts_status = -1; // Só acessado por esta thread do worker
        if (external_tts_status == -1) {
            int check_res = std::system("edge-tts --version >nul 2>&1 && ffmpeg -version >nul 2>&1");
            external_tts_status = (check_res == 0) ? 1 : 0;
        }

        if (external_tts_status == 1) {
            std::remove("temp_voice.wav");
            {
                std::ofstream txt_file("temp_voice_text.txt", std::ios::binary | std::ios::trunc);
                if (txt_file.is_open()) {
                    txt_file.write(text_utf8.data(), static_cast<std::streamsize>(text_utf8.size()));
                }
            }

            // Converte para PCM 16-bit, 24000 Hz HD, Mono
            const char* tts_cmd =
                "edge-tts --voice pt-BR-AntonioNeural --rate=+15% --pitch=+30Hz -f temp_voice_text.txt --write-media temp_voice.mp3 >nul 2>&1 "
                "&& ffmpeg -y -i temp_voice.mp3 -acodec pcm_s16le -ar 24000 -ac 1 temp_voice.wav >nul 2>&1";

            if (std::system(tts_cmd) == 0) {
                std::ifstream wav_file("temp_voice.wav", std::ios::binary | std::ios::ate);
                if (wav_file.is_open()) {
                    std::streamsize file_size = wav_file.tellg();
                    if (file_size > 44) {
                        wav_file.seekg(0, std::ios::beg);
                        out_wav.resize(static_cast<size_t>(file_size));
                        if (wav_file.read(reinterpret_cast<char*>(out_wav.data()), file_size)) {
                            loaded_raw_wav = true;
                        }
                    }
                }
            }
        }

        if (!loaded_raw_wav) {
            loaded_raw_wav = oracle_brain->synthesizeCloudNeuralTtsWav(text_utf8, out_wav);
        }

        if (loaded_raw_wav && oracle_brain->isSdStorageReady()) {
            std::ofstream sd_cache_out(cache_path, std::ios::binary | std::ios::trunc);
            if (sd_cache_out.is_open()) {
                sd_cache_out.write(reinterpret_cast<const char*>(out_wav.data()),
                                   static_cast<std::streamsize>(out_wav.size()));
                M5_LOGI("[SD-TTS] Frase sintetizada em 24kHz HD e salva no cache do Cartao SD: %s", cache_path);
            }
        }
    }
#endif

    return loaded_raw_wav && out_wav.size() > 44;
}
