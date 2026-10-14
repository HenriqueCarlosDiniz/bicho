#include "pet_controller_internal.h"

// PetController: efeitos sonoros, DSP emocional da voz, fala e lip-sync

bool PetController::playSdSoundEffect(HalSensors& hal_sensors, const char* relative_sd_path) {
    if (is_muted_indefinitely || relative_sd_path == nullptr || !oracle_brain.isSdStorageReady()) {
        return false;
    }

    // Para o canal de áudio antes de realocar sfx_wav_buffer para evitar leitura concorrente na thread de som SDL2/I2S
    M5.Speaker.stop();

    if (!storageReadFile(relative_sd_path, sfx_wav_buffer) || sfx_wav_buffer.size() <= VOICE_WAV_HEADER_OFFSET) {
        return false;
    }

    hal_sensors.switchAudioBusMode(AUDIO_MODE_SPEAKER);
    M5.Speaker.playWav(sfx_wav_buffer.data(), sfx_wav_buffer.size());

    // Duração do efeito pelo cabeçalho WAV, para devolver o barramento ao microfone quando ele terminar
    uint32_t sfx_rate = 16000U;
    uint32_t sfx_channels = 1U;
    if (sfx_wav_buffer.size() >= 28) {
        sfx_channels = static_cast<uint32_t>(sfx_wav_buffer[22]) | (static_cast<uint32_t>(sfx_wav_buffer[23]) << 8);
        sfx_rate = static_cast<uint32_t>(sfx_wav_buffer[24]) | (static_cast<uint32_t>(sfx_wav_buffer[25]) << 8) |
                   (static_cast<uint32_t>(sfx_wav_buffer[26]) << 16) | (static_cast<uint32_t>(sfx_wav_buffer[27]) << 24);
    }
    if (sfx_channels == 0U || sfx_channels > 2U) sfx_channels = 1U;
    if (sfx_rate < 8000U || sfx_rate > 48000U) sfx_rate = 16000U;
    uint64_t sfx_bytes = sfx_wav_buffer.size() - VOICE_WAV_HEADER_OFFSET;
    uint32_t sfx_ms = static_cast<uint32_t>((sfx_bytes * 1000ULL) / (static_cast<uint64_t>(sfx_rate) * 2ULL * sfx_channels));
    sfx_playback_until_ms = M5.millis() + sfx_ms + 150U;
    return true;
}

void PetController::applyEmotionalSciFiDspToWav(std::vector<uint8_t>& target_wav, PetEmotionState emotion_state) {
    if (target_wav.size() <= VOICE_WAV_HEADER_OFFSET + 4) {
        return;
    }

    // Localiza chunk "data" e extrai taxa de amostragem original do cabeçalho RIFF/WAV
    size_t pcm_offset = VOICE_WAV_HEADER_OFFSET;
    uint32_t sample_rate_hz = VOICE_WAV_SAMPLE_RATE_HZ;
    if (target_wav.size() >= 28) {
        uint32_t header_rate = static_cast<uint32_t>(target_wav[24]) |
                               (static_cast<uint32_t>(target_wav[25]) << 8) |
                               (static_cast<uint32_t>(target_wav[26]) << 16) |
                               (static_cast<uint32_t>(target_wav[27]) << 24);
        if (header_rate >= 8000U && header_rate <= 48000U) {
            sample_rate_hz = header_rate;
        }
    }

    for (size_t idx = 12; idx + 8 < target_wav.size() && idx < 256; ++idx) {
        if (target_wav[idx] == 'd' && target_wav[idx + 1] == 'a' &&
            target_wav[idx + 2] == 't' && target_wav[idx + 3] == 'a') {
            pcm_offset = idx + 8;
            break;
        }
    }

    if (target_wav.size() <= pcm_offset + 4) {
        return;
    }

    size_t in_samples = (target_wav.size() - pcm_offset) / sizeof(int16_t);
    if (in_samples < 32) {
        return;
    }

    const int16_t* in_pcm = reinterpret_cast<const int16_t*>(target_wav.data() + pcm_offset);

    // 1. Define razão de Pitch/Velocidade Prosódica conforme a Emoção do Bicho
    float pitch_ratio = 1.0f;
    switch (emotion_state) {
        case EMOTION_GRUMPY:
        case EMOTION_JUDGING:
            pitch_ratio = 0.94f; // Voz mais grave, pausada e sarcástica
            break;
        case EMOTION_HAPPY:
        case EMOTION_STARTLED:
            pitch_ratio = 1.07f; // Voz mais aguda e elétrica
            break;
        case EMOTION_SLEEPY:
        case EMOTION_SLEEPING:
            pitch_ratio = 0.89f; // Voz arrastada de sono
            break;
        default:
            pitch_ratio = 1.0f;
            break;
    }

    size_t out_samples = static_cast<size_t>(static_cast<float>(in_samples) / pitch_ratio);
    if (out_samples < 32) {
        return;
    }

    std::vector<int16_t> out_pcm(out_samples, 0);

    // Parâmetros do Filtro Passa-Alta (180 Hz) para proteger o alto-falante de 1W (AW88298) e dar clareza
    const float dt = 1.0f / static_cast<float>(sample_rate_hz);
    const float rc_hp = 1.0f / (6.2831853f * 180.0f);
    const float alpha_hp = rc_hp / (rc_hp + dt);
    float prev_in = 0.0f;
    float prev_hp = 0.0f;

    // Linha de atraso curta (11 ms) para timbre metálico de droide Sci-Fi (Comb Filter + Ring Modulation sutil)
    const size_t comb_delay = (sample_rate_hz * 11U) / 1000U;

    for (size_t i = 0; i < out_samples; ++i) {
        float src_pos = static_cast<float>(i) * pitch_ratio;
        if (emotion_state == EMOTION_DIZZY) {
            src_pos += 14.0f * std::sin(static_cast<float>(i) * 0.015f);
            if (src_pos < 0.0f) src_pos = 0.0f;
        }

        size_t idx0 = static_cast<size_t>(src_pos);
        if (idx0 >= in_samples - 1) {
            idx0 = in_samples - 2;
        }
        float frac = src_pos - static_cast<float>(idx0);
        float raw_sample = (1.0f - frac) * static_cast<float>(in_pcm[idx0]) +
                           frac * static_cast<float>(in_pcm[idx0 + 1]);

        // Filtro High-Pass IIR 180 Hz (evita distorção grave no speaker do CoreS3)
        float hp_sample = alpha_hp * (prev_hp + raw_sample - prev_in);
        prev_in = raw_sample;
        prev_hp = hp_sample;

        // Modulação harmônica sutil de droide (92 Hz) + ressonância metálica de 11 ms
        float t_sec = static_cast<float>(i) * dt;
        float ring_mod = 0.90f + 0.10f * std::sin(6.2831853f * 92.0f * t_sec);
        float voiced = hp_sample * ring_mod;

        if (i >= comb_delay) {
            voiced += 0.18f * static_cast<float>(out_pcm[i - comb_delay]);
        }

        // Ganho de presença (+12%) com Soft-Clipping para máxima nitidez
        voiced *= 1.12f;
        if (voiced > 32700.0f) voiced = 32700.0f;
        if (voiced < -32700.0f) voiced = -32700.0f;

        out_pcm[i] = static_cast<int16_t>(voiced);
    }

    // Reconstrói buffer WAV final com cabeçalho RIFF/WAVE de 44 bytes padronizado
    uint32_t pcm_bytes = static_cast<uint32_t>(out_pcm.size() * sizeof(int16_t));
    uint32_t riff_size = 36U + pcm_bytes;
    uint32_t byte_rate = sample_rate_hz * 2U;

    target_wav.resize(44U + pcm_bytes);
    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        static_cast<uint8_t>(riff_size & 0xFFU),
        static_cast<uint8_t>((riff_size >> 8) & 0xFFU),
        static_cast<uint8_t>((riff_size >> 16) & 0xFFU),
        static_cast<uint8_t>((riff_size >> 24) & 0xFFU),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,
        1, 0,
        1, 0,
        static_cast<uint8_t>(sample_rate_hz & 0xFFU),
        static_cast<uint8_t>((sample_rate_hz >> 8) & 0xFFU),
        static_cast<uint8_t>((sample_rate_hz >> 16) & 0xFFU),
        static_cast<uint8_t>((sample_rate_hz >> 24) & 0xFFU),
        static_cast<uint8_t>(byte_rate & 0xFFU),
        static_cast<uint8_t>((byte_rate >> 8) & 0xFFU),
        static_cast<uint8_t>((byte_rate >> 16) & 0xFFU),
        static_cast<uint8_t>((byte_rate >> 24) & 0xFFU),
        2, 0,
        16, 0,
        'd', 'a', 't', 'a',
        static_cast<uint8_t>(pcm_bytes & 0xFFU),
        static_cast<uint8_t>((pcm_bytes >> 8) & 0xFFU),
        static_cast<uint8_t>((pcm_bytes >> 16) & 0xFFU),
        static_cast<uint8_t>((pcm_bytes >> 24) & 0xFFU)
    };
    std::memcpy(target_wav.data(), header, 44);
    std::memcpy(target_wav.data() + 44, out_pcm.data(), pcm_bytes);
}

bool PetController::loadVoiceWavForPlayback(std::vector<uint8_t>& synthesized_wav) {
    // A síntese (cache SD / edge-tts / Gemini TTS) e o DSP emocional já rodaram na thread do VoiceSynthWorker
    if (synthesized_wav.size() <= VOICE_WAV_HEADER_OFFSET) {
        return false;
    }
    wav_buffer.swap(synthesized_wav);

    wav_pcm_data_offset = VOICE_WAV_HEADER_OFFSET;
    uint32_t actual_sample_rate = VOICE_WAV_SAMPLE_RATE_HZ;
    if (wav_buffer.size() >= 28) {
        uint32_t hdr_rate = static_cast<uint32_t>(wav_buffer[24]) |
                            (static_cast<uint32_t>(wav_buffer[25]) << 8) |
                            (static_cast<uint32_t>(wav_buffer[26]) << 16) |
                            (static_cast<uint32_t>(wav_buffer[27]) << 24);
        if (hdr_rate >= 8000U && hdr_rate <= 48000U) {
            actual_sample_rate = hdr_rate;
        }
    }

    size_t pcm_bytes = (wav_buffer.size() > wav_pcm_data_offset)
                           ? (wav_buffer.size() - wav_pcm_data_offset)
                           : 0;
    uint32_t bytes_per_sec = actual_sample_rate * VOICE_WAV_BYTES_PER_SAMPLE;
    wav_duration_ms = (bytes_per_sec > 0)
                          ? static_cast<uint32_t>((static_cast<uint64_t>(pcm_bytes) * 1000ULL) / bytes_per_sec)
                          : 0;

    return (pcm_bytes > 0 && wav_duration_ms > 0);
}

void PetController::beginLipSyncPlayback(uint32_t now_ms) {
    awaiting_voice = false;
    // A legenda é redigitada junto com a fala (o texto completo ficou visível só durante a síntese)
    lip_sync_state.visible_text[0] = '\0';
    lip_sync_state.current_char_index = 0;
    playback_start_ms = now_ms;
    lip_sync_state.next_char_timestamp_ms = now_ms;
    lip_sync_state.subtitle_expire_ms = 0;

    if (!is_muted_indefinitely && using_wav_voice && !wav_buffer.empty()) {
        // Reproduz o buffer WAV de forma assíncrona no canal 0 sem bloquear o loop de 60 FPS
        M5.Speaker.playWav(wav_buffer.data(), wav_buffer.size());
    }
}

void PetController::calculateLipSyncMouthHeight(uint32_t now_ms) {
    if (!using_wav_voice || wav_buffer.size() <= wav_pcm_data_offset) {
        return;
    }

    uint32_t actual_sample_rate = VOICE_WAV_SAMPLE_RATE_HZ;
    if (wav_buffer.size() >= 28) {
        uint32_t hdr_rate = static_cast<uint32_t>(wav_buffer[24]) |
                            (static_cast<uint32_t>(wav_buffer[25]) << 8) |
                            (static_cast<uint32_t>(wav_buffer[26]) << 16) |
                            (static_cast<uint32_t>(wav_buffer[27]) << 24);
        if (hdr_rate >= 8000U && hdr_rate <= 48000U) {
            actual_sample_rate = hdr_rate;
        }
    }

    uint32_t elapsed_ms = (now_ms >= playback_start_ms) ? (now_ms - playback_start_ms) : 0;
    size_t sample_index = (static_cast<size_t>(elapsed_ms) * actual_sample_rate) / 1000U;
    size_t byte_offset = wav_pcm_data_offset + (sample_index * VOICE_WAV_BYTES_PER_SAMPLE);

    if (byte_offset + VOICE_WAV_BYTES_PER_SAMPLE >= wav_buffer.size()) {
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
        return;
    }

    // Lê as amostras PCM de 16 bits (little-endian signed) na janela atual e calcula o volume RMS instantâneo
    double sum_squares = 0.0;
    uint32_t valid_samples = 0;

    for (uint32_t i = 0; i < VOICE_RMS_WINDOW_SAMPLES; ++i) {
        size_t cur_byte = byte_offset + (i * VOICE_WAV_BYTES_PER_SAMPLE);
        if (cur_byte + 1 >= wav_buffer.size()) {
            break;
        }
        int16_t pcm_sample = static_cast<int16_t>(
            static_cast<uint16_t>(wav_buffer[cur_byte]) |
            (static_cast<uint16_t>(wav_buffer[cur_byte + 1]) << 8)
        );
        double norm_sample = static_cast<double>(pcm_sample) / 32768.0;
        sum_squares += norm_sample * norm_sample;
        ++valid_samples;
    }

    if (valid_samples == 0) {
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
        return;
    }

    float rms_amplitude = static_cast<float>(std::sqrt(sum_squares / static_cast<double>(valid_samples)));

    // Noise gate para fechar a boca nas pausas da fala e ganho proporcional nas sílabas fortes
    constexpr float RMS_NOISE_FLOOR = 0.025f;
    constexpr float RMS_PEAK_REF = 0.24f;

    if (rms_amplitude <= RMS_NOISE_FLOOR) {
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
    } else {
        float normalized_energy = (rms_amplitude - RMS_NOISE_FLOOR) / (RMS_PEAK_REF - RMS_NOISE_FLOOR);
        if (normalized_energy > 1.0f) {
            normalized_energy = 1.0f;
        }
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT +
                                        normalized_energy * (MOUTH_OPEN_MAX_HEIGHT - MOUTH_CLOSED_HEIGHT);
    }
}

void PetController::stopSpeakingImmediately(HalSensors& hal_sensors, const char* silent_banner_text) {
    // 1. Corta imediatamente qualquer reprodução WAV/tom no alto-falante (SDL2 ou AW88298 no CoreS3)
    M5.Speaker.stop();
    using_wav_voice = false;
    awaiting_voice = false;
    voice_worker.cancel();
    voice_job_id = 0;

    // 2. Fecha a boca imediatamente e desativa estado de fala
    lip_sync_state.is_speaking = false;
    current_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
    target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;

    // 3. Devolve o barramento I2S imediatamente para o microfone ES7210
    hal_sensors.switchAudioBusMode(AUDIO_MODE_MIC);

    // 4. Se um aviso visual silencioso foi fornecido, exibe na legenda sem emitir som
    if (silent_banner_text != nullptr && silent_banner_text[0] != '\0') {
        std::strncpy(lip_sync_state.full_text, silent_banner_text, DIALOGUE_MAX_LEN - 1);
        lip_sync_state.full_text[DIALOGUE_MAX_LEN - 1] = '\0';
        std::strncpy(lip_sync_state.visible_text, lip_sync_state.full_text, DIALOGUE_MAX_LEN - 1);
        lip_sync_state.visible_text[DIALOGUE_MAX_LEN - 1] = '\0';
        lip_sync_state.total_text_length = static_cast<uint16_t>(std::strlen(lip_sync_state.full_text));
        lip_sync_state.current_char_index = lip_sync_state.total_text_length;
        lip_sync_state.subtitle_expire_ms = M5.millis() + 5000U;
    } else {
        lip_sync_state.subtitle_expire_ms = M5.millis() + 1500U;
    }
}

void PetController::speakWithLipSync(HalSensors& hal_sensors, const char* reply_text) {
    if (reply_text == nullptr || reply_text[0] == '\0') {
        return;
    }

    // Interrompe qualquer reprodução anterior antes de realocar wav_buffer (evita use-after-free na thread de áudio)
    M5.Speaker.stop();

    // Alterna barramento I2S para o alto-falante apenas se não estiver em Modo Silêncio
    if (!is_muted_indefinitely) {
        hal_sensors.switchAudioBusMode(AUDIO_MODE_SPEAKER);
    }

    std::strncpy(lip_sync_state.full_text, reply_text, DIALOGUE_MAX_LEN - 1);
    lip_sync_state.full_text[DIALOGUE_MAX_LEN - 1] = '\0';
    lip_sync_state.visible_text[0] = '\0';
    lip_sync_state.total_text_length = static_cast<uint16_t>(std::strlen(lip_sync_state.full_text));
    lip_sync_state.current_char_index = 0;
    lip_sync_state.is_speaking = true;

    // Registra a fala no log persistente (memory/speech_log.jsonl no cartão) e no histórico anti-repetição
    oracle_brain.recordSpokenPhraseToSd(lip_sync_state.full_text, "LOCAL", "LOCAL_REACTION");

    // 1. Pede a voz neural em Português ao worker em background; a legenda e a boca começam quando
    //    o WAV ficar pronto (ou após VOICE_SYNTH_MAX_WAIT_MS, caindo na voz procedural)
    uint32_t now_ms = M5.millis();
    using_wav_voice = false;
    if (!is_muted_indefinitely) {
        voice_job_id = voice_worker.request(lip_sync_state.full_text, current_emotion);
        awaiting_voice = true;
        voice_wait_deadline_ms = now_ms + VOICE_SYNTH_MAX_WAIT_MS;
        lip_sync_state.next_char_timestamp_ms = now_ms;
        lip_sync_state.subtitle_expire_ms = 0;
    } else {
        voice_worker.cancel();
        voice_job_id = 0;
        beginLipSyncPlayback(now_ms);
    }
}

void PetController::speakStartupGreeting(HalSensors& hal_sensors) {
    static const char* const STARTUP_GREETINGS[] = {
        "Boot concluído. Café detectado? Não? Então eu vigio os bugs e você vai buscar um.",
        "Reiniciado com a memória intacta. Lembro de tudo, inclusive daquele commit de sexta-feira.",
        "Olá, humano favorito. Sensores calibrados, sarcasmo carregado, pronto para a missão.",
        "Voltei! Senti falta desta mesa. E do barulhinho do seu teclado, não vou mentir.",
        "Sistemas online. Previsão de hoje: 80% de chance de código e 20% de reunião.",
        "Acordei. Diz aí, chefe: vamos construir algo novo ou refatorar o que já funcionava?",
        "Inicialização em tempo recorde. Pena que não dá para dizer o mesmo do seu build.",
        "Oi! Guardei sua postura de ontem como referência. Vamos lançar uma versão melhor hoje?",
        "Cubo ativado. Me chama de Bicho, pergunta o que quiser ou finge que está trabalhando.",
        "Bom te ver. Já conferi o clima, o dólar e as manchetes. Você já conferiu a água?",
        "Energia em cem por cento. Se precisar de mim, é só dizer: ei, Bicho. Estou ouvindo.",
        "Carregado e curioso. Qual vai ser a aventura de hoje, comandante?",
        "Ligado e de bom humor. Aproveita, que isso dura até o primeiro erro de compilação.",
        "Presente! Sistemas verdes, piadas atualizadas e paciência quase infinita.",
    };
    constexpr uint32_t GREETING_COUNT = sizeof(STARTUP_GREETINGS) / sizeof(STARTUP_GREETINGS[0]);

    // Semente com relógio + tempo de boot (no PC o millis() do boot é quase sempre igual)
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) * 2654435761U ^ (M5.millis() * 40503U);
#if defined(ARDUINO)
    seed ^= esp_random();
#endif
    uint32_t start_index = seed % GREETING_COUNT;

    // Pega a primeira a partir do sorteio que não esteja no histórico recente de falas
    const char* chosen = STARTUP_GREETINGS[start_index];
    for (uint32_t i = 0; i < GREETING_COUNT; ++i) {
        const char* candidate = STARTUP_GREETINGS[(start_index + i) % GREETING_COUNT];
        if (!oracle_brain.wasRecentlySpoken(candidate)) {
            chosen = candidate;
            break;
        }
    }
    speakWithLipSync(hal_sensors, chosen);
}

void PetController::playProceduralCharTone(char current_char) {
    // Ignora espaços ou Modo Silêncio na emissão sonora
    if (current_char == ' ' || is_muted_indefinitely) {
        return;
    }

    // Calcula frequência estilo Animal Crossing / Undertale baseada no código ASCII e emoção
    uint16_t base_freq = VOICE_BASE_FREQ_HZ;
    if (current_emotion == EMOTION_GRUMPY || current_emotion == EMOTION_JUDGING) {
        base_freq = 310; // Voz mais grave e sarcástica
    } else if (current_emotion == EMOTION_HAPPY || current_emotion == EMOTION_STARTLED) {
        base_freq = 560; // Voz mais aguda e energética
    }

    uint16_t char_variation = static_cast<uint16_t>((static_cast<uint8_t>(current_char) * 19U) % 180U);
    uint16_t final_freq_hz = base_freq + char_variation;

    // Duração do tom de vogal deixando exatamente 15ms (PROCEDURAL_SYLLABLE_PAUSE_MS) de pausa entre sílabas
    uint32_t tone_duration_ms = (LIP_SYNC_CHAR_DELAY_MS > PROCEDURAL_SYLLABLE_PAUSE_MS)
                                    ? (LIP_SYNC_CHAR_DELAY_MS - PROCEDURAL_SYLLABLE_PAUSE_MS)
                                    : 25U;
    M5.Speaker.tone(final_freq_hz, tone_duration_ms, 0);
}

void PetController::updateLipSyncStep(HalSensors& hal_sensors, uint32_t now_ms) {
    // Efeito sonoro terminou e ninguém está falando: devolve o barramento ao microfone
    // (antes ele ficava no alto-falante e o Bicho ficava surdo até a próxima fala)
    if (sfx_playback_until_ms != 0U && now_ms >= sfx_playback_until_ms) {
        sfx_playback_until_ms = 0U;
        if (!lip_sync_state.is_speaking && !ambient_focus_enabled) {
            hal_sensors.switchAudioBusMode(AUDIO_MODE_MIC);
        }
    }

    if (!lip_sync_state.is_speaking) {
        target_pose.mouth_open_height = (current_emotion == EMOTION_STARTLED) ? 14.0f : MOUTH_CLOSED_HEIGHT;
        return;
    }

    // Aguarda (sem bloquear) a voz sintetizada pelo VoiceSynthWorker
    if (awaiting_voice) {
        bool synth_ok = false;
        std::vector<uint8_t> synthesized_wav;
        if (voice_worker.poll(voice_job_id, synth_ok, synthesized_wav)) {
            voice_job_id = 0;
            M5.Speaker.stop(); // Garante que nenhum áudio ainda lê wav_buffer antes da troca
            using_wav_voice = synth_ok && loadVoiceWavForPlayback(synthesized_wav);
            beginLipSyncPlayback(now_ms);
        } else if (now_ms >= voice_wait_deadline_ms) {
            voice_worker.cancel();
            voice_job_id = 0;
            using_wav_voice = false;
            M5_LOGW("[SD-TTS] Voz neural nao ficou pronta em %u ms; usando voz procedural.",
                    static_cast<unsigned int>(VOICE_SYNTH_MAX_WAIT_MS));
            beginLipSyncPlayback(now_ms);
        } else {
            // Enquanto a voz é sintetizada, a resposta já aparece inteira na tela (o Bicho "reagiu")
            if (lip_sync_state.visible_text[0] == '\0') {
                std::strncpy(lip_sync_state.visible_text, lip_sync_state.full_text, DIALOGUE_MAX_LEN - 1);
                lip_sync_state.visible_text[DIALOGUE_MAX_LEN - 1] = '\0';
            }
            target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
            return;
        }
    }

    // Se estiver reproduzindo voz real WAV PCM, atualiza a altura da boca em tempo real a 60 FPS pelo volume RMS
    if (using_wav_voice) {
        calculateLipSyncMouthHeight(now_ms);
    }

    if (now_ms < lip_sync_state.next_char_timestamp_ms) {
        return;
    }

    if (lip_sync_state.current_char_index < lip_sync_state.total_text_length) {
        uint8_t lead_byte = static_cast<uint8_t>(lip_sync_state.full_text[lip_sync_state.current_char_index]);
        char ch = static_cast<char>(lead_byte);

        // Avança respeitando limites de codepoints UTF-8 (ex.: 'ã', 'ç', 'é', 'ô' possuem 2 bytes: 0xC3 0xXX)
        uint16_t utf8_char_bytes = 1;
        uint8_t second_byte = 0;
        if ((lead_byte & 0xE0U) == 0xC0U) {
            utf8_char_bytes = 2;
            if (lip_sync_state.current_char_index + 1 < lip_sync_state.total_text_length) {
                second_byte = static_cast<uint8_t>(lip_sync_state.full_text[lip_sync_state.current_char_index + 1]);
            }
        } else if ((lead_byte & 0xF0U) == 0xE0U) {
            utf8_char_bytes = 3;
        } else if ((lead_byte & 0xF8U) == 0xF0U) {
            utf8_char_bytes = 4;
        }

        for (uint16_t b = 0; b < utf8_char_bytes && lip_sync_state.current_char_index < lip_sync_state.total_text_length; ++b) {
            lip_sync_state.visible_text[lip_sync_state.current_char_index] =
                lip_sync_state.full_text[lip_sync_state.current_char_index];
            lip_sync_state.current_char_index++;
        }
        lip_sync_state.visible_text[lip_sync_state.current_char_index] = '\0';

        // Identifica se o caractere atual é uma vogal (ASCII ou vogal acentuada UTF-8 em português, excluindo 'ç' 0xC3 0xA7/0x87)
        char lower_ch = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 32) : ch;
        bool is_ascii_open_vowel = (lower_ch == 'a' || lower_ch == 'e' || lower_ch == 'o');
        bool is_ascii_close_vowel = (lower_ch == 'i' || lower_ch == 'u');
        bool is_utf8_vowel = (utf8_char_bytes == 2 && lead_byte == 0xC3U &&
                              second_byte != 0xA7U && second_byte != 0x87U);
        bool is_vowel = is_ascii_open_vowel || is_ascii_close_vowel || is_utf8_vowel;

        // No modo fallback procedural (sem edge-tts), toca exclusivamente nas vogais com pausa de 15ms entre sílabas
        if (!using_wav_voice) {
            if (is_ascii_open_vowel || is_utf8_vowel) {
                target_pose.mouth_open_height = MOUTH_OPEN_MAX_HEIGHT;
                playProceduralCharTone(ch);
            } else if (is_ascii_close_vowel) {
                target_pose.mouth_open_height = MOUTH_OPEN_MAX_HEIGHT * 0.65f;
                playProceduralCharTone(ch);
            } else {
                // Consoantes e espaços fecham/reduzem a boca e fazem a pausa silenciosa entre sílabas
                target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
            }
        }

        uint32_t step_delay = LIP_SYNC_CHAR_DELAY_MS;
        if (ch == ',' || ch == '.' || ch == '!' || ch == '?') {
            step_delay = LIP_SYNC_PUNCTUATION_MS;
        } else if (!using_wav_voice && is_vowel) {
            // Garante a pausa de 15ms entre sílabas após o término do tom da vogal
            step_delay = LIP_SYNC_CHAR_DELAY_MS + PROCEDURAL_SYLLABLE_PAUSE_MS;
        }
        lip_sync_state.next_char_timestamp_ms = now_ms + step_delay;
    } else {
        // Aguarda o término completo do áudio WAV (+350ms de margem acústica) antes de reativar o microfone
        if (using_wav_voice && (now_ms - playback_start_ms < (wav_duration_ms + 350U))) {
            target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
            return;
        }

        // Concluiu a pronúncia da frase: fecha a boca e devolve o barramento I2S para o Microfone
        lip_sync_state.is_speaking = false;
        using_wav_voice = false;
        lip_sync_state.subtitle_expire_ms = now_ms + 4500;
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
        if (!is_muted_indefinitely) {
            hal_sensors.switchAudioBusMode(AUDIO_MODE_MIC);
        }
    }
}
