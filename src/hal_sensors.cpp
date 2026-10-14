#include "hal_sensors.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if __has_include(<dirent.h>)
#include <dirent.h>
#endif

#if __has_include(<ArduinoJson.h>)
#include <ArduinoJson.h>
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#if defined(ARDUINO)
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#if __has_include(<esp_camera.h>)
#include <esp_camera.h>
#include <img_converters.h>
#endif
#else
#include <fstream>
#include <sys/stat.h>
#endif

// =============================================================================
// Implementação da Camada de Abstração de Hardware (HAL)
// =============================================================================

std::string HalSensors::encodeBase64(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return "";
    }
    static constexpr const char* B64_TABLE =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < length) ? data[i + 1] : 0U;
        uint32_t octet_c = (i + 2 < length) ? data[i + 2] : 0U;

        uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        encoded.push_back(B64_TABLE[(triple >> 18U) & 0x3FU]);
        encoded.push_back(B64_TABLE[(triple >> 12U) & 0x3FU]);
        encoded.push_back((i + 1 < length) ? B64_TABLE[(triple >> 6U) & 0x3FU] : '=');
        encoded.push_back((i + 2 < length) ? B64_TABLE[triple & 0x3FU] : '=');
    }

    return encoded;
}

HalSensors::HalSensors()
    : current_audio_mode(AUDIO_MODE_MIC),
      last_loud_peak_ms(0),
      has_valid_pip_frame(false),
      has_valid_pip_hd_frame(false),
      has_valid_pip_rgb565(false),
      calibrated_face_y(static_cast<float>(SCREEN_HEIGHT) * 0.48f),
      calibrated_face_w(62.0f),
      calibration_samples(0),
      gesture_hist_count(0),
      gesture_hist_head(0),
      last_gesture_sample_ms(0),
      last_head_gesture_ms(0),
      nod_active_until_ms(0),
      shake_active_until_ms(0),
      shhh_active_until_ms(0),
      thumb_active_until_ms(0),
      victory_active_until_ms(0),
      palm_active_until_ms(0),
      vad_active_speech_frames(0),
      vad_trailing_silence_frames(0),
      last_vad_trigger_ms(0),
      mic_echo_guard_until_ms(0),
      dsp_noise_floor_db(32.0f),
      dsp_prev_block_db(NOISE_FLOOR_DB),
      dsp_prev_zcr(0.08f),
      dsp_latest_zcr(0.08f),
      dsp_latest_crest(1.5f),
      dsp_impulse_active(false),
      dsp_impulse_start_ms(0),
      dsp_impulse_peak_db(NOISE_FLOOR_DB),
      dsp_impulse_max_zcr(0.0f),
      dsp_impulse_max_crest(0.0f),
      dsp_clap_stage(0),
      dsp_last_clap_ms(0),
      dsp_double_clap_latched(false),
      dsp_whistle_accum_ms(0),
      dsp_last_whistle_ms(0),
      dsp_whistle_latched(false),
      dsp_typing_click_count(0),
      dsp_typing_window_start_ms(0),
      dsp_typing_active_until_ms(0),
      dsp_kw_phonetic_stage(0),
      dsp_voiced_formant_frames(0),
      dsp_syllable_peaks(0),
      dsp_in_syllable_peak(false),
      dsp_wake_word_latched(false),
      dsp_last_beat_peak_ms(0),
      dsp_beat_interval_ms(500),
      dsp_beat_streak(0),
      dsp_music_active_until_ms(0)
#if defined(ARDUINO)
    , cores3_camera_initialized(false),
      cores3_ltr553_initialized(false),
      last_cores3_cam_capture_ms(0),
      last_cores3_ltr_read_ms(0),
      cores3_mic_ring_buffer(MIC_RING_BUFFER_SAMPLES, 0),
      cores3_mic_write_pos(0),
      cores3_mic_filled(false)
#else
    , prev_key_space(false),
      prev_key_m(false),
      prev_key_i(false),
      prev_key_r(false),
      prev_key_p(false),
      prev_key_o(false),
      prev_key_d(false),
      prev_key_f(false),
      prev_key_v(false),
      prev_key_g(false),
      prev_key_b(false),
      prev_key_n(false),
      prev_key_t(false),
      prev_key_z(false),
      prev_key_tab(false),
      prev_key_c(false),
      prev_key_x(false),
      prev_key_s(false),
      prev_key_q(false),
      prev_key_j(false),
      prev_key_k(false),
      prev_key_h(false),
      prev_key_w(false),
      simulated_presence_enabled(true),
      manual_meeting_override(false),
      webcam_ambient_lux(240),
      mic_device_id(0),
      mic_capture_active(false),
      mic_ring_buffer(MIC_RING_BUFFER_SAMPLES, 0),
      mic_ring_write_pos(0),
      mic_ring_filled(false),
      realtime_mic_db(NOISE_FLOOR_DB),
      sustained_voice_frames(0),
      silence_voice_frames(0),
      last_face_track_check_ms(0),
      last_pip_read_ms(0),
      webcam_tracker_active(false)
#endif
    , debug_mode_enabled(false)
{
    std::memset(&gaze_data, 0, sizeof(gaze_data));
    gaze_data.raw_screen_x = SCREEN_WIDTH / 2;
    gaze_data.raw_screen_y = SCREEN_HEIGHT / 2;
    gaze_data.face_detected = true;
    gaze_data.face_width_px = 62;
    gaze_data.face_count = 1;
    gaze_data.secondary_face_x = -1;
    gaze_data.secondary_face_y = -1;

    std::memset(&audio_data, 0, sizeof(audio_data));
    audio_data.noise_level_db = NOISE_FLOOR_DB;
    audio_data.adaptive_noise_floor_db = 32.0f;
    audio_data.spectral_zcr = 0.08f;
    audio_data.vad_enabled = true;
    audio_data.vad_keyword_mode = true; // Padrão: Palavra-Chave "Bicho" / "Ei Bicho" / "Oráculo" ativa!
    audio_data.last_event = AUDIO_EVENT_NONE;

    env_data.accel_x = 0.0f;
    env_data.accel_y = 0.0f;
    env_data.accel_z = 1.0f;
    env_data.ambient_lux = 240;
    env_data.proximity_raw = 0;
    env_data.battery_percent = 100;
    env_data.is_charging = false;
    env_data.cpu_usage_percent = 18;
    env_data.ram_usage_percent = 45;
    env_data.git_modified_files = 0;
    std::snprintf(env_data.active_window_title, sizeof(env_data.active_window_title), "CoreS3 Ready");
    std::memset(&sd_config, 0, sizeof(sd_config));
    std::snprintf(sd_config.wifi_ssid, sizeof(sd_config.wifi_ssid), "%s", WIFI_SSID);
    std::snprintf(sd_config.wifi_password, sizeof(sd_config.wifi_password), "%s", WIFI_PASSWORD);
    std::snprintf(sd_config.gemini_api_key, sizeof(sd_config.gemini_api_key), "%s", AI_API_KEY);
    sd_config.latitude = -23.5505f;
    sd_config.longitude = -46.6333f;
    sd_config.time_scale = 1.0f;
    sd_config.speaker_volume = 175;
    sd_config.morning_briefing_enabled = true;
    std::memset(pip_frame_gray, 0, sizeof(pip_frame_gray));
    std::memset(pip_hd_frame_gray, 0, sizeof(pip_hd_frame_gray));
    std::memset(pip_frame_rgb565, 0, sizeof(pip_frame_rgb565));
    std::memset(pip_hd_frame_rgb565, 0, sizeof(pip_hd_frame_rgb565));
    std::memset(gesture_hist_x, 0, sizeof(gesture_hist_x));
    std::memset(gesture_hist_y, 0, sizeof(gesture_hist_y));
#if defined(ARDUINO)
    std::memset(prev_cores3_luma_grid, 128, sizeof(prev_cores3_luma_grid));
#endif
}

HalSensors::~HalSensors() {
#if !defined(ARDUINO)
#if __has_include(<SDL2/SDL.h>) || __has_include(<SDL.h>)
    if (mic_capture_active && mic_device_id != 0) {
        SDL_PauseAudioDevice(mic_device_id, 1);
        SDL_CloseAudioDevice(mic_device_id);
        mic_device_id = 0;
        mic_capture_active = false;
    }
#endif
#endif
}

bool HalSensors::loadOrInitSdConfig() {
    FILE* f = std::fopen(SD_CONFIG_FILE_PATH, "r");
    if (f == nullptr) {
        // Garante diretório sd_card/ e cria sd_card/config.json padrão se ainda não existir
#if !defined(ARDUINO)
#if defined(_WIN32)
        std::system("if not exist \"sd_card\" mkdir \"sd_card\" >nul 2>&1");
#else
        mkdir("sd_card", 0755);
#endif
#endif
        FILE* wf = std::fopen(SD_CONFIG_FILE_PATH, "w");
        if (wf != nullptr) {
            std::fprintf(
                wf,
                "{\n"
                "  \"wifi_ssid\": \"%s\",\n"
                "  \"wifi_password\": \"%s\",\n"
                "  \"gemini_api_key\": \"\",\n"
                "  \"latitude\": %.4f,\n"
                "  \"longitude\": %.4f,\n"
                "  \"time_scale\": %.1f,\n"
                "  \"speaker_volume\": %u,\n"
                "  \"morning_briefing_enabled\": %s\n"
                "}\n",
                sd_config.wifi_ssid,
                sd_config.wifi_password,
                sd_config.latitude,
                sd_config.longitude,
                sd_config.time_scale,
                static_cast<unsigned int>(sd_config.speaker_volume),
                sd_config.morning_briefing_enabled ? "true" : "false"
            );
            std::fclose(wf);
            M5_LOGI("[SD-CFG] Arquivo padrao '%s' gerado no Cartao SD.", SD_CONFIG_FILE_PATH);
        }
        return true;
    }

    char buf[1024] = {0};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';

    auto extractJsonString = [&](const char* key, char* out_buf, size_t max_len) {
        const char* pos = std::strstr(buf, key);
        if (pos == nullptr) return;
        const char* colon = std::strchr(pos, ':');
        if (colon == nullptr) return;
        const char* q1 = std::strchr(colon, '"');
        if (q1 == nullptr) return;
        ++q1;
        const char* q2 = std::strchr(q1, '"');
        if (q2 == nullptr) return;
        size_t len = static_cast<size_t>(q2 - q1);
        if (len == 0) return; // Ignora string vazia para preservar secrets.h caso não preenchido no JSON
        if (len >= max_len) len = max_len - 1;
        std::memcpy(out_buf, q1, len);
        out_buf[len] = '\0';
    };

    extractJsonString("\"wifi_ssid\"", sd_config.wifi_ssid, sizeof(sd_config.wifi_ssid));
    extractJsonString("\"wifi_password\"", sd_config.wifi_password, sizeof(sd_config.wifi_password));
    extractJsonString("\"gemini_api_key\"", sd_config.gemini_api_key, sizeof(sd_config.gemini_api_key));

    const char* lat_pos = std::strstr(buf, "\"latitude\"");
    if (lat_pos != nullptr) {
        const char* c = std::strchr(lat_pos, ':');
        if (c != nullptr) std::sscanf(c + 1, "%f", &sd_config.latitude);
    }
    const char* lon_pos = std::strstr(buf, "\"longitude\"");
    if (lon_pos != nullptr) {
        const char* c = std::strchr(lon_pos, ':');
        if (c != nullptr) std::sscanf(c + 1, "%f", &sd_config.longitude);
    }
    const char* ts_pos = std::strstr(buf, "\"time_scale\"");
    if (ts_pos != nullptr) {
        const char* c = std::strchr(ts_pos, ':');
        if (c != nullptr) std::sscanf(c + 1, "%f", &sd_config.time_scale);
    }
    const char* vol_pos = std::strstr(buf, "\"speaker_volume\"");
    if (vol_pos != nullptr) {
        const char* c = std::strchr(vol_pos, ':');
        int v = 175;
        if (c != nullptr && std::sscanf(c + 1, "%d", &v) == 1) {
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            sd_config.speaker_volume = static_cast<uint8_t>(v);
        }
    }
    const char* mb_pos = std::strstr(buf, "\"morning_briefing_enabled\"");
    if (mb_pos != nullptr) {
        const char* c = std::strchr(mb_pos, ':');
        if (c != nullptr) {
            sd_config.morning_briefing_enabled = (std::strstr(c + 1, "false") == nullptr ||
                                                  (std::strstr(c + 1, "true") != nullptr &&
                                                   std::strstr(c + 1, "true") < std::strstr(c + 1, "false")));
        }
    }

    M5_LOGI("[SD-CFG] Configuracao carregada de '%s' (Lat=%.2f, Lon=%.2f, Vol=%u, Briefing=%s)",
            SD_CONFIG_FILE_PATH,
            sd_config.latitude,
            sd_config.longitude,
            static_cast<unsigned int>(sd_config.speaker_volume),
            sd_config.morning_briefing_enabled ? "ON" : "OFF");
    return true;
}

const SdSystemConfig& HalSensors::getSdConfig() const {
    return sd_config;
}

bool HalSensors::initHardware() {
    loadOrInitSdConfig();
#if defined(ARDUINO)
    initCoreS3Peripherals();
#else
    M5_LOGI("[HAL-PC] Simulador Desktop SDL2 inicializado.");
    M5_LOGI("[HAL-PC] Controles: RPG [TAB] | Chuva [W] | Gestos: Shhh [Q] Joinha [J] Selfie [K] Palma [H] | Postura [C] | Óptica PIP [X] | Objeto [S] | Tema [T] | Zoom [Z] | Voz [V] | Pomodoro [F] | Timelapse [G] | Som [B] | Nota [N] | Debug [D]");

    // Se o script de rastreamento facial da webcam não estiver rodando ainda, inicia automaticamente em background
    struct stat track_stat;
    std::time_t now_epoch = std::time(nullptr);
    bool tracker_already_running = (stat(FACE_TRACK_FILE_PATH, &track_stat) == 0 &&
                                    std::fabs(std::difftime(now_epoch, track_stat.st_mtime)) <= 3.5);
    if (!tracker_already_running) {
#if defined(_WIN32)
        std::system("start \"\" /B python scripts/webcam_tracker.py >nul 2>&1");
#else
        std::system("python3 scripts/webcam_tracker.py >/dev/null 2>&1 &");
#endif
        M5_LOGI("[HAL-PC] Processo background 'scripts/webcam_tracker.py' (YuNet ONNX Visao 2.0 + Gestos) disparado.");
    }

#if __has_include(<SDL2/SDL.h>) || __has_include(<SDL.h>)
    // Garante inicialização do subsistema de áudio do SDL2 para captura de microfone real
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            M5_LOGW("[HAL-PC] Aviso: Falha ao inicializar SDL_INIT_AUDIO: %s", SDL_GetError());
        }
    }

    SDL_AudioSpec desired_spec;
    SDL_AudioSpec obtained_spec;
    SDL_zero(desired_spec);
    SDL_zero(obtained_spec);

    desired_spec.freq = static_cast<int>(MIC_CAPTURE_SAMPLE_RATE); // 16kHz
    desired_spec.format = AUDIO_S16SYS;                            // 16-bit PCM signed
    desired_spec.channels = MIC_CAPTURE_CHANNELS;                  // 1 canal (Mono)
    desired_spec.samples = MIC_CAPTURE_BUFFER_FRAMES;              // 512 frames (~32ms de baixa latência)
    desired_spec.callback = HalSensors::sdlAudioCaptureCallback;
    desired_spec.userdata = this;

    // Abre o dispositivo padrão de captura de áudio do sistema (SDL_TRUE = iscapture)
    mic_device_id = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &desired_spec, &obtained_spec, 0);
    if (mic_device_id != 0) {
        mic_capture_active = true;
        SDL_PauseAudioDevice(mic_device_id, 0); // Inicia a captura imediatamente
        const char* default_mic_name = SDL_GetAudioDeviceName(0, SDL_TRUE);
        M5_LOGI("[HAL-PC] Microfone REAL inicializado via SDL2 (%d Hz, %d ch, buf=%u) | Dispositivo: %s",
                obtained_spec.freq,
                static_cast<int>(obtained_spec.channels),
                static_cast<unsigned int>(obtained_spec.samples),
                (default_mic_name != nullptr) ? default_mic_name : "Padrao do Sistema");
    } else {
        mic_capture_active = false;
        M5_LOGW("[HAL-PC] Aviso: Nenhum microfone real detectado pelo SDL2 (%s). Usando simulacao de teclado.",
                SDL_GetError());
    }
#endif
#endif
    return true;
}

void HalSensors::recalibratePostureBaseline() {
    if (gaze_data.face_detected) {
        calibrated_face_y = static_cast<float>(gaze_data.raw_screen_y);
        calibrated_face_w = (gaze_data.face_width_px > 20) ? static_cast<float>(gaze_data.face_width_px) : 62.0f;
        calibration_samples = 40;
        gaze_data.posture_calibrated = true;
        gaze_data.is_slouching = false;
        gaze_data.is_too_close = false;
        M5_LOGI("[VISAO-CAL] Postura ergonomica pessoal recalibrada! Baseline Y=%.1f px | Largura Rosto=%.1f px",
                calibrated_face_y, calibrated_face_w);
    } else {
        calibration_samples = 0;
        gaze_data.posture_calibrated = false;
        M5_LOGI("[VISAO-CAL] Aguardando deteccao de rosto para auto-calibrar baseline de postura...");
    }
}

void HalSensors::updateSharedVisionCalibrationAndGestures(uint32_t now_ms, bool raw_slouch_hint, bool raw_close_hint) {
    // 1. Mantém flags temporárias de gestos ("SIM", "NÃO", "SHHH", "JOINHA", "VITÓRIA", "PALMA") ativas por ~900ms
    gaze_data.head_nod_yes      = (now_ms < nod_active_until_ms);
    gaze_data.head_shake_no     = (now_ms < shake_active_until_ms);
    gaze_data.gesture_shhh      = (now_ms < shhh_active_until_ms);
    gaze_data.gesture_thumbs_up = (now_ms < thumb_active_until_ms);
    gaze_data.gesture_victory   = (now_ms < victory_active_until_ms);
    gaze_data.gesture_open_palm = (now_ms < palm_active_until_ms);

    if (!gaze_data.face_detected) {
        gaze_data.is_slouching = false;
        gaze_data.is_too_close = false;
        gesture_hist_count = 0;
        return;
    }

    // 2. Auto-calibração progressiva da postura ergonômica pessoal nas primeiras 35 leituras válidas (~1.8s)
    const float cur_y = static_cast<float>(gaze_data.raw_screen_y);
    const float cur_w = (gaze_data.face_width_px > 18) ? static_cast<float>(gaze_data.face_width_px) : 62.0f;

    if (calibration_samples < 35) {
        // Ignora poses extremas na borda do quadro durante a calibração inicial
        if (cur_y >= 35.0f && cur_y <= static_cast<float>(SCREEN_HEIGHT - 40)) {
            if (calibration_samples == 0) {
                calibrated_face_y = cur_y;
                calibrated_face_w = cur_w;
            } else {
                calibrated_face_y = calibrated_face_y * 0.88f + cur_y * 0.12f;
                calibrated_face_w = calibrated_face_w * 0.88f + cur_w * 0.12f;
            }
            ++calibration_samples;
            if (calibration_samples == 35) {
                gaze_data.posture_calibrated = true;
                M5_LOGI("[VISAO-CAL] Auto-calibracao de postura concluida: BaseY=%.1f px | BaseW=%.1f px",
                        calibrated_face_y, calibrated_face_w);
            }
        }
    } else {
        // Adaptação lenta de longo prazo quando o usuário está sentado em postura normal
        if (!raw_slouch_hint && !raw_close_hint && std::fabs(cur_y - calibrated_face_y) < 22.0f) {
            calibrated_face_y = calibrated_face_y * 0.997f + cur_y * 0.003f;
            calibrated_face_w = calibrated_face_w * 0.997f + cur_w * 0.003f;
        }
    }

    // 3. Avaliação ergonômica RELATIVA ao baseline pessoal do usuário (elimina falsos positivos de altura da cadeira/mesa)
    if (gaze_data.posture_calibrated) {
        // Postura curvada ("camarão"): cabeça caiu > 30px abaixo do baseline pessoal OU encolheu e desceu
        bool rel_slouch = (cur_y - calibrated_face_y) > 30.0f ||
                          ((cur_y - calibrated_face_y) > 22.0f && raw_slouch_hint);
        // Rosto colado na tela: largura do rosto aumentou > 36% acima do baseline pessoal
        bool rel_close = (cur_w > calibrated_face_w * 1.36f) ||
                         (cur_w > calibrated_face_w * 1.24f && raw_close_hint);
        gaze_data.is_slouching = rel_slouch;
        gaze_data.is_too_close = rel_close;
    } else {
        gaze_data.is_slouching = raw_slouch_hint;
        gaze_data.is_too_close = raw_close_hint;
    }

    // 4. Gestos de cabeça automáticos ("SIM" / "NÃO") desativados na câmera para evitar falsos positivos ao olhar para monitores laterais.
    (void)now_ms;
}

void HalSensors::analyzeSharedPcmBlockDsp(const int16_t* samples,
                                          size_t sample_count,
                                          float block_db,
                                          double rms_raw,
                                          uint32_t now_ms) {
    if (samples == nullptr || sample_count < 16) {
        return;
    }

    uint32_t block_ms = static_cast<uint32_t>((sample_count * 1000U) / MIC_CAPTURE_SAMPLE_RATE);
    if (block_ms < 8U) {
        block_ms = 8U;
    }

    // 1. Extrai Zero-Crossing Rate (ZCR) com histerese anti-ruído térmico e Fator de Crista (Peak/RMS)
    uint32_t zero_crossings = 0;
    int32_t peak_abs = 0;
    int16_t prev_s = samples[0];
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t abs_s = std::abs(static_cast<int32_t>(samples[i]));
        if (abs_s > peak_abs) {
            peak_abs = abs_s;
        }
        if (i > 0) {
            int16_t cur_s = samples[i];
            if (((cur_s >= 0 && prev_s < 0) || (cur_s < 0 && prev_s >= 0)) &&
                std::abs(static_cast<int32_t>(cur_s) - static_cast<int32_t>(prev_s)) > 55) {
                ++zero_crossings;
            }
            prev_s = cur_s;
        }
    }

    float zcr = static_cast<float>(zero_crossings) / static_cast<float>(sample_count);
    float crest = (rms_raw > 12.0) ? static_cast<float>(static_cast<double>(peak_abs) / rms_raw) : 1.2f;
    dsp_latest_zcr = zcr;
    dsp_latest_crest = crest;

    // 2. Piso de Ruído Ambiente Adaptativo (aprende ventoinha/ar-condicionado sem travar o VAD)
    if (block_db < dsp_noise_floor_db) {
        dsp_noise_floor_db = (dsp_noise_floor_db * 0.92f) + (block_db * 0.08f);
    } else if (!dsp_impulse_active && (block_db < dsp_noise_floor_db + 9.0f || zcr > 0.28f)) {
        // Sobe muito devagar apenas em ruído estacionário de fundo
        dsp_noise_floor_db = (dsp_noise_floor_db * 0.996f) + (block_db * 0.004f);
    }
    if (dsp_noise_floor_db < NOISE_FLOOR_DB) dsp_noise_floor_db = NOISE_FLOOR_DB;
    if (dsp_noise_floor_db > 62.0f)          dsp_noise_floor_db = 62.0f;

    const float speech_thresh_db = fmaxf(44.5f, dsp_noise_floor_db + 10.5f);
    const float impulse_onset_db = fmaxf(48.0f, dsp_noise_floor_db + 13.0f);

    // 3. Detector de Transientes Impulsivos: Duas Palmas ("Clap-Clap" 👏👏) vs Digitação em Teclado Mecânico
    if (!dsp_impulse_active) {
        bool sharp_onset = (block_db >= impulse_onset_db) &&
                           ((block_db - dsp_prev_block_db >= 8.5f) || (crest >= 2.75f));
        if (sharp_onset) {
            dsp_impulse_active = true;
            dsp_impulse_start_ms = now_ms;
            dsp_impulse_peak_db = block_db;
            dsp_impulse_max_zcr = zcr;
            dsp_impulse_max_crest = crest;
        }
    } else {
        if (block_db > dsp_impulse_peak_db) dsp_impulse_peak_db = block_db;
        if (zcr > dsp_impulse_max_zcr)      dsp_impulse_max_zcr = zcr;
        if (crest > dsp_impulse_max_crest)  dsp_impulse_max_crest = crest;

        uint32_t imp_dur_ms = (now_ms >= dsp_impulse_start_ms) ? (now_ms - dsp_impulse_start_ms) : block_ms;
        bool impulse_decayed = (block_db < dsp_noise_floor_db + 9.5f) ||
                               (block_db < dsp_impulse_peak_db - 11.0f);

        if (impulse_decayed || imp_dur_ms > 220U) {
            dsp_impulse_active = false;
            // Transiente curto (< 175 ms): verifica se foi Palma Forte ou Tecla de Teclado Mecânico
            if (imp_dur_ms <= 175U) {
                bool is_strong_clap = (dsp_impulse_peak_db >= fmaxf(56.0f, dsp_noise_floor_db + 18.5f)) &&
                                      (dsp_impulse_max_crest >= 2.45f);
                if (is_strong_clap) {
                    uint32_t since_prev_clap = (dsp_impulse_start_ms >= dsp_last_clap_ms)
                                                   ? (dsp_impulse_start_ms - dsp_last_clap_ms)
                                                   : 9999U;
                    if (dsp_clap_stage == 1 && since_prev_clap >= 120U && since_prev_clap <= 750U) {
                        dsp_double_clap_latched = true;
                        dsp_clap_stage = 0;
                        dsp_last_clap_ms = now_ms;
                        M5_LOGI("[OUVIDO-DSP] Duas Palmas ('Clap-Clap') detectadas! (dt=%u ms, pico=%.1f dB)",
                                static_cast<unsigned int>(since_prev_clap), dsp_impulse_peak_db);
                    } else {
                        dsp_clap_stage = 1;
                        dsp_last_clap_ms = dsp_impulse_start_ms;
                    }
                } else if (imp_dur_ms <= 105U && (dsp_impulse_max_zcr >= 0.19f || dsp_impulse_max_crest >= 2.75f)) {
                    // Clique curto de alta frequência -> Digitação em Teclado Mecânico!
                    if (now_ms - dsp_typing_window_start_ms > 2600U) {
                        dsp_typing_window_start_ms = now_ms;
                        dsp_typing_click_count = 1;
                    } else {
                        if (dsp_typing_click_count < 250) ++dsp_typing_click_count;
                        if (dsp_typing_click_count >= 3) {
                            dsp_typing_active_until_ms = now_ms + 2000U;
                        }
                    }
                }
            }
        }
    }

    // Expira estágio de 1ª palma se a 2ª não vier em 800ms
    if (dsp_clap_stage == 1 && (now_ms - dsp_last_clap_ms > 800U)) {
        dsp_clap_stage = 0;
    }

    // 4. Detector de Assobio Humano (Senóide estreita 950 Hz a 2800 Hz -> ZCR estável 0.11 a 0.35)
    bool is_whistle_frame = (block_db >= dsp_noise_floor_db + 10.5f) &&
                            (zcr >= 0.11f && zcr <= 0.35f) &&
                            (std::fabs(zcr - dsp_prev_zcr) < 0.036f) &&
                            (crest < 2.55f);
    if (is_whistle_frame) {
        dsp_whistle_accum_ms += block_ms;
        if (dsp_whistle_accum_ms >= 240U && (now_ms - dsp_last_whistle_ms > 5000U)) {
            dsp_whistle_latched = true;
            dsp_last_whistle_ms = now_ms;
            dsp_whistle_accum_ms = 0;
            M5_LOGI("[OUVIDO-DSP] Assobio humano detectado! (ZCR=%.3f, Vol=%.1f dB)", zcr, block_db);
        }
    } else {
        if (dsp_whistle_accum_ms >= block_ms) {
            dsp_whistle_accum_ms -= block_ms;
        } else {
            dsp_whistle_accum_ms = 0;
        }
    }

    // 5. Detector de Batida Musical / Ritmo (70 a 205 BPM -> intervalos de 290ms a 850ms)
    if (block_db >= dsp_noise_floor_db + 9.0f && (block_db - dsp_prev_block_db >= 5.0f) && zcr < 0.20f) {
        uint32_t beat_dt = (now_ms >= dsp_last_beat_peak_ms) ? (now_ms - dsp_last_beat_peak_ms) : 0U;
        if (beat_dt >= 290U && beat_dt <= 850U) {
            int32_t tempo_diff = std::abs(static_cast<int32_t>(beat_dt) - static_cast<int32_t>(dsp_beat_interval_ms));
            dsp_beat_interval_ms = (dsp_beat_interval_ms + beat_dt) / 2U;
            if (tempo_diff <= 140) {
                if (dsp_beat_streak < 10) ++dsp_beat_streak;
                if (dsp_beat_streak >= 3) {
                    dsp_music_active_until_ms = now_ms + 2200U;
                }
            }
        } else if (beat_dt > 1200U) {
            dsp_beat_streak = 1;
        }
        dsp_last_beat_peak_ms = now_ms;
    }

    // 6. Detector Acústico-Prosódico da Palavra-Chave ("Bi-cho" / "Ei Bi-cho" / "Oráculo")
    // Na língua portuguesa, "Bi-cho" possui:
    //   Estágio 1: Vogal anterior sonora ("Bi" / "Ei") com formante vocal (ZCR < 0.175)
    //   Estágio 2: Fricativa palatal surda "ch" [ʃ] (ZCR >= 0.175) OU vale inter-silábico nítido
    //   Estágio 3: Vogal posterior sonora final ("cho" / "lo") com formante vocal (ZCR < 0.185)
    if (block_db >= speech_thresh_db && block_db < 85.0f) {
        bool is_voiced_formant = (zcr >= 0.014f && zcr <= 0.178f) && (crest < 3.4f);
        bool is_palatal_fricative = (zcr > 0.178f && zcr <= 0.52f);

        if (is_voiced_formant) {
            if (dsp_voiced_formant_frames < 1000U) {
                ++dsp_voiced_formant_frames;
            }
            if (!dsp_in_syllable_peak && block_db >= speech_thresh_db + 2.5f) {
                dsp_in_syllable_peak = true;
                if (dsp_syllable_peaks < 20U) ++dsp_syllable_peaks;
            }
            if (dsp_kw_phonetic_stage == 0) {
                dsp_kw_phonetic_stage = 1; // Vogal inicial ("Bi" / "Ei" / "O")
            } else if (dsp_kw_phonetic_stage == 2) {
                dsp_kw_phonetic_stage = 3; // Vogal final pós-fricativa/sílabas ("cho" / "lo") -> Match!
            }
        } else if (is_palatal_fricative) {
            dsp_in_syllable_peak = false;
            if (dsp_kw_phonetic_stage == 1 && dsp_voiced_formant_frames >= 2U) {
                dsp_kw_phonetic_stage = 2; // Fricativa "ch" [ʃ] ou sibilante detectada!
            }
        }

        // Também reconhece a transição inter-silábica de "Bi-cho" se o microfone tiver roll-off em agudos
        if (dsp_in_syllable_peak && block_db < dsp_prev_block_db - 3.2f) {
            dsp_in_syllable_peak = false;
            if (dsp_kw_phonetic_stage == 1 && dsp_voiced_formant_frames >= 3U) {
                dsp_kw_phonetic_stage = 2;
            }
        }

        if (dsp_kw_phonetic_stage >= 3 && dsp_voiced_formant_frames >= 5U) {
            dsp_wake_word_latched = true;
        }
    } else if (block_db < dsp_noise_floor_db + 6.0f) {
        dsp_in_syllable_peak = false;
    }

    dsp_prev_block_db = block_db;
    dsp_prev_zcr = zcr;
}

void HalSensors::updateSensors() {
#if defined(ARDUINO)
    readCoreS3CameraGaze();
    readCoreS3Microphone();
    readCoreS3OnboardSensors();
#else
    readPcSimulatedGaze();
    readPcSimulatedAudio();
    readPcSimulatedEnvironment();
#endif
}

void HalSensors::updatePerceptionSensors() {
    updateSensors();
}

void HalSensors::switchAudioBusMode(AudioModeState desired_mode) {
    if (current_audio_mode == desired_mode) {
        return;
    }

#if defined(ARDUINO)
    // No M5Stack CoreS3, o codec ES7210 (Mic) e o amp AW88298 (Speaker)
    // compartilham o barramento I2S. É obrigatório encerrar um antes de iniciar o outro.
    if (desired_mode == AUDIO_MODE_SPEAKER) {
        M5.Mic.end();
        M5.Speaker.begin();
    } else {
        M5.Speaker.end();
        M5.Mic.begin();
    }
    cores3_mic_write_pos = 0;
    cores3_mic_filled = false;
#else
    {
        std::lock_guard<std::mutex> lock(mic_buffer_mutex);
        mic_ring_write_pos = 0;
        mic_ring_filled = false;
        realtime_mic_db = NOISE_FLOOR_DB;
    }
    sustained_voice_frames = 0;
    silence_voice_frames = 0;
    if (!manual_meeting_override) {
        audio_data.is_continuous_speech = false;
    }
#endif

    // Proteção anti-eco: bloqueia o microfone por 2.5 segundos após o término da fala do próprio Bicho
    // para que ele jamais ouça a própria voz saindo da caixa de som e entre em loop!
    vad_active_speech_frames = 0;
    vad_trailing_silence_frames = 0;
    audio_data.vad_speech_active = false;
    audio_data.vad_phrase_completed = false;
    mic_echo_guard_until_ms = M5.millis() + 2500U;

    current_audio_mode = desired_mode;
}

std::string HalSensors::recordUserAudioWav() const {
    std::vector<int16_t> pcm_samples;
#if defined(ARDUINO)
    size_t total_available = cores3_mic_filled ? MIC_RING_BUFFER_SAMPLES : cores3_mic_write_pos;
    if (total_available < (MIC_CAPTURE_SAMPLE_RATE / 2U)) {
        return "";
    }
    pcm_samples.resize(total_available);
    if (cores3_mic_filled) {
        size_t tail_len = MIC_RING_BUFFER_SAMPLES - cores3_mic_write_pos;
        std::memcpy(pcm_samples.data(), &cores3_mic_ring_buffer[cores3_mic_write_pos], tail_len * sizeof(int16_t));
        if (cores3_mic_write_pos > 0) {
            std::memcpy(pcm_samples.data() + tail_len, &cores3_mic_ring_buffer[0], cores3_mic_write_pos * sizeof(int16_t));
        }
    } else {
        std::memcpy(pcm_samples.data(), &cores3_mic_ring_buffer[0], total_available * sizeof(int16_t));
    }
    cores3_mic_write_pos = 0;
    cores3_mic_filled = false;
#else
    if (!mic_capture_active) {
        return "";
    }

    // 1. Copia os últimos 4 segundos (64.000 amostras de 16 bits = 128 KB) do buffer circular do microfone
    // e ESVAZIA o buffer imediatamente para nunca reenviar o mesmo áudio em chamadas futuras!
    {
        std::lock_guard<std::mutex> lock(mic_buffer_mutex);
        size_t total_available = mic_ring_filled ? MIC_RING_BUFFER_SAMPLES : mic_ring_write_pos;
        if (total_available < (MIC_CAPTURE_SAMPLE_RATE / 2U)) {
            // Menos de 0.5s inédito capturado até o momento
            return "";
        }

        pcm_samples.resize(total_available);
        if (mic_ring_filled) {
            size_t tail_len = MIC_RING_BUFFER_SAMPLES - mic_ring_write_pos;
            std::memcpy(pcm_samples.data(),
                        &mic_ring_buffer[mic_ring_write_pos],
                        tail_len * sizeof(int16_t));
            if (mic_ring_write_pos > 0) {
                std::memcpy(pcm_samples.data() + tail_len,
                            &mic_ring_buffer[0],
                            mic_ring_write_pos * sizeof(int16_t));
            }
        } else {
            std::memcpy(pcm_samples.data(),
                        &mic_ring_buffer[0],
                        total_available * sizeof(int16_t));
        }

        // Consome o buffer para impedir loop de reenvio da mesma gravação!
        mic_ring_write_pos = 0;
        mic_ring_filled = false;
    }
#endif

    // 2. Monta o cabeçalho padrão RIFF/WAVE de 44 bytes (16kHz, 16-bit PCM, Mono) em memória
    const uint32_t data_chunk_bytes = static_cast<uint32_t>(pcm_samples.size() * sizeof(int16_t));
    const uint32_t riff_chunk_size = 36U + data_chunk_bytes;
    const uint32_t sample_rate = MIC_CAPTURE_SAMPLE_RATE;
    const uint16_t num_channels = MIC_CAPTURE_CHANNELS;
    const uint16_t bits_per_sample = 16U;
    const uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8U);
    const uint16_t block_align = num_channels * (bits_per_sample / 8U);

    std::vector<uint8_t> wav_bytes(44U + data_chunk_bytes, 0);
    auto writeLe16 = [&wav_bytes](size_t offset, uint16_t val) {
        wav_bytes[offset]     = static_cast<uint8_t>(val & 0xFFU);
        wav_bytes[offset + 1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
    };
    auto writeLe32 = [&wav_bytes](size_t offset, uint32_t val) {
        wav_bytes[offset]     = static_cast<uint8_t>(val & 0xFFU);
        wav_bytes[offset + 1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
        wav_bytes[offset + 2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
        wav_bytes[offset + 3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
    };

    std::memcpy(&wav_bytes[0], "RIFF", 4);
    writeLe32(4, riff_chunk_size);
    std::memcpy(&wav_bytes[8], "WAVE", 4);
    std::memcpy(&wav_bytes[12], "fmt ", 4);
    writeLe32(16, 16U);            // Subchunk1Size (PCM = 16)
    writeLe16(20, 1U);             // AudioFormat (PCM = 1)
    writeLe16(22, num_channels);   // NumChannels (1 = Mono)
    writeLe32(24, sample_rate);    // SampleRate (16000 Hz)
    writeLe32(28, byte_rate);      // ByteRate (32000 B/s)
    writeLe16(32, block_align);    // BlockAlign (2 bytes)
    writeLe16(34, bits_per_sample);// BitsPerSample (16 bits)
    std::memcpy(&wav_bytes[36], "data", 4);
    writeLe32(40, data_chunk_bytes);

    std::memcpy(&wav_bytes[44], pcm_samples.data(), data_chunk_bytes);

    // 3. Arquiva na "Caixa Preta" de Notas de Voz do Cartão SD (sd_card/voice_notes/)
    FILE* last_out = std::fopen("sd_card/voice_notes/voice_last.wav", "wb");
    if (last_out != nullptr) {
        std::fwrite(wav_bytes.data(), 1, wav_bytes.size(), last_out);
        std::fclose(last_out);
    }

    std::time_t now_t = std::time(nullptr);
    std::tm* lt = std::localtime(&now_t);
    char stamp_path[128];
    if (lt != nullptr) {
        std::snprintf(stamp_path, sizeof(stamp_path),
                      "sd_card/voice_notes/voice_%04d%02d%02d_%02d%02d%02d.wav",
                      lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                      lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        std::snprintf(stamp_path, sizeof(stamp_path), "sd_card/voice_notes/voice_note.wav");
    }
    FILE* note_out = std::fopen(stamp_path, "wb");
    if (note_out != nullptr) {
        std::fwrite(wav_bytes.data(), 1, wav_bytes.size(), note_out);
        std::fclose(note_out);
    }

#if __has_include(<dirent.h>)
    // 4. Rotação de retenção de notas de voz: mantém no máximo os últimos MAX_VOICE_NOTES_FILES arquivos
    DIR* notes_dir = opendir("sd_card/voice_notes");
    if (notes_dir != nullptr) {
        std::vector<std::string> voice_note_files;
        struct dirent* entry = nullptr;
        while ((entry = readdir(notes_dir)) != nullptr) {
            std::string fname(entry->d_name);
            if (fname.size() > 10 &&
                fname.rfind("voice_", 0) == 0 &&
                fname != "voice_last.wav" &&
                fname != "voice_note.wav" &&
                fname.substr(fname.size() - 4) == ".wav") {
                voice_note_files.push_back("sd_card/voice_notes/" + fname);
            }
        }
        closedir(notes_dir);

        if (voice_note_files.size() > MAX_VOICE_NOTES_FILES) {
            std::sort(voice_note_files.begin(), voice_note_files.end());
            size_t excess_count = voice_note_files.size() - MAX_VOICE_NOTES_FILES;
            for (size_t i = 0; i < excess_count; ++i) {
                if (std::remove(voice_note_files[i].c_str()) == 0) {
                    M5_LOGI("[HAL-AUDIO] Rotacao de notas de voz: arquivo antigo '%s' removido (limite=%u).",
                            voice_note_files[i].c_str(),
                            static_cast<unsigned int>(MAX_VOICE_NOTES_FILES));
                }
            }
        }
    }
#endif

    M5_LOGI("[HAL-AUDIO] Audio real do usuario gravado (%u amostras / %u bytes WAV) e salvo em sd_card/voice_notes/.",
            static_cast<unsigned int>(pcm_samples.size()),
            static_cast<unsigned int>(wav_bytes.size()));

    return encodeBase64(wav_bytes.data(), wav_bytes.size());
}

std::string HalSensors::captureCameraJpegBase64() const {
#if defined(ARDUINO)
#if __has_include(<esp_camera.h>)
    if (cores3_camera_initialized) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb != nullptr) {
            uint8_t* jpg_buf = nullptr;
            size_t jpg_len = 0;
            bool ok = false;
            if (fb->format == PIXFORMAT_JPEG) {
                std::string b64 = encodeBase64(fb->buf, fb->len);
                esp_camera_fb_return(fb);
                return b64;
            } else if (frame2jpg(fb, 80, &jpg_buf, &jpg_len) && jpg_buf != nullptr && jpg_len > 128) {
                FILE* snap_f = std::fopen(CAM_SNAP_FILE_PATH, "wb");
                if (snap_f != nullptr) {
                    std::fwrite(jpg_buf, 1, jpg_len, snap_f);
                    std::fclose(snap_f);
                }
                std::string b64 = encodeBase64(jpg_buf, jpg_len);
                std::free(jpg_buf);
                esp_camera_fb_return(fb);
                M5_LOGI("[HAL-CORES3] Snapshot JPEG GC0308 capturado (%u bytes) para o Gemini.",
                        static_cast<unsigned int>(jpg_len));
                return b64;
            }
            esp_camera_fb_return(fb);
        }
    }
#endif
    // Fallback em caso de snapshot prévio salvo no SD do CoreS3
    FILE* f = std::fopen(CAM_SNAP_FILE_PATH, "rb");
    if (f != nullptr) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz > 256) {
            std::vector<uint8_t> buf(static_cast<size_t>(sz));
            size_t rd = std::fread(buf.data(), 1, buf.size(), f);
            std::fclose(f);
            if (rd == buf.size()) {
                return encodeBase64(buf.data(), buf.size());
            }
        } else {
            std::fclose(f);
        }
    }
    return "";
#else
    // 1. Verifica se 'cam_snap.jpg' já foi atualizado recentemente pelo script 'scripts/webcam_tracker.py'
    bool has_fresh_snapshot = false;
    struct stat file_stat;
    if (stat(CAM_SNAP_FILE_PATH, &file_stat) == 0 && file_stat.st_size > 256) {
        std::time_t now_epoch = std::time(nullptr);
        if (std::difftime(now_epoch, file_stat.st_mtime) <= 6.0) {
            has_fresh_snapshot = true;
        }
    }

    // 2. Caso o tracker contínuo não esteja rodando, captura 1 frame 320x240 sob demanda
    if (!has_fresh_snapshot) {
        std::remove(CAM_SNAP_FILE_PATH);

        // Tenta primeiro o helper leve em Python/OpenCV (--snap-once) que seleciona automaticamente a webcam 0
        int snap_res = std::system("python scripts/webcam_tracker.py --snap-once >nul 2>&1");

        // Fallback via ffmpeg (DirectShow no Windows / avfoundation no macOS / v4l2 no Linux)
        if (snap_res != 0 || stat(CAM_SNAP_FILE_PATH, &file_stat) != 0 || file_stat.st_size <= 256) {
#if defined(_WIN32)
            snap_res = std::system(
                "ffmpeg -loglevel quiet -f dshow -i video=\"Camera Sensor OV02C10\" "
                "-frames:v 1 -vf scale=320:240 -y cam_snap.jpg >nul 2>&1"
            );
            if (snap_res != 0) {
                std::system(
                    "ffmpeg -loglevel quiet -f dshow -i video=\"Integrated Camera\" "
                    "-frames:v 1 -vf scale=320:240 -y cam_snap.jpg >nul 2>&1"
                );
            }
#elif defined(__APPLE__)
            std::system(
                "ffmpeg -loglevel quiet -f avfoundation -framerate 30 -i \"0\" "
                "-frames:v 1 -vf scale=320:240 -y cam_snap.jpg >/dev/null 2>&1"
            );
#else
            std::system(
                "ffmpeg -loglevel quiet -f v4l2 -i /dev/video0 "
                "-frames:v 1 -vf scale=320:240 -y cam_snap.jpg >/dev/null 2>&1"
            );
#endif
        }
    }

    // 3. Lê o arquivo cam_snap.jpg e codifica em Base64
    std::ifstream jpg_file(CAM_SNAP_FILE_PATH, std::ios::binary | std::ios::ate);
    if (!jpg_file.is_open()) {
        return "";
    }

    std::streamsize jpg_size = jpg_file.tellg();
    if (jpg_size <= 256) {
        return "";
    }

    jpg_file.seekg(0, std::ios::beg);
    std::vector<uint8_t> jpg_bytes(static_cast<size_t>(jpg_size));
    if (!jpg_file.read(reinterpret_cast<char*>(jpg_bytes.data()), jpg_size)) {
        return "";
    }

    M5_LOGI("[HAL-PC] Snapshot JPEG 320x240 da webcam capturado (%u bytes) para o Gemini.",
            static_cast<unsigned int>(jpg_bytes.size()));

    return encodeBase64(jpg_bytes.data(), jpg_bytes.size());
#endif
}

void HalSensors::setFacePresence(bool is_present) {
    gaze_data.face_detected = is_present;
#if !defined(ARDUINO)
    simulated_presence_enabled = is_present;
#endif
}

void HalSensors::setDebugMode(bool is_enabled) {
    debug_mode_enabled = is_enabled;
}

bool HalSensors::isDebugModeEnabled() const {
    return debug_mode_enabled;
}

bool HalSensors::savePetState(const PersistentPetData& save_data) {
#if defined(ARDUINO)
    Preferences nvs_prefs;
    if (!nvs_prefs.begin(PET_NVS_NAMESPACE, false)) {
        return false;
    }
    nvs_prefs.putInt("hunger_level", save_data.hunger_level);
    nvs_prefs.putInt("mood_score", save_data.mood_score);
    nvs_prefs.putInt("energy_level", save_data.energy_level);
    nvs_prefs.putUInt("focus_minutes", save_data.focus_minutes);
    nvs_prefs.end();
    return true;
#else
    FILE* save_file = std::fopen(PET_SAVE_FILE_PATH, "w");
    if (save_file == nullptr) {
        return false;
    }
    std::fprintf(
        save_file,
        "{\n  \"hunger_level\": %d,\n  \"mood_score\": %d,\n  \"energy_level\": %d,\n  \"focus_minutes\": %u\n}\n",
        static_cast<int>(save_data.hunger_level),
        static_cast<int>(save_data.mood_score),
        static_cast<int>(save_data.energy_level),
        static_cast<unsigned int>(save_data.focus_minutes)
    );
    std::fclose(save_file);
    return true;
#endif
}

bool HalSensors::loadPetState(PersistentPetData& out_save_data) {
#if defined(ARDUINO)
    Preferences nvs_prefs;
    if (!nvs_prefs.begin(PET_NVS_NAMESPACE, true)) {
        return false;
    }
    if (!nvs_prefs.isKey("mood_score") && !nvs_prefs.isKey("hunger_level")) {
        nvs_prefs.end();
        return false;
    }
    out_save_data.hunger_level  = nvs_prefs.getInt("hunger_level", 35);
    out_save_data.mood_score    = nvs_prefs.getInt("mood_score", 75);
    out_save_data.energy_level  = nvs_prefs.getInt("energy_level", 90);
    out_save_data.focus_minutes = nvs_prefs.getUInt("focus_minutes", 0);
    nvs_prefs.end();
    return true;
#else
    FILE* save_file = std::fopen(PET_SAVE_FILE_PATH, "r");
    if (save_file == nullptr) {
        return false;
    }
    char file_buffer[512] = {0};
    size_t read_bytes = std::fread(file_buffer, 1, sizeof(file_buffer) - 1, save_file);
    std::fclose(save_file);
    if (read_bytes == 0) {
        return false;
    }
    file_buffer[read_bytes] = '\0';

    int hunger_v = 35;
    int mood_v = 75;
    int energy_v = 90;
    unsigned int focus_v = 0;
    bool any_field_found = false;

#if __has_include(<ArduinoJson.h>)
    JsonDocument save_doc;
    if (deserializeJson(save_doc, file_buffer) == DeserializationError::Ok) {
        if (save_doc["hunger_level"].is<int>()) {
            hunger_v = save_doc["hunger_level"].as<int>();
            any_field_found = true;
        }
        if (save_doc["mood_score"].is<int>()) {
            mood_v = save_doc["mood_score"].as<int>();
            any_field_found = true;
        }
        if (save_doc["energy_level"].is<int>()) {
            energy_v = save_doc["energy_level"].as<int>();
            any_field_found = true;
        }
        if (save_doc["focus_minutes"].is<unsigned int>()) {
            focus_v = save_doc["focus_minutes"].as<unsigned int>();
            any_field_found = true;
        }
    }
#endif

    // Parser resiliente por chave individual (independente da ordem das chaves no JSON)
    if (!any_field_found) {
        auto extractIntByKey = [&](const char* key_name, int& out_val) -> bool {
            const char* key_pos = std::strstr(file_buffer, key_name);
            if (key_pos == nullptr) return false;
            const char* colon_pos = std::strchr(key_pos, ':');
            if (colon_pos == nullptr) return false;
            return (std::sscanf(colon_pos + 1, "%d", &out_val) == 1);
        };
        auto extractUIntByKey = [&](const char* key_name, unsigned int& out_val) -> bool {
            const char* key_pos = std::strstr(file_buffer, key_name);
            if (key_pos == nullptr) return false;
            const char* colon_pos = std::strchr(key_pos, ':');
            if (colon_pos == nullptr) return false;
            return (std::sscanf(colon_pos + 1, "%u", &out_val) == 1);
        };

        if (extractIntByKey("\"hunger_level\"", hunger_v))   any_field_found = true;
        if (extractIntByKey("\"mood_score\"", mood_v))       any_field_found = true;
        if (extractIntByKey("\"energy_level\"", energy_v))   any_field_found = true;
        if (extractUIntByKey("\"focus_minutes\"", focus_v))  any_field_found = true;
    }

    if (any_field_found) {
        out_save_data.hunger_level  = hunger_v;
        out_save_data.mood_score    = mood_v;
        out_save_data.energy_level  = energy_v;
        out_save_data.focus_minutes = focus_v;
        M5_LOGI("[HAL-PC] Save state carregado de '%s' (Fome=%d, Humor=%d, Energia=%d, Foco=%um).",
                PET_SAVE_FILE_PATH, hunger_v, mood_v, energy_v, focus_v);
        return true;
    }
    return false;
#endif
}

bool HalSensors::requestOracleInference(const char* state_json,
                                        char* out_reply_text,
                                        size_t reply_buffer_size,
                                        PetEmotionState& out_target_emotion,
                                        int32_t& out_mood_delta,
                                        int32_t& out_energy_delta,
                                        int32_t* out_hunger_delta,
                                        std::string* out_new_memory,
                                        std::string* out_permanent_fact,
                                        std::string* out_memory_tags) {
#if defined(ARDUINO)
    // Prioriza sd_config.gemini_api_key (sd_card/config.json), com fallback para AI_API_KEY (secrets.h)
    const char* active_api_key = (std::strlen(sd_config.gemini_api_key) > 10 &&
                                  std::strcmp(sd_config.gemini_api_key, "SUA_CHAVE_DE_API_MULTIMODAL_AQUI") != 0)
                                     ? sd_config.gemini_api_key
                                     : AI_API_KEY;

    // No M5Stack CoreS3: utiliza WiFiClientSecure + HTTPClient + ArduinoJson se o Wi-Fi estiver conectado
    if (WiFi.status() == WL_CONNECTED && active_api_key != nullptr &&
        std::strlen(active_api_key) > 10 &&
        std::strcmp(active_api_key, "SUA_CHAVE_DE_API_MULTIMODAL_AQUI") != 0) {
        WiFiClientSecure secure_client;
        secure_client.setInsecure();
        HTTPClient http_client;

        char full_url[256];
        std::snprintf(full_url, sizeof(full_url), "%s?key=%s", AI_API_ENDPOINT, active_api_key);

        if (http_client.begin(secure_client, full_url)) {
            http_client.addHeader("Content-Type", "application/json");
            int http_code = http_client.POST(state_json);
            if (http_code == HTTP_CODE_OK) {
#if __has_include(<ArduinoJson.h>)
                JsonDocument response_doc;
                DeserializationError err = deserializeJson(response_doc, http_client.getString());
                if (!err) {
                    // Extrai primeiro a string JSON interna de candidates[0].content.parts[0].text (padrão generateContent)
                    JsonDocument inner_doc;
                    JsonVariantConst payload_obj;
                    const char* inner_text = response_doc["candidates"][0]["content"]["parts"][0]["text"] | nullptr;
                    if (inner_text != nullptr && deserializeJson(inner_doc, inner_text) == DeserializationError::Ok) {
                        payload_obj = inner_doc.as<JsonVariantConst>();
                    } else {
                        payload_obj = response_doc.as<JsonVariantConst>();
                    }

                    if (payload_obj["reply_text"].is<const char*>()) {
                        std::strncpy(out_reply_text, payload_obj["reply_text"].as<const char*>(), reply_buffer_size - 1);
                        out_reply_text[reply_buffer_size - 1] = '\0';
                        out_mood_delta = payload_obj["mood_delta"] | 0;
                        out_energy_delta = payload_obj["energy_delta"] | 0;

                        int32_t parsed_hunger_delta = payload_obj["hunger_delta"] | 0;
                        const char* parsed_new_memory = payload_obj["new_memory"] | "";
                        const char* parsed_perm_fact = payload_obj["permanent_fact"] | "";
                        const char* parsed_mem_tags = payload_obj["memory_tags"] | "";

                        if (out_hunger_delta != nullptr)   *out_hunger_delta = parsed_hunger_delta;
                        if (out_new_memory != nullptr)     *out_new_memory = parsed_new_memory;
                        if (out_permanent_fact != nullptr) *out_permanent_fact = parsed_perm_fact;
                        if (out_memory_tags != nullptr)    *out_memory_tags = parsed_mem_tags;

                        const char* emo_str = payload_obj["target_emotion"] | "EMOTION_JUDGING";
                        if (std::strcmp(emo_str, "EMOTION_IDLE") == 0)          out_target_emotion = EMOTION_IDLE;
                        else if (std::strcmp(emo_str, "EMOTION_FOCUSED") == 0)  out_target_emotion = EMOTION_FOCUSED;
                        else if (std::strcmp(emo_str, "EMOTION_STARTLED") == 0) out_target_emotion = EMOTION_STARTLED;
                        else if (std::strcmp(emo_str, "EMOTION_SLEEPING") == 0) out_target_emotion = EMOTION_SLEEPING;
                        else if (std::strcmp(emo_str, "EMOTION_JUDGING") == 0)  out_target_emotion = EMOTION_JUDGING;
                        else if (std::strcmp(emo_str, "EMOTION_HAPPY") == 0)    out_target_emotion = EMOTION_HAPPY;
                        else if (std::strcmp(emo_str, "EMOTION_GRUMPY") == 0)   out_target_emotion = EMOTION_GRUMPY;
                        else if (std::strcmp(emo_str, "EMOTION_DIZZY") == 0)    out_target_emotion = EMOTION_DIZZY;
                        else                                                    out_target_emotion = EMOTION_JUDGING;

                        http_client.end();
                        return true;
                    }
                }
#endif
            }
            http_client.end();
        }
    }
    return false;
#else
    (void)state_json;
    (void)out_reply_text;
    (void)reply_buffer_size;
    (void)out_target_emotion;
    (void)out_mood_delta;
    (void)out_energy_delta;
    (void)out_hunger_delta;
    (void)out_new_memory;
    (void)out_permanent_fact;
    (void)out_memory_tags;
    return false;
#endif
}

const GazeTargetData& HalSensors::getGazeData() const {
    return gaze_data;
}

const AudioSensorData& HalSensors::getAudioData() const {
    return audio_data;
}

const EnvironmentSensorData& HalSensors::getEnvironmentData() const {
    return env_data;
}

AudioModeState HalSensors::getAudioBusMode() const {
    return current_audio_mode;
}

const uint8_t* HalSensors::getPipFrameBuffer() const {
    return pip_frame_gray;
}

bool HalSensors::hasValidPipFrame() const {
    return has_valid_pip_frame;
}

const uint8_t* HalSensors::getPipHdFrameBuffer() const {
    return pip_hd_frame_gray;
}

bool HalSensors::hasValidPipHdFrame() const {
    return has_valid_pip_hd_frame;
}

const uint16_t* HalSensors::getPipFrameRgb565Buffer() const {
    return pip_frame_rgb565;
}

const uint16_t* HalSensors::getPipHdFrameRgb565Buffer() const {
    return pip_hd_frame_rgb565;
}

bool HalSensors::hasValidPipRgb565Frame() const {
    return has_valid_pip_rgb565;
}

// =============================================================================
// Implementação Exclusiva para Hardware Real: ESP32-S3 (M5Stack CoreS3)
// =============================================================================
#if defined(ARDUINO)

void HalSensors::initCoreS3Peripherals() {
    // 1. Inicia em modo microfone por padrão para monitorar o ambiente
    M5.Speaker.setVolume(sd_config.speaker_volume);
    M5.Speaker.end();
    M5.Mic.begin();
    current_audio_mode = AUDIO_MODE_MIC;

    // 2. Inicializa sensor integrado de Luz Ambiente (ALS) + Proximidade IR (PS) LTR-553ALS-01 (I2C 0x23)
    constexpr uint8_t LTR553_ADDR = 0x23;
    // Registrador ALS_CONTR (0x80) = 0x01 (Active Mode, Gain 1X)
    // Registrador PS_CONTR  (0x81) = 0x03 (PS Active Mode)
    bool als_ok = M5.In_I2C.writeRegister8(LTR553_ADDR, 0x80, 0x01, 400000U);
    bool ps_ok  = M5.In_I2C.writeRegister8(LTR553_ADDR, 0x81, 0x03, 400000U);
    if (als_ok && ps_ok) {
        cores3_ltr553_initialized = true;
        M5_LOGI("[HAL-CORES3] Sensor optico LTR-553ALS-01 (Lux + Proximidade IR) inicializado em 0x23.");
    }

    // 3. Inicializa câmera frontal integrada GC0308 (QVGA 320x240 RGB565) no M5Stack CoreS3
#if __has_include(<esp_camera.h>)
    camera_config_t cam_cfg;
    std::memset(&cam_cfg, 0, sizeof(cam_cfg));
    cam_cfg.pin_pwdn     = -1;
    cam_cfg.pin_reset    = -1;
    cam_cfg.pin_xclk     = -1;
    cam_cfg.pin_sccb_sda = 12;
    cam_cfg.pin_sccb_scl = 11;
    cam_cfg.pin_d7       = 47;
    cam_cfg.pin_d6       = 48;
    cam_cfg.pin_d5       = 16;
    cam_cfg.pin_d4       = 15;
    cam_cfg.pin_d3       = 42;
    cam_cfg.pin_d2       = 41;
    cam_cfg.pin_d1       = 40;
    cam_cfg.pin_d0       = 39;
    cam_cfg.pin_vsync    = 46;
    cam_cfg.pin_href     = 38;
    cam_cfg.pin_pclk     = 45;
    cam_cfg.xclk_freq_hz = 20000000;
    cam_cfg.ledc_timer   = LEDC_TIMER_0;
    cam_cfg.ledc_channel = LEDC_CHANNEL_0;
    cam_cfg.pixel_format = PIXFORMAT_RGB565;
    cam_cfg.frame_size   = FRAMESIZE_QVGA; // 320x240 nativo
    cam_cfg.jpeg_quality = 12;
    cam_cfg.fb_count     = 2;
    cam_cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cam_cfg.grab_mode    = CAMERA_GRAB_LATEST;

    if (esp_camera_init(&cam_cfg) == ESP_OK) {
        cores3_camera_initialized = true;
        sensor_t* s = esp_camera_sensor_get();
        if (s != nullptr) {
            s->set_hmirror(s, 1);
        }
        M5_LOGI("[HAL-CORES3] Camera frontal GC0308 inicializada em QVGA 320x240 RGB565 (Visao 2.0 Ativa).");
    } else {
        cores3_camera_initialized = false;
        M5_LOGW("[HAL-CORES3] Aviso: Falha ao inicializar GC0308 via esp_camera.");
    }
#endif

    // 4. Conecta ao Wi-Fi priorizando sd_card/config.json com fallback para secrets.h
    const char* active_wifi_ssid = (std::strlen(sd_config.wifi_ssid) > 0 &&
                                    std::strcmp(sd_config.wifi_ssid, "SEU_WIFI_SSID") != 0)
                                       ? sd_config.wifi_ssid
                                       : WIFI_SSID;
    const char* active_wifi_pass = (std::strlen(sd_config.wifi_password) > 0 &&
                                    std::strcmp(sd_config.wifi_password, "SUA_SENHA_WIFI") != 0)
                                       ? sd_config.wifi_password
                                       : WIFI_PASSWORD;
    if (active_wifi_ssid != nullptr && std::strlen(active_wifi_ssid) > 0 &&
        std::strcmp(active_wifi_ssid, "SEU_WIFI_SSID") != 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(active_wifi_ssid, active_wifi_pass);
        M5_LOGI("[HAL-CORES3] Conectando ao Wi-Fi '%s'...", active_wifi_ssid);
    }
}

void HalSensors::readCoreS3CameraGaze() {
    uint32_t now_ms = M5.millis();

    // 1. Verifica toque capacitivo na tela (FT6336U)
    auto touch_detail = M5.Touch.getDetail();
    if (touch_detail.isPressed()) {
        gaze_data.touch_active = true;
        gaze_data.face_detected = true;
        gaze_data.raw_screen_x = touch_detail.x;
        gaze_data.raw_screen_y = touch_detail.y;

        float norm_x = ((static_cast<float>(touch_detail.x) / (SCREEN_WIDTH / 2.0f)) - 1.0f);
        float norm_y = ((static_cast<float>(touch_detail.y) / (SCREEN_HEIGHT / 2.0f)) - 1.0f);
        gaze_data.target_x = fmaxf(-1.0f, fminf(1.0f, norm_x));
        gaze_data.target_y = fmaxf(-1.0f, fminf(1.0f, norm_y));
        return;
    }
    gaze_data.touch_active = false;

    // 2. Processamento embarcado de Visão 2.0 na câmera GC0308 do CoreS3 a ~8 FPS (125ms)
#if __has_include(<esp_camera.h>)
    if (cores3_camera_initialized && (now_ms - last_cores3_cam_capture_ms >= 125U)) {
        last_cores3_cam_capture_ms = now_ms;
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb != nullptr && fb->width == 320 && fb->height == 240 && fb->len >= (320 * 240 * 2)) {
            const uint16_t* rgb565_src = reinterpret_cast<const uint16_t*>(fb->buf);

            // 2a. Atualiza buffers PIP 64x48 e HD 160x120 (Grayscale + RGB565 16-bit nativo)
            uint32_t luma_sum = 0;
            for (int py = 0; py < PIP_FRAME_HEIGHT; ++py) {
                int sy = py * 5; // 48 * 5 = 240
                for (int px = 0; px < PIP_FRAME_WIDTH; ++px) {
                    int sx = px * 5; // 64 * 5 = 320
                    uint16_t raw_px = rgb565_src[sy * 320 + sx];
                    // Byte-swap se big-endian da câmera CSI
                    uint16_t p = static_cast<uint16_t>((raw_px >> 8) | (raw_px << 8));
                    uint8_t r8 = static_cast<uint8_t>(((p >> 11) & 0x1F) << 3);
                    uint8_t g8 = static_cast<uint8_t>(((p >> 5) & 0x3F) << 2);
                    uint8_t b8 = static_cast<uint8_t>((p & 0x1F) << 3);
                    uint8_t y_luma = static_cast<uint8_t>((r8 * 77U + g8 * 150U + b8 * 29U) >> 8);
                    pip_frame_gray[py * PIP_FRAME_WIDTH + px] = y_luma;
                    pip_frame_rgb565[py * PIP_FRAME_WIDTH + px] = p;
                    luma_sum += y_luma;
                }
            }
            has_valid_pip_frame = true;
            has_valid_pip_rgb565 = true;

            for (int hy = 0; hy < PIP_HD_FRAME_HEIGHT; ++hy) {
                int sy = hy * 2; // 120 * 2 = 240
                for (int hx = 0; hx < PIP_HD_FRAME_WIDTH; ++hx) {
                    int sx = hx * 2; // 160 * 2 = 320
                    uint16_t raw_px = rgb565_src[sy * 320 + sx];
                    uint16_t p = static_cast<uint16_t>((raw_px >> 8) | (raw_px << 8));
                    uint8_t r8 = static_cast<uint8_t>(((p >> 11) & 0x1F) << 3);
                    uint8_t g8 = static_cast<uint8_t>(((p >> 5) & 0x3F) << 2);
                    uint8_t b8 = static_cast<uint8_t>((p & 0x1F) << 3);
                    pip_hd_frame_gray[hy * PIP_HD_FRAME_WIDTH + hx] =
                        static_cast<uint8_t>((r8 * 77U + g8 * 150U + b8 * 29U) >> 8);
                    pip_hd_frame_rgb565[hy * PIP_HD_FRAME_WIDTH + hx] = p;
                }
            }
            has_valid_pip_hd_frame = true;

            // 2b. Calcula Mudança de Cena (Scene Change %) e Aceno de Mão Periférico na grade 8x8
            uint32_t diff_acc = 0;
            uint32_t top_corner_motion = 0;
            for (int gy = 0; gy < 8; ++gy) {
                for (int gx = 0; gx < 8; ++gx) {
                    uint8_t cell_luma = pip_frame_gray[(gy * 6 + 3) * PIP_FRAME_WIDTH + (gx * 8 + 4)];
                    int diff = std::abs(static_cast<int>(cell_luma) - static_cast<int>(prev_cores3_luma_grid[gy * 8 + gx]));
                    prev_cores3_luma_grid[gy * 8 + gx] = cell_luma;
                    diff_acc += static_cast<uint32_t>(diff);
                    if (gy < 3 && (gx < 2 || gx >= 6)) {
                        top_corner_motion += static_cast<uint32_t>(diff);
                    }
                }
            }
            uint32_t sc_pct = (diff_acc * 100U) / (64U * 48U);
            if (sc_pct > 100U) sc_pct = 100U;
            gaze_data.scene_change_pct = static_cast<uint8_t>(sc_pct);
            (void)top_corner_motion;
            gaze_data.hand_wave_detected = false;

            // Se o sensor LTR-553ALS não estiver presente, estima Lux pela câmera GC0308
            if (!cores3_ltr553_initialized) {
                uint32_t avg_luma = luma_sum / PIP_FRAME_BYTES;
                env_data.ambient_lux = static_cast<uint16_t>((avg_luma * avg_luma) / 55U);
            }

            // 2c. Segmentação de tom de pele (YCbCr/RGB) no buffer 64x48 para rastrear Rosto + Mão Lateral/Intruso
            int32_t sum_x1 = 0, sum_y1 = 0, count1 = 0;
            int32_t min_x1 = 64, max_x1 = 0;
            int32_t sum_x2 = 0, sum_y2 = 0, count2 = 0;
            int32_t min_x2 = 64, max_x2 = 0, min_y2 = 48, max_y2 = 0;
            uint8_t skin_mask[PIP_FRAME_BYTES] = {0};

            for (int py = 4; py < PIP_FRAME_HEIGHT - 4; ++py) {
                for (int px = 4; px < PIP_FRAME_WIDTH - 4; ++px) {
                    uint16_t p = pip_frame_rgb565[py * PIP_FRAME_WIDTH + px];
                    int r = ((p >> 11) & 0x1F) << 3;
                    int g = ((p >> 5) & 0x3F) << 2;
                    int b = (p & 0x1F) << 3;
                    // Regra rápida de crominância de pele humana (R > 85, G > 40, B > 20, R > G, R > B, R - G >= 15)
                    bool is_skin = (r > 85 && g > 40 && b > 25 && r > g && g > b - 10 && (r - g) >= 14 && (r - b) >= 22);
                    if (is_skin) {
                        skin_mask[py * PIP_FRAME_WIDTH + px] = 1;
                        if (count1 == 0 || std::abs(px - (sum_x1 / count1)) <= 18) {
                            sum_x1 += px;
                            sum_y1 += py;
                            ++count1;
                            if (px < min_x1) min_x1 = px;
                            if (px > max_x1) max_x1 = px;
                        } else {
                            sum_x2 += px;
                            sum_y2 += py;
                            ++count2;
                            if (px < min_x2) min_x2 = px;
                            if (px > max_x2) max_x2 = px;
                            if (py < min_y2) min_y2 = py;
                            if (py > max_y2) max_y2 = py;
                        }
                    }
                }
            }

            bool raw_slouch = false;
            bool raw_close = false;
            if (count1 >= 18) {
                gaze_data.face_detected = true;
                int32_t cx_pip = sum_x1 / count1;
                int32_t cy_pip = sum_y1 / count1;
                gaze_data.raw_screen_x = (cx_pip * SCREEN_WIDTH) / PIP_FRAME_WIDTH;
                gaze_data.raw_screen_y = (cy_pip * SCREEN_HEIGHT) / PIP_FRAME_HEIGHT;
                int32_t fw_pip = (max_x1 > min_x1) ? (max_x1 - min_x1 + 1) : 12;
                gaze_data.face_width_px = fw_pip * 5;
                gaze_data.head_yaw_deg = ((static_cast<float>(gaze_data.raw_screen_x) - 160.0f) / 160.0f) * 30.0f;
                raw_slouch = (gaze_data.raw_screen_y > 165);
                raw_close = (gaze_data.face_width_px > 125);

                // 2d. Detecção Embarcada Estrita de Gestos de Mão Lateral APENAS para Joinha 👍 e Palma Aberta ✋ no GC0308
                if (sc_pct <= 25U && count2 >= 12 && max_y2 > min_y2 + 4 && max_x2 > min_x2 + 3) {
                    int max_top_runs = 0;
                    int top_skin_cols = 0;
                    int top_end_y = (min_y2 + 3 <= max_y2) ? (min_y2 + 3) : max_y2;
                    for (int hy = min_y2; hy <= top_end_y; ++hy) {
                        int runs = 0;
                        bool in_run = false;
                        for (int hx = min_x2; hx <= max_x2; ++hx) {
                            if (skin_mask[hy * PIP_FRAME_WIDTH + hx]) {
                                ++top_skin_cols;
                                if (!in_run) {
                                    ++runs;
                                    in_run = true;
                                }
                            } else {
                                in_run = false;
                            }
                        }
                        if (runs > max_top_runs) max_top_runs = runs;
                    }
                    int hand_w = max_x2 - min_x2 + 1;
                    int hand_h = max_y2 - min_y2 + 1;
                    if (max_top_runs == 1 && (hand_h * 3) >= (hand_w * 4) && top_skin_cols <= (hand_w + 2)) {
                        thumb_active_until_ms = now_ms + 900U;
                    } else if (max_top_runs >= 4 && count2 >= 42) {
                        palm_active_until_ms = now_ms + 900U;
                    }
                }

                if (count2 >= 14) {
                    gaze_data.face_count = 2;
                    gaze_data.secondary_face_x = ((sum_x2 / count2) * SCREEN_WIDTH) / PIP_FRAME_WIDTH;
                    gaze_data.secondary_face_y = ((sum_y2 / count2) * SCREEN_HEIGHT) / PIP_FRAME_HEIGHT;
                } else {
                    gaze_data.face_count = 1;
                    gaze_data.secondary_face_x = -1;
                    gaze_data.secondary_face_y = -1;
                }
            } else {
                gaze_data.face_detected = false;
                gaze_data.face_count = 0;
                gaze_data.secondary_face_x = -1;
                gaze_data.secondary_face_y = -1;
            }

            esp_camera_fb_return(fb);
            updateSharedVisionCalibrationAndGestures(now_ms, raw_slouch, raw_close);
        }
    }
#endif

    // Normaliza coordenadas [-1.0, +1.0]
    if (gaze_data.face_detected) {
        float norm_x = (static_cast<float>(gaze_data.raw_screen_x) / (SCREEN_WIDTH / 2.0f)) - 1.0f;
        float norm_y = (static_cast<float>(gaze_data.raw_screen_y) / (SCREEN_HEIGHT / 2.0f)) - 1.0f;
        gaze_data.target_x = fmaxf(-1.0f, fminf(1.0f, norm_x));
        gaze_data.target_y = fmaxf(-1.0f, fminf(1.0f, norm_y));
    } else {
        gaze_data.target_x = 0.0f;
        gaze_data.target_y = 0.25f;
    }
}

void HalSensors::readCoreS3Microphone() {
    uint32_t current_ms = M5.millis();
    audio_data.is_loud_peak = false;
    audio_data.interaction_triggered = false;
    audio_data.vad_phrase_completed = false;
    audio_data.double_clap_detected = false;
    audio_data.whistle_detected = false;
    audio_data.silence_toggle_kbd_triggered = false;
    audio_data.pomodoro_toggle_triggered = false;
    audio_data.gallery_timelapse_triggered = false;
    audio_data.ambient_audio_toggle_triggered = false;
    audio_data.voice_note_replay_triggered = false;
    audio_data.theme_cycle_triggered = false;
    audio_data.pip_zoom_toggle_triggered = false;
    audio_data.rpg_modal_toggle_triggered = false;
    audio_data.calibrate_posture_triggered = false;
    audio_data.pip_color_mode_triggered = false;
    audio_data.object_scan_triggered = false;
    audio_data.rain_preview_toggle_triggered = false;

    if (current_audio_mode != AUDIO_MODE_MIC) {
        return;
    }

    int16_t pcm_buf[256];
    if (M5.Mic.record(pcm_buf, 256, MIC_CAPTURE_SAMPLE_RATE)) {
        // Alimenta o ring buffer de 4s do CoreS3 para envio real de WAV ao Gemini Multimodal
        if (current_ms >= mic_echo_guard_until_ms) {
            for (size_t i = 0; i < 256; ++i) {
                cores3_mic_ring_buffer[cores3_mic_write_pos] = pcm_buf[i];
                ++cores3_mic_write_pos;
                if (cores3_mic_write_pos >= MIC_RING_BUFFER_SAMPLES) {
                    cores3_mic_write_pos = 0;
                    cores3_mic_filled = true;
                }
            }
        }

        double sum_sq = 0.0;
        for (size_t i = 0; i < 256; ++i) {
            double s = static_cast<double>(pcm_buf[i]);
            sum_sq += s * s;
        }
        double rms = std::sqrt(sum_sq / 256.0);
        float computed_db = NOISE_FLOOR_DB;
        if (rms > 1.0) {
            float raw_db = 20.0f * std::log10(static_cast<float>(rms));
            computed_db = 30.0f + (raw_db - 24.0f) * 1.12f;
            if (computed_db < NOISE_FLOOR_DB) computed_db = NOISE_FLOOR_DB;
            if (computed_db > MAX_SPL_DB)     computed_db = MAX_SPL_DB;
        }

        if (current_ms >= mic_echo_guard_until_ms) {
            analyzeSharedPcmBlockDsp(pcm_buf, 256, computed_db, rms, current_ms);
        }

        audio_data.noise_level_db = (audio_data.noise_level_db * 0.55f) + (computed_db * 0.45f);
        audio_data.adaptive_noise_floor_db = dsp_noise_floor_db;
        audio_data.spectral_zcr = dsp_latest_zcr;
        audio_data.is_typing_cadence = (current_ms < dsp_typing_active_until_ms);
        audio_data.music_beat_active = (current_ms < dsp_music_active_until_ms);
        if (audio_data.music_beat_active && dsp_beat_interval_ms > 150U) {
            audio_data.music_beat_phase = static_cast<float>(current_ms % dsp_beat_interval_ms) /
                                          static_cast<float>(dsp_beat_interval_ms);
        } else {
            audio_data.music_beat_phase = 0.0f;
        }

        if (dsp_double_clap_latched) {
            audio_data.double_clap_detected = true;
            dsp_double_clap_latched = false;
            vad_active_speech_frames = 0;
            vad_trailing_silence_frames = 0;
            audio_data.vad_speech_active = false;
        }
        if (dsp_whistle_latched) {
            audio_data.whistle_detected = true;
            dsp_whistle_latched = false;
        }

        if (audio_data.noise_level_db >= 85.0f && !audio_data.double_clap_detected && (current_ms - last_loud_peak_ms > 1200U)) {
            audio_data.is_loud_peak = true;
            audio_data.last_event = AUDIO_EVENT_LOUD_NOISE;
            last_loud_peak_ms = current_ms;
        }

        const float vad_thresh_db = fmaxf(44.5f, dsp_noise_floor_db + 10.5f);
        if (audio_data.vad_enabled && !audio_data.is_continuous_speech) {
            bool is_voice_frame = (audio_data.noise_level_db >= vad_thresh_db && audio_data.noise_level_db < 84.0f) &&
                                  (!audio_data.is_typing_cadence || dsp_voiced_formant_frames >= 6U);
            if (is_voice_frame) {
                vad_active_speech_frames++;
                vad_trailing_silence_frames = 0;
                if (vad_active_speech_frames >= 18U && vad_active_speech_frames <= 215U) {
                    audio_data.vad_speech_active = true;
                }
            } else if (vad_active_speech_frames >= 20U && vad_active_speech_frames <= 210U) {
                vad_trailing_silence_frames++;
                if (vad_trailing_silence_frames >= 45U && (current_ms - last_vad_trigger_ms > 8000U)) {
                    bool facing_bicho = gaze_data.face_detected && (std::fabs(gaze_data.head_yaw_deg) <= 24.0f);
                    bool kw_ok = !audio_data.vad_keyword_mode ||
                                 dsp_wake_word_latched ||
                                 (facing_bicho && dsp_voiced_formant_frames >= 8U && dsp_syllable_peaks >= 2U);
                    audio_data.wake_word_detected = dsp_wake_word_latched || (facing_bicho && dsp_syllable_peaks >= 2U);
                    if (kw_ok && dsp_voiced_formant_frames >= 5U) {
                        audio_data.vad_phrase_completed = true;
                        audio_data.vad_speech_active = false;
                        audio_data.last_event = AUDIO_EVENT_VOICE_COMMAND;
                        last_vad_trigger_ms = current_ms;
                    }
                    vad_active_speech_frames = 0;
                    vad_trailing_silence_frames = 0;
                    dsp_kw_phonetic_stage = 0;
                    dsp_voiced_formant_frames = 0;
                    dsp_syllable_peaks = 0;
                    dsp_wake_word_latched = false;
                }
            } else {
                vad_active_speech_frames = 0;
                vad_trailing_silence_frames = 0;
                audio_data.vad_speech_active = false;
                dsp_kw_phonetic_stage = 0;
                dsp_voiced_formant_frames = 0;
                dsp_syllable_peaks = 0;
                dsp_wake_word_latched = false;
            }
        }
    }
}

void HalSensors::readCoreS3OnboardSensors() {
    uint32_t now_ms = M5.millis();
    if (M5.Imu.update()) {
        M5.Imu.getAccel(&env_data.accel_x, &env_data.accel_y, &env_data.accel_z);
    }
    env_data.battery_percent = static_cast<uint8_t>(M5.Power.getBatteryLevel());
    env_data.is_charging = M5.Power.isCharging();

    // Leitura a 5Hz do sensor óptico integrado LTR-553ALS-01 (I2C 0x23: Lux real + Proximidade IR para objetos)
    if (cores3_ltr553_initialized && (now_ms - last_cores3_ltr_read_ms >= 200U)) {
        last_cores3_ltr_read_ms = now_ms;
        constexpr uint8_t LTR553_ADDR = 0x23;
        uint8_t als_regs[4] = {0};
        uint8_t ps_regs[2]  = {0};
        if (M5.In_I2C.readRegister(LTR553_ADDR, 0x88, als_regs, 4, 400000U)) {
            uint16_t ch1 = static_cast<uint16_t>(als_regs[0] | (als_regs[1] << 8));
            uint16_t ch0 = static_cast<uint16_t>(als_regs[2] | (als_regs[3] << 8));
            env_data.ambient_lux = static_cast<uint16_t>((ch0 + ch1) / 2U);
        }
        if (M5.In_I2C.readRegister(LTR553_ADDR, 0x8D, ps_regs, 2, 400000U)) {
            uint16_t ps_val = static_cast<uint16_t>(ps_regs[0] | ((ps_regs[1] & 0x07U) << 8));
            env_data.proximity_raw = ps_val;
            // Objeto segurado próximo ao sensor IR frontal do CoreS3 (caneca, garrafa, placa)
            gaze_data.object_held_close = (ps_val >= 180U);
        }
    }

    // Telemetria interna do ESP32-S3 (SRAM usada) + leitura opcional de telemetria/visão do PC via USB Serial
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t total_heap = ESP.getHeapSize();
    if (total_heap > 0) {
        env_data.ram_usage_percent = static_cast<uint8_t>(100U - ((free_heap * 100U) / total_heap));
    }

    // Suporta tanto "TELEM:<cpu>:<ram>:<window>" quanto "VISION:<campos de face_track.txt + gestos>" via USB Serial
    if (Serial.available() > 0) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.startsWith("TELEM:")) {
            int first_colon = line.indexOf(':', 6);
            int second_colon = (first_colon > 0) ? line.indexOf(':', first_colon + 1) : -1;
            if (first_colon > 6 && second_colon > first_colon) {
                env_data.cpu_usage_percent = static_cast<uint8_t>(line.substring(6, first_colon).toInt());
                env_data.ram_usage_percent = static_cast<uint8_t>(line.substring(first_colon + 1, second_colon).toInt());
                String win_title = line.substring(second_colon + 1);
                std::snprintf(env_data.active_window_title, sizeof(env_data.active_window_title), "%s", win_title.c_str());
            }
        } else if (line.startsWith("VISION:")) {
            int det = 0, fx = 160, fy = 120, sl = 0, cl = 0, sm = 0, ld = 0, hw = 0, fc = 1, sx = -1, sy = -1, fw = 62, yw = 0, ob = 0, lx = 240, sc = 0;
            int shhh_s = 0, thumb_s = 0, vict_s = 0, palm_s = 0;
            float tilt = 0.0f, yaw = 0.0f;
            int n = std::sscanf(line.c_str() + 7,
                                "%d %d %d %f %d %d %d %d %d %d %d %d %f %d %d %d %d %d %d %d %d %d",
                                &det, &fx, &fy, &tilt, &sl, &cl, &sm, &ld, &hw, &fc, &sx, &sy, &yaw, &fw, &yw, &ob, &lx, &sc,
                                &shhh_s, &thumb_s, &vict_s, &palm_s);
            if (n >= 9) {
                gaze_data.face_detected = (det != 0);
                gaze_data.raw_screen_x = fx;
                gaze_data.raw_screen_y = fy;
                gaze_data.head_tilt_deg = tilt;
                gaze_data.is_smiling = (sm != 0);
                gaze_data.is_looking_down_phone = (ld != 0);
                gaze_data.hand_wave_detected = (hw != 0);
                if (n >= 16) {
                    gaze_data.face_count = static_cast<uint8_t>(fc);
                    gaze_data.secondary_face_x = sx;
                    gaze_data.secondary_face_y = sy;
                    gaze_data.head_yaw_deg = yaw;
                    gaze_data.face_width_px = fw;
                    gaze_data.is_yawning = (yw != 0);
                    gaze_data.object_held_close = (ob != 0);
                }
                if (n >= 18) {
                    env_data.ambient_lux = static_cast<uint16_t>(lx);
                    gaze_data.scene_change_pct = static_cast<uint8_t>(sc);
                }
                if (n >= 22) {
                    (void)shhh_s;
                    (void)thumb_s;
                    (void)vict_s;
                    if (palm_s != 0) palm_active_until_ms = now_ms + 950U;
                }
                updateSharedVisionCalibrationAndGestures(now_ms, sl != 0, cl != 0);
            }
        }
    }
}

#else
// =============================================================================
// Implementação Exclusiva para Emulador Desktop PC (SDL2 + Mic Real + Webcam Real)
// =============================================================================

void HalSensors::sdlAudioCaptureCallback(void* userdata, uint8_t* stream, int len) {
    if (userdata == nullptr || stream == nullptr || len <= 0) {
        return;
    }
    HalSensors* self = static_cast<HalSensors*>(userdata);
    const int16_t* pcm_samples = reinterpret_cast<const int16_t*>(stream);
    size_t sample_count = static_cast<size_t>(len) / sizeof(int16_t);
    self->processCapturedAudioSamples(pcm_samples, sample_count);
}

void HalSensors::processCapturedAudioSamples(const int16_t* samples, size_t sample_count) {
    if (samples == nullptr || sample_count == 0) {
        return;
    }

    // 1. Calcula o volume RMS instantâneo do bloco de amostras de 16 bits
    double sum_squares = 0.0;
    for (size_t i = 0; i < sample_count; ++i) {
        double s = static_cast<double>(samples[i]);
        sum_squares += s * s;
    }
    double rms_raw = std::sqrt(sum_squares / static_cast<double>(sample_count));

    // Converte RMS (0 a 32768) para escala de decibéis ambientais (30.0 dB a 100.0 dB)
    float computed_db = NOISE_FLOOR_DB;
    if (rms_raw > 1.0) {
        // 20 * log10(32768) ≈ 90.3 dB -> mapeia [25 dB_raw .. 86 dB_raw] para [30 dB .. 100 dB]
        float raw_db = 20.0f * std::log10(static_cast<float>(rms_raw));
        computed_db = 30.0f + (raw_db - 24.0f) * 1.12f;
        if (computed_db < NOISE_FLOOR_DB) computed_db = NOISE_FLOOR_DB;
        if (computed_db > MAX_SPL_DB)     computed_db = MAX_SPL_DB;
    }

    // 2. Grava amostras no buffer circular de 4 segundos (16kHz Mono) e executa o DSP do Ouvido 2.0
    // quando o bicho não está falando no speaker e fora da janela de guarda contra eco acústico (2.5s pós-TTS)
    uint32_t now_ms = M5.millis();
    std::lock_guard<std::mutex> lock(mic_buffer_mutex);
    if (current_audio_mode == AUDIO_MODE_MIC && now_ms >= mic_echo_guard_until_ms) {
        for (size_t i = 0; i < sample_count; ++i) {
            mic_ring_buffer[mic_ring_write_pos] = samples[i];
            ++mic_ring_write_pos;
            if (mic_ring_write_pos >= MIC_RING_BUFFER_SAMPLES) {
                mic_ring_write_pos = 0;
                mic_ring_filled = true;
            }
        }
        analyzeSharedPcmBlockDsp(samples, sample_count, computed_db, rms_raw, now_ms);
        // Suavização exponencial rápida para animar os VU meters laterais com responsividade
        realtime_mic_db = (realtime_mic_db * 0.45f) + (computed_db * 0.55f);
    } else {
        // Atenua leitura enquanto o próprio alto-falante do pet está falando ou em guarda anti-eco
        realtime_mic_db = NOISE_FLOOR_DB + 2.0f;
    }
}

void HalSensors::updateWebcamPipBuffer(uint32_t now_ms) {
    if (now_ms - last_pip_read_ms < 180) {
        return;
    }
    last_pip_read_ms = now_ms;

    bool read_pip_ok = false;
    FILE* pip_file = std::fopen(CAM_PIP_FILE_PATH, "rb");
    if (pip_file != nullptr) {
        size_t bytes_read = std::fread(pip_frame_gray, 1, PIP_FRAME_BYTES, pip_file);
        std::fclose(pip_file);
        if (bytes_read == PIP_FRAME_BYTES) {
            has_valid_pip_frame = true;
            read_pip_ok = true;
        }
    }

    if (!read_pip_ok) {
        // Fallback sintético estilo radar térmico caso cam_pip.raw ainda não exista
        int cx = gaze_data.raw_screen_x * PIP_FRAME_WIDTH / SCREEN_WIDTH;
        int cy = gaze_data.raw_screen_y * PIP_FRAME_HEIGHT / SCREEN_HEIGHT;
        uint8_t phase = static_cast<uint8_t>((now_ms / 40) & 0xFF);
        for (int y = 0; y < PIP_FRAME_HEIGHT; ++y) {
            for (int x = 0; x < PIP_FRAME_WIDTH; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                int dist2 = dx * dx + dy * dy;
                uint8_t base_val = static_cast<uint8_t>(18 + ((y * 3 + phase) % 24));
                if (gaze_data.face_detected && dist2 < 120) {
                    int blob = 210 - dist2 * 1;
                    if (blob > base_val) base_val = static_cast<uint8_t>(blob);
                }
                pip_frame_gray[y * PIP_FRAME_WIDTH + x] = base_val;
            }
        }
        has_valid_pip_frame = true;
    }

    // Lê também o frame HD 160x120 (cam_pip_hd.raw) com anti-aliasing da webcam;
    // caso ainda não exista, faz upsampling bilinear suave a partir do buffer 64x48
    bool read_hd_ok = false;
    FILE* hd_file = std::fopen(CAM_PIP_HD_FILE_PATH, "rb");
    if (hd_file != nullptr) {
        size_t hd_bytes = std::fread(pip_hd_frame_gray, 1, PIP_HD_FRAME_BYTES, hd_file);
        std::fclose(hd_file);
        if (hd_bytes == PIP_HD_FRAME_BYTES) {
            has_valid_pip_hd_frame = true;
            read_hd_ok = true;
        }
    }

    if (!read_hd_ok && has_valid_pip_frame) {
        // Interpolação bilinear de 64x48 -> 160x120 para eliminar serrilhado
        for (int hy = 0; hy < PIP_HD_FRAME_HEIGHT; ++hy) {
            float src_y = (static_cast<float>(hy) * static_cast<float>(PIP_FRAME_HEIGHT - 1)) / static_cast<float>(PIP_HD_FRAME_HEIGHT - 1);
            int y0 = static_cast<int>(src_y);
            int y1 = (y0 + 1 < PIP_FRAME_HEIGHT) ? (y0 + 1) : y0;
            float fy = src_y - static_cast<float>(y0);

            for (int hx = 0; hx < PIP_HD_FRAME_WIDTH; ++hx) {
                float src_x = (static_cast<float>(hx) * static_cast<float>(PIP_FRAME_WIDTH - 1)) / static_cast<float>(PIP_HD_FRAME_WIDTH - 1);
                int x0 = static_cast<int>(src_x);
                int x1 = (x0 + 1 < PIP_FRAME_WIDTH) ? (x0 + 1) : x0;
                float fx = src_x - static_cast<float>(x0);

                float p00 = static_cast<float>(pip_frame_gray[y0 * PIP_FRAME_WIDTH + x0]);
                float p10 = static_cast<float>(pip_frame_gray[y0 * PIP_FRAME_WIDTH + x1]);
                float p01 = static_cast<float>(pip_frame_gray[y1 * PIP_FRAME_WIDTH + x0]);
                float p11 = static_cast<float>(pip_frame_gray[y1 * PIP_FRAME_WIDTH + x1]);

                float top = p00 + fx * (p10 - p00);
                float bot = p01 + fx * (p11 - p01);
                float val = top + fy * (bot - top);
                if (val < 0.0f) val = 0.0f;
                if (val > 255.0f) val = 255.0f;
                pip_hd_frame_gray[hy * PIP_HD_FRAME_WIDTH + hx] = static_cast<uint8_t>(val + 0.5f);
            }
        }
        has_valid_pip_hd_frame = true;
    }

    // Lê os buffers coloridos 16-bit RGB565 (64x48 e 160x120) gerados pelo webcam_tracker.py
    bool read_rgb_ok = false;
    FILE* rgb_file = std::fopen(CAM_PIP_RGB565_FILE_PATH, "rb");
    if (rgb_file != nullptr) {
        size_t words_read = std::fread(pip_frame_rgb565, sizeof(uint16_t), PIP_FRAME_BYTES, rgb_file);
        std::fclose(rgb_file);
        if (words_read == PIP_FRAME_BYTES) {
            read_rgb_ok = true;
            has_valid_pip_rgb565 = true;
        }
    }

    bool read_hd_rgb_ok = false;
    FILE* hd_rgb_file = std::fopen(CAM_PIP_HD_RGB565_FILE_PATH, "rb");
    if (hd_rgb_file != nullptr) {
        size_t hd_words = std::fread(pip_hd_frame_rgb565, sizeof(uint16_t), PIP_HD_FRAME_BYTES, hd_rgb_file);
        std::fclose(hd_rgb_file);
        if (hd_words == PIP_HD_FRAME_BYTES) {
            read_hd_rgb_ok = true;
        }
    }

    if (!read_rgb_ok && has_valid_pip_frame) {
        for (size_t i = 0; i < PIP_FRAME_BYTES; ++i) {
            uint8_t lum = pip_frame_gray[i];
            pip_frame_rgb565[i] = static_cast<uint16_t>(((lum >> 3) << 11) | ((lum >> 2) << 5) | (lum >> 3));
        }
        has_valid_pip_rgb565 = true;
    }
    if (!read_hd_rgb_ok && has_valid_pip_hd_frame) {
        for (size_t i = 0; i < PIP_HD_FRAME_BYTES; ++i) {
            uint8_t lum = pip_hd_frame_gray[i];
            pip_hd_frame_rgb565[i] = static_cast<uint16_t>(((lum >> 3) << 11) | ((lum >> 2) << 5) | (lum >> 3));
        }
    }
}

bool HalSensors::readWebcamTrackerFile(int32_t& out_face_x, int32_t& out_face_y, bool& out_face_detected) {
    struct stat track_stat;
    if (stat(FACE_TRACK_FILE_PATH, &track_stat) != 0) {
        return false;
    }

    // Verifica pelo mtime do sistema de arquivos se o tracker atualizou face_track.txt nos últimos 4 segundos
    std::time_t now_epoch = std::time(nullptr);
    if (std::fabs(std::difftime(now_epoch, track_stat.st_mtime)) > 4.0) {
        return false;
    }

    FILE* track_file = std::fopen(FACE_TRACK_FILE_PATH, "r");
    if (track_file == nullptr) {
        // Arquivo momentaneamente aberto para escrita pelo Python: mantém última posição válida
        out_face_detected = gaze_data.face_detected;
        out_face_x = gaze_data.raw_screen_x;
        out_face_y = gaze_data.raw_screen_y;
        return true;
    }

    char line1[320] = {0};
    char line2[128] = {0};
    bool has_line1 = (std::fgets(line1, sizeof(line1), track_file) != nullptr);
    bool has_line2 = (std::fgets(line2, sizeof(line2), track_file) != nullptr);
    std::fclose(track_file);

    if (!has_line1) {
        out_face_detected = gaze_data.face_detected;
        out_face_x = gaze_data.raw_screen_x;
        out_face_y = gaze_data.raw_screen_y;
        return true;
    }

    long long timestamp_sec = 0;
    int detected_flag = 0;
    int fx = SCREEN_WIDTH / 2;
    int fy = SCREEN_HEIGHT / 2;
    float tilt_deg = 0.0f;
    int slouch_flag = 0;
    int close_flag = 0;
    int cpu_pct = static_cast<int>(env_data.cpu_usage_percent);
    int ram_pct = static_cast<int>(env_data.ram_usage_percent);
    int smile_flag = 0;
    int looking_down_flag = 0;
    int hand_wave_flag = 0;
    int git_dirty = static_cast<int>(env_data.git_modified_files);
    // Novos campos Visão 2.0 + Gestos de Mão (retrocompatíveis)
    int face_count_v = 1;
    int sec_x_v = -1;
    int sec_y_v = -1;
    float yaw_deg_v = 0.0f;
    int face_w_v = 62;
    int yawn_flag = 0;
    int object_close_flag = 0;
    int ambient_lux_v = static_cast<int>(webcam_ambient_lux);
    int scene_change_v = 0;
    int shhh_v = 0;
    int thumb_v = 0;
    int victory_v = 0;
    int palm_v = 0;

    int parsed = std::sscanf(
        line1,
        "%lld %d %d %d %f %d %d %d %d %d %d %d %d %d %d %d %f %d %d %d %d %d %d %d %d %d",
        &timestamp_sec,
        &detected_flag,
        &fx,
        &fy,
        &tilt_deg,
        &slouch_flag,
        &close_flag,
        &cpu_pct,
        &ram_pct,
        &smile_flag,
        &looking_down_flag,
        &hand_wave_flag,
        &git_dirty,
        &face_count_v,
        &sec_x_v,
        &sec_y_v,
        &yaw_deg_v,
        &face_w_v,
        &yawn_flag,
        &object_close_flag,
        &ambient_lux_v,
        &scene_change_v,
        &shhh_v,
        &thumb_v,
        &victory_v,
        &palm_v
    );

    if (parsed < 4) {
        // Leitura parcial durante truncamento: preserva a última coordenada conhecida sem cair para o mouse
        out_face_detected = gaze_data.face_detected;
        out_face_x = gaze_data.raw_screen_x;
        out_face_y = gaze_data.raw_screen_y;
        return true;
    }

    out_face_detected = (detected_flag != 0);
    out_face_x = (fx < 0) ? 0 : ((fx >= SCREEN_WIDTH) ? (SCREEN_WIDTH - 1) : fx);
    out_face_y = (fy < 0) ? 0 : ((fy >= SCREEN_HEIGHT) ? (SCREEN_HEIGHT - 1) : fy);

    if (parsed >= 7) {
        if (out_face_detected) {
            gaze_data.head_tilt_deg = gaze_data.head_tilt_deg * 0.6f + tilt_deg * 0.4f;
        } else {
            gaze_data.head_tilt_deg *= 0.85f;
        }
    }

    if (parsed >= 9) {
        if (cpu_pct < 0) cpu_pct = 0;
        if (cpu_pct > 100) cpu_pct = 100;
        if (ram_pct < 0) ram_pct = 0;
        if (ram_pct > 100) ram_pct = 100;
        env_data.cpu_usage_percent = static_cast<uint8_t>(cpu_pct);
        env_data.ram_usage_percent = static_cast<uint8_t>(ram_pct);
    }

    if (parsed >= 12) {
        gaze_data.is_smiling = out_face_detected && (smile_flag != 0);
        gaze_data.is_looking_down_phone = out_face_detected && (looking_down_flag != 0);
        gaze_data.hand_wave_detected = (hand_wave_flag != 0);
    } else {
        gaze_data.is_smiling = false;
        gaze_data.is_looking_down_phone = false;
        gaze_data.hand_wave_detected = false;
    }

    if (parsed >= 13) {
        if (git_dirty < 0) git_dirty = 0;
        if (git_dirty > 255) git_dirty = 255;
        env_data.git_modified_files = static_cast<uint8_t>(git_dirty);
    }

    if (parsed >= 20) {
        if (face_count_v < 0) face_count_v = 0;
        if (face_count_v > 10) face_count_v = 10;
        gaze_data.face_count = out_face_detected ? static_cast<uint8_t>(face_count_v) : 0U;
        gaze_data.secondary_face_x = (gaze_data.face_count >= 2) ? sec_x_v : -1;
        gaze_data.secondary_face_y = (gaze_data.face_count >= 2) ? sec_y_v : -1;
        gaze_data.head_yaw_deg = out_face_detected ? (gaze_data.head_yaw_deg * 0.55f + yaw_deg_v * 0.45f) : 0.0f;
        gaze_data.face_width_px = (face_w_v > 0) ? face_w_v : 62;
        gaze_data.is_yawning = out_face_detected && (yawn_flag != 0);
        gaze_data.object_held_close = (object_close_flag != 0);
    } else {
        gaze_data.face_count = out_face_detected ? 1U : 0U;
        gaze_data.secondary_face_x = -1;
        gaze_data.secondary_face_y = -1;
    }

    if (parsed >= 22) {
        if (ambient_lux_v < 1) ambient_lux_v = 1;
        if (ambient_lux_v > 2000) ambient_lux_v = 2000;
        webcam_ambient_lux = static_cast<uint16_t>(ambient_lux_v);
        if (scene_change_v < 0) scene_change_v = 0;
        if (scene_change_v > 100) scene_change_v = 100;
        gaze_data.scene_change_pct = static_cast<uint8_t>(scene_change_v);
    }

    uint32_t now_ms = M5.millis();
    if (parsed >= 26) {
        (void)thumb_v;
        (void)victory_v;
        (void)shhh_v;
        if (palm_v != 0) {
            palm_active_until_ms = now_ms + 950U;
            shhh_active_until_ms = 0;
        }
    }

    // Executa o motor C++ compartilhado (PC + CoreS3) para calibração postural e gestos
    gaze_data.face_detected = out_face_detected && simulated_presence_enabled;
    if (gaze_data.face_detected) {
        gaze_data.raw_screen_x = out_face_x;
        gaze_data.raw_screen_y = out_face_y;
    }
    updateSharedVisionCalibrationAndGestures(now_ms, slouch_flag != 0, close_flag != 0);

    if (has_line2) {
        // Remove quebras de linha ao final do título da janela ativa
        size_t len = std::strlen(line2);
        while (len > 0 && (line2[len - 1] == '\n' || line2[len - 1] == '\r')) {
            line2[len - 1] = '\0';
            --len;
        }
        if (len > 0) {
            std::snprintf(env_data.active_window_title, sizeof(env_data.active_window_title), "%s", line2);
        }
    }

    return true;
}

void HalSensors::readPcSimulatedGaze() {
    uint32_t now_ms = M5.millis();

    // Atualiza miniatura 64x48 e 160x120 (Grayscale + RGB565) do visor PIP a ~5.5 FPS
    updateWebcamPipBuffer(now_ms);

    // 1. Verifica clique/arraste dentro da tela virtual via M5.Touch (mapeado pelo M5GFX_SDL)
    auto touch_detail = M5.Touch.getDetail();
    if (touch_detail.isPressed()) {
        gaze_data.touch_active = true;
        gaze_data.face_detected = true;
        simulated_presence_enabled = true;
        gaze_data.raw_screen_x = touch_detail.x;
        gaze_data.raw_screen_y = touch_detail.y;
    } else {
        gaze_data.touch_active = false;

        // 2. Verifica dados reais da Webcam (scripts/webcam_tracker.py -> face_track.txt) a ~20Hz
        if (now_ms - last_face_track_check_ms >= 50) {
            last_face_track_check_ms = now_ms;
            int32_t cam_x = gaze_data.raw_screen_x;
            int32_t cam_y = gaze_data.raw_screen_y;
            bool cam_face = gaze_data.face_detected;

            if (readWebcamTrackerFile(cam_x, cam_y, cam_face)) {
                if (!webcam_tracker_active) {
                    webcam_tracker_active = true;
                    M5_LOGI("[HAL-PC] Rastreamento facial REAL da Webcam (Visao 2.0 + Gestos) ativo via '%s'!", FACE_TRACK_FILE_PATH);
                }
                gaze_data.face_detected = cam_face && simulated_presence_enabled;
                if (gaze_data.face_detected) {
                    gaze_data.raw_screen_x = cam_x;
                    gaze_data.raw_screen_y = cam_y;
                }
            } else {
                if (webcam_tracker_active) {
                    M5_LOGW("[HAL-PC] Rastreador da Webcam inativo. Voltando para o cursor do mouse.");
                }
                webcam_tracker_active = false;
            }
        }

        // 3. Fallback automático para o cursor do mouse na janela SDL2 se o script da webcam não estiver rodando
        if (!webcam_tracker_active) {
#if __has_include(<SDL2/SDL.h>) || __has_include(<SDL.h>)
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);

            if (simulated_presence_enabled && mouse_x >= 0 && mouse_x < SCREEN_WIDTH && mouse_y >= 0 && mouse_y < SCREEN_HEIGHT) {
                gaze_data.raw_screen_x = mouse_x;
                gaze_data.raw_screen_y = mouse_y;
                gaze_data.face_detected = true;
            } else {
                gaze_data.face_detected = simulated_presence_enabled;
            }
            updateSharedVisionCalibrationAndGestures(now_ms, false, false);
#endif
        }
    }

    // Normaliza as coordenadas para o intervalo [-1.0, +1.0]
    if (gaze_data.face_detected) {
        float norm_x = (static_cast<float>(gaze_data.raw_screen_x) / (SCREEN_WIDTH / 2.0f)) - 1.0f;
        float norm_y = (static_cast<float>(gaze_data.raw_screen_y) / (SCREEN_HEIGHT / 2.0f)) - 1.0f;
        gaze_data.target_x = fmaxf(-1.0f, fminf(1.0f, norm_x));
        gaze_data.target_y = fmaxf(-1.0f, fminf(1.0f, norm_y));
    } else {
        gaze_data.target_x = 0.0f;
        gaze_data.target_y = 0.25f; // Olhar caído quando usuário está ausente
    }
}

void HalSensors::readPcSimulatedAudio() {
    uint32_t current_ms = M5.millis();
    audio_data.is_loud_peak = false;
    audio_data.interaction_triggered = false;
    audio_data.vad_phrase_completed = false;
    audio_data.double_clap_detected = false;
    audio_data.whistle_detected = false;
    audio_data.silence_toggle_kbd_triggered = false;
    audio_data.pomodoro_toggle_triggered = false;
    audio_data.gallery_timelapse_triggered = false;
    audio_data.ambient_audio_toggle_triggered = false;
    audio_data.voice_note_replay_triggered = false;
    audio_data.theme_cycle_triggered = false;
    audio_data.pip_zoom_toggle_triggered = false;
    audio_data.rpg_modal_toggle_triggered = false;
    audio_data.calibrate_posture_triggered = false;
    audio_data.pip_color_mode_triggered = false;
    audio_data.object_scan_triggered = false;
    audio_data.rain_preview_toggle_triggered = false;

    // 1. Lê o nível de ruído real e estados do Ouvido 2.0 medidos no callback do SDL_OpenAudioDevice
    float current_mic_db = NOISE_FLOOR_DB;
    float local_noise_floor = dsp_noise_floor_db;
    float local_zcr = dsp_latest_zcr;
    bool local_double_clap = false;
    bool local_whistle = false;
    bool local_typing = false;
    bool local_music_beat = false;
    uint32_t local_beat_interval = 500U;
    bool local_wake_latched = false;
    uint16_t local_voiced_frames = 0;
    uint8_t local_syllables = 0;

    if (mic_capture_active) {
        std::lock_guard<std::mutex> lock(mic_buffer_mutex);
        current_mic_db = realtime_mic_db;
        local_noise_floor = dsp_noise_floor_db;
        local_zcr = dsp_latest_zcr;
        local_typing = (current_ms < dsp_typing_active_until_ms);
        local_music_beat = (current_ms < dsp_music_active_until_ms);
        local_beat_interval = dsp_beat_interval_ms;
        local_wake_latched = dsp_wake_word_latched;
        local_voiced_frames = dsp_voiced_formant_frames;
        local_syllables = dsp_syllable_peaks;
        if (dsp_double_clap_latched) {
            local_double_clap = true;
            dsp_double_clap_latched = false;
        }
        if (dsp_whistle_latched) {
            local_whistle = true;
            dsp_whistle_latched = false;
        }
    }

    audio_data.adaptive_noise_floor_db = local_noise_floor;
    audio_data.spectral_zcr = local_zcr;
    audio_data.is_typing_cadence = local_typing;
    audio_data.music_beat_active = local_music_beat;
    if (local_music_beat && local_beat_interval > 150U) {
        audio_data.music_beat_phase = static_cast<float>(current_ms % local_beat_interval) /
                                      static_cast<float>(local_beat_interval);
    } else {
        audio_data.music_beat_phase = 0.0f;
    }

    if (local_double_clap) {
        audio_data.double_clap_detected = true;
        vad_active_speech_frames = 0;
        vad_trailing_silence_frames = 0;
        audio_data.vad_speech_active = false;
    }
    if (local_whistle) {
        audio_data.whistle_detected = true;
    }

#if __has_include(<SDL2/SDL.h>) || __has_include(<SDL.h>)
    const uint8_t* key_state = SDL_GetKeyboardState(nullptr);
    if (key_state != nullptr) {
        bool key_space = key_state[SDL_SCANCODE_SPACE] != 0;
        bool key_m     = key_state[SDL_SCANCODE_M] != 0;
        bool key_i     = key_state[SDL_SCANCODE_I] != 0;
        bool key_o     = key_state[SDL_SCANCODE_O] != 0;
        bool key_p     = key_state[SDL_SCANCODE_P] != 0;
        bool key_r     = key_state[SDL_SCANCODE_R] != 0;
        bool key_d     = key_state[SDL_SCANCODE_D] != 0;
        bool key_f     = key_state[SDL_SCANCODE_F] != 0;
        bool key_v     = key_state[SDL_SCANCODE_V] != 0;
        bool key_g     = key_state[SDL_SCANCODE_G] != 0;
        bool key_b     = key_state[SDL_SCANCODE_B] != 0;
        bool key_n     = key_state[SDL_SCANCODE_N] != 0;
        bool key_t     = key_state[SDL_SCANCODE_T] != 0;
        bool key_z     = key_state[SDL_SCANCODE_Z] != 0;
        bool key_tab   = key_state[SDL_SCANCODE_TAB] != 0;
        bool key_c     = key_state[SDL_SCANCODE_C] != 0;
        bool key_x     = key_state[SDL_SCANCODE_X] != 0;
        bool key_s     = key_state[SDL_SCANCODE_S] != 0;
        bool key_q     = key_state[SDL_SCANCODE_Q] != 0;
        bool key_j     = key_state[SDL_SCANCODE_J] != 0;
        bool key_k     = key_state[SDL_SCANCODE_K] != 0;
        bool key_h     = key_state[SDL_SCANCODE_H] != 0;
        bool key_w     = key_state[SDL_SCANCODE_W] != 0;

        // [ESPAÇO]: Dispara pico de ruído alto manual (susto) na borda de subida
        if (key_space && !prev_key_space) {
            audio_data.is_loud_peak = true;
            audio_data.noise_level_db = LOUD_PEAK_DB;
            audio_data.last_event = AUDIO_EVENT_LOUD_NOISE;
            last_loud_peak_ms = current_ms;
        }

        // [M]: Alterna simulação manual de fala contínua (Modo Reunião)
        if (key_m && !prev_key_m) {
            manual_meeting_override = !manual_meeting_override;
            audio_data.is_continuous_speech = manual_meeting_override;
            audio_data.last_event = audio_data.is_continuous_speech
                                        ? AUDIO_EVENT_CONTINUOUS_SPEECH
                                        : AUDIO_EVENT_NONE;
        }

        // [I] ou [O]: Dispara interação com o Oráculo Sarcástico Multimodal
        if ((key_i && !prev_key_i) || (key_o && !prev_key_o)) {
            audio_data.interaction_triggered = true;
            audio_data.last_event = AUDIO_EVENT_INTERACTION;
        }

        // [P]: Alterna presença/ausência de rosto na câmera
        if (key_p && !prev_key_p) {
            simulated_presence_enabled = !simulated_presence_enabled;
            gaze_data.face_detected = simulated_presence_enabled;
        }

        // [R]: Centraliza o olhar do pet
        if (key_r && !prev_key_r) {
            gaze_data.target_x = 0.0f;
            gaze_data.target_y = 0.0f;
            gaze_data.raw_screen_x = SCREEN_WIDTH / 2;
            gaze_data.raw_screen_y = SCREEN_HEIGHT / 2;
        }

        // [D]: Alterna overlay semitransparente de depuração (FPS, Ruído, Rosto, PIP, JSON)
        if (key_d && !prev_key_d) {
            debug_mode_enabled = !debug_mode_enabled;
        }

        // [F]: Inicia/Cancela Missão de Foco Tático (Pomodoro 25 min)
        if (key_f && !prev_key_f) {
            audio_data.pomodoro_toggle_triggered = true;
        }

        // [V]: Cicla Modo de Escuta Hands-Free (VAD: PALAVRA-CHAVE "Bicho" -> VAD: LIVRE -> VAD: OFF)
        if (key_v && !prev_key_v) {
            if (audio_data.vad_enabled && audio_data.vad_keyword_mode) {
                audio_data.vad_keyword_mode = false;
                M5_LOGI("[HAL-PC] Escuta Hands-Free (VAD): MODO LIVRE (responde a qualquer frase curta).");
            } else if (audio_data.vad_enabled && !audio_data.vad_keyword_mode) {
                audio_data.vad_enabled = false;
                M5_LOGI("[HAL-PC] Escuta Hands-Free (VAD): DESATIVADA.");
            } else {
                audio_data.vad_enabled = true;
                audio_data.vad_keyword_mode = true;
                M5_LOGI("[HAL-PC] Escuta Hands-Free (VAD): MODO PALAVRA-CHAVE ('Bicho' / 'Ei Bicho' / 'Oraculo').");
            }
            vad_active_speech_frames = 0;
            vad_trailing_silence_frames = 0;
            audio_data.vad_speech_active = false;
        }

        // [G]: Dispara Galeria Timelapse de Snapshots do Cartão SD no Visor PIP
        if (key_g && !prev_key_g) {
            audio_data.gallery_timelapse_triggered = true;
        }

        // [B]: Alterna Som Ambiente de Foco (Drone Espacial do Cartão SD)
        if (key_b && !prev_key_b) {
            audio_data.ambient_audio_toggle_triggered = true;
        }

        // [N]: Reproduz a última Nota de Voz gravada na Caixa Preta do Cartão SD
        if (key_n && !prev_key_n) {
            audio_data.voice_note_replay_triggered = true;
        }

        // [T]: Cicla entre os 4 Temas Sci-Fi de 16 bits pré-renderizados no Cartão SD
        if (key_t && !prev_key_t) {
            audio_data.theme_cycle_triggered = true;
        }

        // [Z]: Alterna Visor de Zoom Óptico HD 160x120 com interpolação bilinear
        if (key_z && !prev_key_z) {
            audio_data.pip_zoom_toggle_triggered = true;
        }

        // [TAB]: Abre/Fecha Painel Tático de RPG e Matriz de Hábitos
        if (key_tab && !prev_key_tab) {
            audio_data.rpg_modal_toggle_triggered = true;
        }

        // [C]: Recalibra a postura ergonômica pessoal usando a pose atual do usuário
        if (key_c && !prev_key_c) {
            recalibratePostureBaseline();
            audio_data.calibrate_posture_triggered = true;
        }

        // [X]: Cicla o Modo Óptico do Visor PIP (Ciano Tático / Cor Real RGB565 / Termográfico IR)
        if (key_x && !prev_key_x) {
            audio_data.pip_color_mode_triggered = true;
        }

        // [S]: Dispara Inspeção Visual de Objeto na Câmera (alimentar/hidratar/analisar objeto)
        if (key_s && !prev_key_s) {
            audio_data.object_scan_triggered = true;
        }

        // [Q]: Simula Gesto de Dedo na Boca ("Shhh!" -> Alterna Modo Silêncio Indefinido / Cortar Fala)
        if (key_q && !prev_key_q) {
            shhh_active_until_ms = current_ms + 1100U;
            gaze_data.gesture_shhh = true;
            audio_data.silence_toggle_kbd_triggered = true;
        }

        // [J]: Simula Gesto de Joinha ("Thumbs Up" 👍)
        if (key_j && !prev_key_j) {
            thumb_active_until_ms = current_ms + 1100U;
            gaze_data.gesture_thumbs_up = true;
        }

        // [K]: Simula Gesto de Paz e Amor / Vitória ("V-Sign" ✌️ -> Selfie Tática)
        if (key_k && !prev_key_k) {
            victory_active_until_ms = current_ms + 1100U;
            gaze_data.gesture_victory = true;
        }

        // [H]: Simula Gesto de Palma Aberta ("Stop / Pause" ✋)
        if (key_h && !prev_key_h) {
            palm_active_until_ms = current_ms + 1100U;
            gaze_data.gesture_open_palm = true;
        }

        // [W]: Alterna/Simula Efeito de Chuva com Gotas Escorrendo no Visor
        if (key_w && !prev_key_w) {
            audio_data.rain_preview_toggle_triggered = true;
        }

        prev_key_space = key_space;
        prev_key_m     = key_m;
        prev_key_i     = key_i;
        prev_key_o     = key_o;
        prev_key_p     = key_p;
        prev_key_r     = key_r;
        prev_key_d     = key_d;
        prev_key_f     = key_f;
        prev_key_v     = key_v;
        prev_key_g     = key_g;
        prev_key_b     = key_b;
        prev_key_n     = key_n;
        prev_key_t     = key_t;
        prev_key_z     = key_z;
        prev_key_tab   = key_tab;
        prev_key_c     = key_c;
        prev_key_x     = key_x;
        prev_key_s     = key_s;
        prev_key_q     = key_q;
        prev_key_j     = key_j;
        prev_key_k     = key_k;
        prev_key_h     = key_h;
        prev_key_w     = key_w;
    }
#endif

    // 2. Processa sinais acústicos reais do microfone quando ativo, em modo escuta (AUDIO_MODE_MIC) e fora da guarda anti-eco
    if (mic_capture_active && current_audio_mode == AUDIO_MODE_MIC && current_ms >= mic_echo_guard_until_ms) {
        // Detecta pico repentino de barulho alto real (> 85 dB, exceto se for Duas Palmas) -> EMOTION_STARTLED
        if (current_mic_db >= 85.0f && !audio_data.double_clap_detected && (current_ms - last_loud_peak_ms > 1200)) {
            audio_data.is_loud_peak = true;
            audio_data.noise_level_db = current_mic_db;
            audio_data.last_event = AUDIO_EVENT_LOUD_NOISE;
            last_loud_peak_ms = current_ms;
            M5_LOGI("[HAL-PC] Pico de barulho alto REAL detectado no microfone: %.1f dB!", current_mic_db);
        }

        // Máquina de Estados VAD Hands-Free 2.0 (Limiar Adaptativo + Rejeição de Teclado + Palavra-Chave "Bicho"):
        const float vad_speech_thresh_db = fmaxf(45.5f, local_noise_floor + 10.5f);
        if (audio_data.vad_enabled && !manual_meeting_override && !audio_data.is_continuous_speech) {
            bool is_voice_frame = (current_mic_db >= vad_speech_thresh_db && current_mic_db < 84.0f) &&
                                  (!local_typing || local_voiced_frames >= 6U);
            if (is_voice_frame) {
                vad_active_speech_frames++;
                vad_trailing_silence_frames = 0;
                if (vad_active_speech_frames >= 22U && vad_active_speech_frames <= 215U) {
                    audio_data.vad_speech_active = true;
                }
            } else {
                if (vad_active_speech_frames >= 24U && vad_active_speech_frames <= 210U) {
                    vad_trailing_silence_frames++;
                    // ~0.75s (45 frames a 60Hz) de pausa após a frase + cooldown de 12s -> avalia Palavra-Chave!
                    if (vad_trailing_silence_frames >= 45U && (current_ms - last_vad_trigger_ms > 12000U)) {
                        bool facing_bicho = gaze_data.face_detected && (std::fabs(gaze_data.head_yaw_deg) <= 24.0f);
                        bool kw_ok = !audio_data.vad_keyword_mode ||
                                     local_wake_latched ||
                                     (facing_bicho && local_voiced_frames >= 8U && local_syllables >= 2U);
                        audio_data.wake_word_detected = local_wake_latched || (facing_bicho && local_syllables >= 2U);

                        if (kw_ok && local_voiced_frames >= 5U) {
                            audio_data.vad_phrase_completed = true;
                            audio_data.vad_speech_active = false;
                            audio_data.last_event = AUDIO_EVENT_VOICE_COMMAND;
                            last_vad_trigger_ms = current_ms;
                            M5_LOGI("[OUVIDO-VAD] Frase de voz detectada (PalavraChave=%s, Formantes=%u, Silabas=%u)! Acionando Oraculo...",
                                    local_wake_latched ? "FONETICA_BICHO" : (audio_data.vad_keyword_mode ? "ENCARANDO_BICHO" : "LIVRE"),
                                    static_cast<unsigned int>(local_voiced_frames),
                                    static_cast<unsigned int>(local_syllables));
                        } else {
                            M5_LOGI("[OUVIDO-VAD] Ruido/fala lateral ignorado pelo filtro de Palavra-Chave (Formantes=%u, Silabas=%u, Yaw=%.1f).",
                                    static_cast<unsigned int>(local_voiced_frames),
                                    static_cast<unsigned int>(local_syllables),
                                    gaze_data.head_yaw_deg);
                        }
                        vad_active_speech_frames = 0;
                        vad_trailing_silence_frames = 0;
                        {
                            std::lock_guard<std::mutex> lock(mic_buffer_mutex);
                            dsp_kw_phonetic_stage = 0;
                            dsp_voiced_formant_frames = 0;
                            dsp_syllable_peaks = 0;
                            dsp_wake_word_latched = false;
                        }
                    }
                } else {
                    if (vad_active_speech_frames > 0U && current_mic_db < local_noise_floor + 6.0f) {
                        std::lock_guard<std::mutex> lock(mic_buffer_mutex);
                        dsp_kw_phonetic_stage = 0;
                        dsp_voiced_formant_frames = 0;
                        dsp_syllable_peaks = 0;
                        dsp_wake_word_latched = false;
                    }
                    vad_active_speech_frames = 0;
                    vad_trailing_silence_frames = 0;
                    audio_data.vad_speech_active = false;
                }
            }
        } else {
            audio_data.vad_speech_active = false;
            vad_active_speech_frames = 0;
            vad_trailing_silence_frames = 0;
        }

        // Detecta fala contínua real (reunião) se a voz humana (não teclado!) se mantiver acima do piso + 18 dB por ~5.5s
        const float meeting_thresh_db = fmaxf(54.0f, local_noise_floor + 18.0f);
        if (!manual_meeting_override) {
            if (current_mic_db >= meeting_thresh_db && !local_typing) {
                sustained_voice_frames++;
                silence_voice_frames = 0;
                if (sustained_voice_frames >= 330U && !audio_data.is_continuous_speech) {
                    audio_data.is_continuous_speech = true;
                    audio_data.vad_speech_active = false;
                    audio_data.last_event = AUDIO_EVENT_CONTINUOUS_SPEECH;
                    M5_LOGI("[HAL-PC] Fala continua REAL detectada pelo microfone (Modo Reuniao ativado).");
                }
            } else {
                silence_voice_frames++;
                if (silence_voice_frames >= 240U) { // ~4 segundos de silêncio encerram o modo reunião automático
                    sustained_voice_frames = 0;
                    if (audio_data.is_continuous_speech) {
                        audio_data.is_continuous_speech = false;
                        audio_data.last_event = AUDIO_EVENT_NONE;
                    }
                }
            }
        }

        // Atualiza noise_level_db em tempo real para animar os VU meters laterais
        if (current_ms - last_loud_peak_ms > 450) {
            audio_data.is_loud_peak = false;
            float target_db = current_mic_db;
            if (manual_meeting_override && target_db < SPEECH_LEVEL_DB) {
                target_db = SPEECH_LEVEL_DB;
            }
            audio_data.noise_level_db += (target_db - audio_data.noise_level_db) * 0.35f;
        }
    } else {
        // Fallback suave caso nenhum microfone físico esteja disponível
        audio_data.vad_speech_active = false;
        if (current_ms - last_loud_peak_ms > 450) {
            audio_data.is_loud_peak = false;
            float target_db = audio_data.is_continuous_speech ? SPEECH_LEVEL_DB : NOISE_FLOOR_DB;
            audio_data.noise_level_db += (target_db - audio_data.noise_level_db) * 0.15f;
        }
    }
}

void HalSensors::readPcSimulatedEnvironment() {
    env_data.accel_x = 0.0f;
    env_data.accel_y = 0.0f;
    env_data.accel_z = 1.0f;
    env_data.ambient_lux = webcam_ambient_lux;
    env_data.proximity_raw = gaze_data.object_held_close ? 980 : (gaze_data.is_too_close ? 900 : (gaze_data.touch_active ? 800 : 20));
    env_data.battery_percent = 98;
    env_data.is_charging = true;
}

#endif // defined(ARDUINO)

