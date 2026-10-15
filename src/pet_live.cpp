#include "pet_controller_internal.h"

// PetController: conversa em tempo real pela API Live (voz contínua, interrupção, fim por silêncio)

namespace {
constexpr uint8_t LIVE_SPEAKER_CHANNEL = 7;                         // Canal exclusivo da voz Live no M5.Speaker
constexpr size_t LIVE_PLAY_BLOCK_SAMPLES = LIVE_OUTPUT_SAMPLE_RATE / 10; // Blocos de 100 ms
constexpr uint32_t LIVE_RETRY_AFTER_FAILURE_MS = 60000U;
} // namespace

bool PetController::isLiveAvailable() const {
    return live_enabled && !live_active && M5.millis() >= live_retry_after_ms;
}

bool PetController::startLiveSession(HalSensors& hal_sensors, const std::vector<int16_t>& phrase_pcm16k) {
    const AudioSensorData& audio_data = hal_sensors.getAudioData();
    OracleContextPacket packet;
    packet.current_emotion = current_emotion;
    packet.mood_score = internal_stats.mood_score;
    packet.energy_level = internal_stats.energy_level;
    packet.hunger_level = internal_stats.hunger_level;
    packet.focus_minutes = internal_stats.focus_minutes;
    packet.noise_level_db = audio_data.noise_level_db;
    packet.is_user_present = hal_sensors.getGazeData().face_detected;
    packet.vad_keyword_mode = audio_data.vad_keyword_mode;
    packet.trigger_reason = "VOICE_HANDS_FREE";
    packet.external_world_summary = world_context_service.buildExternalWorldSummary();
    packet.diary_context_json = diary_service.buildContextJson();

    LiveSetupOptions options;
    options.model = live_model;
    options.voice = live_voice;
    options.system_instruction = oracle_brain.buildLiveSystemInstructionFor(packet);

    if (!live_session.start(oracle_brain.getApiKey(), options, phrase_pcm16k)) {
        M5_LOGW("[LIVE] Sessao Live indisponivel (transporte ou chave); usando o fluxo HTTP.");
        live_retry_after_ms = M5.millis() + LIVE_RETRY_AFTER_FAILURE_MS;
        return false;
    }

    // Para qualquer fala local e mantém o microfone aberto durante toda a conversa
    M5.Speaker.stop();
    lip_sync_state.is_speaking = false;
    using_wav_voice = false;
    awaiting_voice = false;
    voice_worker.cancel();
    hal_sensors.switchAudioBusMode(AUDIO_MODE_MIC);
    hal_sensors.setMicStreaming(true);

    live_active = true;
    live_ready_logged = false;
    live_started_ms = M5.millis();
    live_last_activity_ms = live_started_ms;
    live_user_text.clear();
    live_bot_text.clear();
    live_audio_blocks.clear();
    live_mouth_level = 0.0f;
    current_emotion = EMOTION_LISTENING;
    spawnHudPopup("AO VIVO", 132.0f, 44.0f, COLOR_CYBER_GREEN, 1400U);
    M5_LOGI("[LIVE] Abrindo sessao Live (%s, voz %s) com %u ms da frase de chamada.",
            live_model.c_str(), live_voice.c_str(),
            static_cast<unsigned int>(phrase_pcm16k.size() * 1000U / LIVE_INPUT_SAMPLE_RATE));
    return true;
}

void PetController::finishLiveTurn() {
    if (!live_bot_text.empty() || !live_user_text.empty()) {
        oracle_brain.recordLiveTurn(live_user_text, live_bot_text);
        diary_service.logConversation(live_user_text, live_bot_text, "");
        M5_LOGI("[LIVE] Humano: \"%s\" | Bicho: \"%s\"", live_user_text.c_str(), live_bot_text.c_str());
    }
    live_user_text.clear();
    live_bot_text.clear();
}

void PetController::endLiveSession(HalSensors& hal_sensors, const char* reason) {
    if (!live_active) {
        return;
    }
    finishLiveTurn();
    live_session.stop();
    hal_sensors.setMicStreaming(false);
    M5.Speaker.stop(LIVE_SPEAKER_CHANNEL);
    live_audio_blocks.clear();
    live_active = false;
    target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
    current_emotion = EMOTION_IDLE;
    emotion_lock_until_ms = 0;
    spawnHudPopup("ATE JA", 136.0f, 44.0f, COLOR_CYBER_ICE_LIGHT, 1400U);
    M5_LOGI("[LIVE] Sessao encerrada apos %u s (%s).",
            static_cast<unsigned int>((M5.millis() - live_started_ms) / 1000U), reason != nullptr ? reason : "");
}

void PetController::updateLiveSession(HalSensors& hal_sensors, uint32_t now_ms) {
    if (!live_active) {
        return;
    }
    const AudioSensorData& audio_data = hal_sensors.getAudioData();

    // 1. Microfone -> servidor (também enquanto o Bicho fala: é assim que a interrupção funciona)
    std::vector<int16_t> mic;
    hal_sensors.takeMicStream(mic);
    if (!mic.empty()) {
        live_session.pushMicAudio(mic.data(), mic.size());
    }
    if (audio_data.vad_speech_active) {
        live_last_activity_ms = now_ms;
    }

    // 2. Eventos do servidor
    LiveEvent ev;
    while (live_session.popEvent(ev)) {
        switch (ev.type) {
            case LIVE_EVENT_READY:
                if (!live_ready_logged) {
                    live_ready_logged = true;
                    M5_LOGI("[LIVE] Sessao pronta em %u ms.", static_cast<unsigned int>(M5.millis() - live_started_ms));
                }
                break;
            case LIVE_EVENT_INPUT_TEXT:
                live_user_text += ev.text;
                live_last_activity_ms = now_ms;
                break;
            case LIVE_EVENT_OUTPUT_TEXT: {
                live_bot_text += ev.text;
                live_last_activity_ms = now_ms;
                // Legenda: mostra o fim da fala atual se passar do tamanho da caixa
                std::string shown = live_bot_text;
                if (shown.size() >= DIALOGUE_MAX_LEN) {
                    size_t cut = shown.size() - (DIALOGUE_MAX_LEN - 1);
                    while (cut < shown.size() && (static_cast<uint8_t>(shown[cut]) & 0xC0U) == 0x80U) ++cut;
                    shown = shown.substr(cut);
                }
                std::strncpy(lip_sync_state.visible_text, shown.c_str(), DIALOGUE_MAX_LEN - 1);
                lip_sync_state.visible_text[DIALOGUE_MAX_LEN - 1] = '\0';
                lip_sync_state.subtitle_expire_ms = now_ms + 6000U;
                break;
            }
            case LIVE_EVENT_INTERRUPTED:
                M5.Speaker.stop(LIVE_SPEAKER_CHANNEL);
                live_audio_blocks.clear();
                live_bot_text += "...";
                finishLiveTurn();
                live_last_activity_ms = now_ms;
                break;
            case LIVE_EVENT_TURN_COMPLETE:
                finishLiveTurn();
                live_last_activity_ms = now_ms;
                break;
            case LIVE_EVENT_CLOSED: {
                const bool never_ready = !live_ready_logged;
                M5_LOGW("[LIVE] Sessao fechada: %s", ev.text.c_str());
                endLiveSession(hal_sensors, ev.text.c_str());
                if (never_ready) {
                    // Falhou antes de conversar (cota, rede...): volta ao fluxo HTTP por um tempo
                    live_retry_after_ms = now_ms + LIVE_RETRY_AFTER_FAILURE_MS;
                    spawnHudPopup("SEM AO VIVO", 120.0f, 44.0f, COLOR_CYBER_ORANGE, 1800U);
                }
                return;
            }
            default:
                break;
        }
    }

    // 3. Voz do Bicho -> alto-falante em blocos de 100 ms (no máximo 2 enfileirados no canal)
    std::vector<int16_t> audio;
    if (live_session.popAudio(audio)) {
        for (size_t off = 0; off < audio.size(); off += LIVE_PLAY_BLOCK_SAMPLES) {
            size_t n = std::min(LIVE_PLAY_BLOCK_SAMPLES, audio.size() - off);
            live_audio_blocks.emplace_back(audio.begin() + static_cast<std::ptrdiff_t>(off),
                                           audio.begin() + static_cast<std::ptrdiff_t>(off + n));
        }
        live_last_activity_ms = now_ms;
    }
    while (!live_audio_blocks.empty() && M5.Speaker.isPlaying(LIVE_SPEAKER_CHANNEL) < 2) {
        // 3 slots giratórios: com no máximo 2 na fila, o slot reaproveitado já terminou de tocar
        std::vector<int16_t>& slot = live_play_slots[live_slot_index];
        live_slot_index = static_cast<uint8_t>((live_slot_index + 1U) % 3U);
        slot.swap(live_audio_blocks.front());
        live_audio_blocks.pop_front();
        M5.Speaker.playRaw(slot.data(), slot.size(), LIVE_OUTPUT_SAMPLE_RATE, false, 1, LIVE_SPEAKER_CHANNEL, false);

        double sum = 0.0;
        for (int16_t v : slot) sum += static_cast<double>(v) * v;
        float rms = static_cast<float>(std::sqrt(sum / std::max<size_t>(1, slot.size())) / 32768.0);
        live_mouth_level = std::min(1.0f, std::max(0.0f, (rms - 0.02f) / 0.22f));
    }

    // 4. Rosto: boca pela energia da voz; ouvindo quando o humano fala
    const bool bot_speaking = M5.Speaker.isPlaying(LIVE_SPEAKER_CHANNEL) > 0;
    if (bot_speaking) {
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT + live_mouth_level * (MOUTH_OPEN_MAX_HEIGHT - MOUTH_CLOSED_HEIGHT);
        live_mouth_level *= 0.85f;
        current_emotion = EMOTION_HAPPY;
        live_last_activity_ms = now_ms;
    } else {
        target_pose.mouth_open_height = MOUTH_CLOSED_HEIGHT;
        if (audio_data.vad_speech_active) {
            current_emotion = EMOTION_LISTENING;
        }
    }

    // 5. Fim por silêncio: ninguém falou e o Bicho não tem mais nada para dizer
    // (now_ms é capturado no início do frame e pode ser anterior a live_last_activity_ms: comparação com sinal)
    const int32_t idle_ms = static_cast<int32_t>(now_ms - live_last_activity_ms);
    if (!bot_speaking && live_audio_blocks.empty() && idle_ms > static_cast<int32_t>(live_idle_timeout_ms)) {
        endLiveSession(hal_sensors, "sem conversa");
    }
}
