#include "pet_controller_internal.h"

// PetController: estado interno, regras de comportamento, Oráculo, física facial e getters

// =============================================================================
// Implementação da Classe PetController
// =============================================================================

PetController::PetController()
    : current_emotion(EMOTION_HAPPY),
      wav_buffer(),
      sfx_wav_buffer(),
      playback_start_ms(0),
      wav_duration_ms(0),
      wav_pcm_data_offset(VOICE_WAV_HEADER_OFFSET),
      using_wav_voice(false),
      last_minute_tick_ms(0),
      emotion_lock_until_ms(0),
      startle_until_ms(0),
      last_blink_ms(0),
      next_blink_interval_ms(3400),
      last_oracle_trigger_ms(0),
      oracle_phrase_cursor(0),
      is_blinking(false),
      was_user_present(true),
      prev_touch_active(false),
      debug_mode(false),
      imu_shake_detected(false),
      glitch_frames_remaining(0),
      active_glitch_band_count(0),
      cached_pip_buffer(nullptr),
      cached_pip_valid(false),
      cached_pip_hd_buffer(nullptr),
      cached_pip_hd_valid(false),
      cached_pip_rgb565_buffer(nullptr),
      cached_pip_rgb565_valid(false),
      cached_pip_hd_rgb565_buffer(nullptr),
      cached_pip_hd_rgb565_valid(false),
      pip_optic_mode(1), // Inicia no Modo 1 (Cor Real 16-bit RGB565) por padrão para exibir cores vivas no PIP
      pip_hd_zoom_active(false),
      pip_display_until_ms(0),
      bad_posture_since_ms(0),
      last_posture_alert_ms(0),
      pomodoro_active(false),
      pomodoro_end_ms(0),
      ambient_focus_enabled(false),
      last_ambient_loop_ms(0),
      timelapse_active(false),
      timelapse_until_ms(0),
      timelapse_last_step_ms(0),
      timelapse_frame_index(0),
      timelapse_files(),
      active_theme_index(0),
      sd_gfx_ready(false),
      active_theme_bg_rgb565(),
      rpg_panel_open(false),
      daily_briefing_done(false),
      phone_look_down_since_ms(0),
      last_phone_alert_ms(0),
      procrastination_since_ms(0),
      last_procrastination_alert_ms(0),
      last_hand_wave_ms(0),
      intruder_since_ms(0),
      last_intruder_alert_ms(0),
      yawn_since_ms(0),
      last_yawn_alert_ms(0),
      object_close_since_ms(0),
      last_object_inspect_ms(0),
      dark_room_since_ms(0),
      last_night_owl_alert_ms(0),
      last_head_gesture_ms(0),
      last_hand_gesture_react_ms(0),
      last_acoustic_gesture_ms(0),
      is_muted_indefinitely(false),
      selfie_flash_until_ms(0),
      saccade_offset_x(0.0f),
      saccade_offset_y(0.0f),
      next_saccade_ms(0),
      rain_simulation_override(false),
      visor_raindrops(),
      head_pat_until_ms(0),
      last_head_pat_reward_ms(0),
      wink_until_ms(0),
      next_wink_ms(14000),
      blink_bounce_until_ms(0),
      last_touch_x(-1),
      last_touch_y(-1),
      touch_stroke_accum(0.0f),
      vu_peak_left(2),
      vu_peak_right(2),
      last_vu_decay_ms(0),
      hud_popups(),
      micro_saccade_offset_x(0.0f),
      micro_saccade_offset_y(0.0f),
      micro_saccade_until_ms(0),
      next_micro_saccade_ms(2800),
      pupil_lux_radius_offset(0.0f),
      side_monitor_since_ms(0),
      is_side_eye_active(false),
      side_eye_dir(0.0f),
      bounce_offset_y(0.0f),
      slouch_drop_y(0.0f),
      was_slouching_pose(false),
      prev_git_modified_files(0),
      git_commits_today(0),
      shift_debrief_done_today(false),
      debrief_overlay_until_ms(0),
      pending_voice_until_ms(0),
      diary_service(),
      sfx_playback_until_ms(0),
      retention_policy(),
      privacy_mode(false),
      pending_voice_wav_b64(),
      voice_worker(),
      voice_job_id(0),
      awaiting_voice(false),
      voice_wait_deadline_ms(0)
{
    internal_stats = {20, 86, 88, 0, 0, 0};
    current_pose = {
        0.0f, 0.0f,
        0.0f, 0.0f,
        FACE_EYE_DEFAULT_R, FACE_PUPIL_DEFAULT_R,
        0.0f, 0.18f,
        -4.0f, -2.0f, 0.0f,
        MOUTH_CLOSED_HEIGHT, 26.0f, 0.55f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        0.18f, 0.18f,
        -4.0f, 4.0f,
        -2.0f, -2.0f,
        0.0f, 0.25f
    };
    target_pose = current_pose;
    std::memset(&lip_sync_state, 0, sizeof(lip_sync_state));
    std::memset(glitch_bands, 0, sizeof(glitch_bands));
    std::memset(last_state_json, 0, sizeof(last_state_json));
    std::memset(&cached_env_data, 0, sizeof(cached_env_data));
    std::memset(&cached_audio_data, 0, sizeof(cached_audio_data));
    std::memset(timelapse_pip_buffer, 0, sizeof(timelapse_pip_buffer));
    std::memset(eye_lens_textures_rgb565, 0, sizeof(eye_lens_textures_rgb565));
    std::memset(hud_icons_rgb565, 0, sizeof(hud_icons_rgb565));
    std::memset(visor_raindrops, 0, sizeof(visor_raindrops));
    std::memset(hud_popups, 0, sizeof(hud_popups));
    initVisorRainDrops();
}

void PetController::spawnHudPopup(const char* text, float x, float y, uint16_t color, uint32_t duration_ms) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    uint32_t now_ms = M5.millis();
    int32_t chosen_idx = 0;
    uint32_t oldest_spawn = 0xFFFFFFFFU;
    for (uint8_t i = 0; i < MAX_HUD_POPUPS; ++i) {
        if (!hud_popups[i].active || now_ms >= hud_popups[i].expire_ms) {
            chosen_idx = static_cast<int32_t>(i);
            break;
        }
        if (hud_popups[i].spawn_ms < oldest_spawn) {
            oldest_spawn = hud_popups[i].spawn_ms;
            chosen_idx = static_cast<int32_t>(i);
        }
    }
    FloatingHudPopup& p = hud_popups[chosen_idx];
    std::strncpy(p.text, text, sizeof(p.text) - 1);
    p.text[sizeof(p.text) - 1] = '\0';
    p.x = x;
    p.y = y;
    p.vy = -0.38f;
    p.color = color;
    p.spawn_ms = now_ms;
    p.expire_ms = now_ms + duration_ms;
    p.active = true;
}

bool PetController::isDistractionWindow(const char* window_title) const {
    if (window_title == nullptr || window_title[0] == '\0') {
        return false;
    }
    char lower_buf[96];
    size_t i = 0;
    for (; window_title[i] != '\0' && i < sizeof(lower_buf) - 1; ++i) {
        char c = window_title[i];
        lower_buf[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }
    lower_buf[i] = '\0';

    static const char* const DISTRACTION_KEYWORDS[] = {
        "youtube", "netflix", "twitch", "reddit", "twitter", "x.com",
        "instagram", "tiktok", "facebook", "steam", "9gag", "prime video"
    };
    for (size_t k = 0; k < sizeof(DISTRACTION_KEYWORDS) / sizeof(DISTRACTION_KEYWORDS[0]); ++k) {
        if (std::strstr(lower_buf, DISTRACTION_KEYWORDS[k]) != nullptr) {
            return true;
        }
    }
    return false;
}

void PetController::initController() {
    uint32_t now_ms = M5.millis();
    last_minute_tick_ms = now_ms;
    last_blink_ms = now_ms;
    emotion_lock_until_ms = now_ms + 5000;
    initVisorRainDrops();
    oracle_brain.initBrain();
    voice_worker.start(&oracle_brain, &PetController::applyEmotionalSciFiDspToWav);
    initOrGenerateSdAudioAssets();
    initOrGenerateSdGraphicsAssets();
    world_context_service.initWorldContext();
    buildStateJson(last_state_json, sizeof(last_state_json));
}

void PetController::restorePersistentState(HalSensors& hal_sensors) {
    const SdSystemConfig& sd_cfg = hal_sensors.getSdConfig();
    world_context_service.setLocation(sd_cfg.latitude, sd_cfg.longitude);
    retention_policy.snapshots_max_files = sd_cfg.snapshots_max_files;
    retention_policy.tts_cache_max_mb = sd_cfg.tts_cache_max_mb;
    retention_policy.log_max_lines = sd_cfg.log_max_lines;
    retention_policy.diary_keep_days = sd_cfg.diary_keep_days;
    oracle_brain.setRetentionPolicy(retention_policy);
    runStorageRetention(retention_policy);

    privacy_mode = sd_cfg.privacy_mode;
    oracle_brain.setPrivacyMode(privacy_mode);
    diary_service.setPrivacyMode(privacy_mode);
    diary_service.init(&oracle_brain, sd_cfg.diary_enabled, sd_cfg.diary_store_transcripts);
    diary_service.logEvent("SESSION_START", "Bicho ligado na mesa");
    oracle_brain.loadMemoriesFromStorage();
    PersistentPetData loaded_data = {
        internal_stats.hunger_level,
        internal_stats.mood_score,
        internal_stats.energy_level,
        internal_stats.focus_minutes
    };
    if (hal_sensors.loadPetState(loaded_data)) {
        internal_stats.hunger_level  = clampStatValue(loaded_data.hunger_level, 0, MAX_STAT_VALUE);
        internal_stats.mood_score    = clampStatValue(loaded_data.mood_score, 0, MAX_STAT_VALUE);
        internal_stats.energy_level  = clampStatValue(loaded_data.energy_level, 0, MAX_STAT_VALUE);
        internal_stats.focus_minutes = loaded_data.focus_minutes;
        // Só recupera valores padrões caso o save esteja completamente zerado/corrompido
        if (internal_stats.mood_score == 0 && internal_stats.energy_level == 0) {
            internal_stats.mood_score = 85;
            internal_stats.energy_level = 88;
        }
        current_emotion = (internal_stats.mood_score >= 60) ? EMOTION_HAPPY : EMOTION_IDLE;
        syncPersistentState(hal_sensors);
        buildStateJson(last_state_json, sizeof(last_state_json));
    } else {
        // Cria o arquivo/NVS inicial na primeira execução
        syncPersistentState(hal_sensors);
    }
}

void PetController::syncPersistentState(HalSensors& hal_sensors) const {
    PersistentPetData save_data = {
        internal_stats.hunger_level,
        internal_stats.mood_score,
        internal_stats.energy_level,
        internal_stats.focus_minutes
    };
    hal_sensors.savePetState(save_data);
}

int32_t PetController::clampStatValue(int32_t value, int32_t min_val, int32_t max_val) const {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

void PetController::buildStateJson(char* out_buffer, size_t buffer_size) const {
    std::snprintf(
        out_buffer,
        buffer_size,
        "{\"hunger_level\": %d, \"mood_score\": %d, \"energy_level\": %d, \"focus_minutes\": %u}",
        static_cast<int>(internal_stats.hunger_level),
        static_cast<int>(internal_stats.mood_score),
        static_cast<int>(internal_stats.energy_level),
        static_cast<unsigned int>(internal_stats.focus_minutes)
    );
}

void PetController::updateController(HalSensors& hal_sensors, uint32_t now_ms) {
    const GazeTargetData& gaze_data = hal_sensors.getGazeData();
    const AudioSensorData& audio_data = hal_sensors.getAudioData();
    const EnvironmentSensorData& env_data = hal_sensors.getEnvironmentData();

    // Atualiza cache local para renderização de HUD e Visor PIP (64x48 e 160x120 HD + 16-bit RGB565)
    cached_env_data = env_data;
    cached_audio_data = audio_data;
    cached_pip_buffer = hal_sensors.getPipFrameBuffer();
    cached_pip_valid = hal_sensors.hasValidPipFrame();
    cached_pip_hd_buffer = hal_sensors.getPipHdFrameBuffer();
    cached_pip_hd_valid = hal_sensors.hasValidPipHdFrame();
    cached_pip_rgb565_buffer = hal_sensors.getPipFrameRgb565Buffer();
    cached_pip_rgb565_valid = hal_sensors.hasValidPipRgb565Frame();
    cached_pip_hd_rgb565_buffer = hal_sensors.getPipHdFrameRgb565Buffer();
    cached_pip_hd_rgb565_valid = hal_sensors.hasValidPipRgb565Frame();

    // Sincroniza flag debug_mode com o estado da HAL (tecla [D] no PC)
    debug_mode = hal_sensors.isDebugModeEnabled();

    // Sincroniza de forma assíncrona e não-bloqueante o mundo externo (Open-Meteo, AwesomeAPI, GitHub, BrasilAPI, HN, Geek)
    world_context_service.syncExternalWorldData(now_ms);

    uint32_t prev_focus = internal_stats.focus_minutes;
    int32_t prev_mood = internal_stats.mood_score;

    // 0. Roteamento de Toque / Clique Direto na Tela (UI Clicável / Hotspots Táticos + Cafuné na Testa)
    bool touch_rising_edge = (gaze_data.touch_active && !prev_touch_active);
    prev_touch_active = gaze_data.touch_active;

    bool touch_poke_face    = false;
    bool touch_head_pat     = false;
    bool touch_pomodoro     = false;
    bool touch_ambient      = false;
    bool touch_timelapse    = false;
    bool touch_theme        = false;
    bool touch_pip_zoom     = false;
    bool touch_pip_optic    = false;
    bool touch_calibrate    = false;
    bool touch_rain_preview = false;
    bool touch_replay_vn    = false;
    bool touch_rpg_modal    = false;

    int32_t tx = gaze_data.raw_screen_x;
    int32_t ty = gaze_data.raw_screen_y;
    bool pip_visible = pip_hd_zoom_active || debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms);
    int32_t pip_x0 = pip_hd_zoom_active ? 78 : 232;
    int32_t pip_y0 = pip_hd_zoom_active ? 30 : 34;
    int32_t pip_x1 = pip_x0 + (pip_hd_zoom_active ? 164 : 68);
    int32_t pip_y1 = pip_y0 + (pip_hd_zoom_active ? 132 : 60);

    // Detecção contínua de Cafuné / Carinho por arrasto sobre a testa e órbitas (Y = 30..112)
    if (gaze_data.touch_active && !rpg_panel_open &&
        !(pip_visible && tx >= pip_x0 && tx <= pip_x1 && ty >= pip_y0 && ty <= pip_y1) &&
        ty >= 30 && ty <= 112 && tx >= 42 && tx <= 278) {
        if (last_touch_x >= 0 && last_touch_y >= 0) {
            float dx = static_cast<float>(tx - last_touch_x);
            float dy = static_cast<float>(ty - last_touch_y);
            float step_dist = std::sqrt(dx * dx + dy * dy);
            if (step_dist >= 1.5f && step_dist <= 65.0f) {
                touch_stroke_accum += step_dist;
            }
        }
        last_touch_x = tx;
        last_touch_y = ty;
        if (touch_stroke_accum >= 18.0f) {
            touch_stroke_accum = 0.0f;
            touch_head_pat = true;
        }
    } else if (!gaze_data.touch_active) {
        last_touch_x = -1;
        last_touch_y = -1;
        touch_stroke_accum = 0.0f;
    }

    if (touch_rising_edge) {
        if (ty <= 28) {
            if (tx < 88) {
                touch_rpg_modal = true; // Célula 1 (Badge Poligonal): Abre/Fecha Modal de RPG & Matriz de Hábitos
            } else if (tx < 142) {
                touch_ambient = true;   // Célula 2 (CPU/Core): Alterna Som Ambiente de Foco 24kHz [B]
            } else if (tx < 196) {
                touch_pomodoro = true;  // Célula 3 (Mira Foco): Inicia/Cancela Missão Pomodoro 25m [F]
            } else if (tx < 290) {
                touch_timelapse = true; // Célula 4 (Alvo/HMR): Reproduz Galeria Timelapse no PIP [G]
            } else {
                touch_theme = true;     // Célula 5 (Radar Sonar): Alterna Tema Sci-Fi 16-bit do SD [T]
            }
        } else if (rpg_panel_open) {
            // Qualquer clique fora do topo fecha o modal de RPG & Hábitos
            rpg_panel_open = false;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        } else if (pip_visible && tx >= pip_x0 && tx <= pip_x1 && ty >= pip_y0 && ty <= pip_y1) {
            // Toque na barra superior do Visor PIP (12px do topo) alterna o Modo Óptico [X]; no corpo alterna o Zoom HD [Z]
            if (ty <= pip_y0 + 12) {
                touch_pip_optic = true;
            } else {
                touch_pip_zoom = true;
            }
        } else if (!pip_visible && ty >= 110 && ty <= 130 && tx <= 108) {
            // Toque no Callout Esquerdo (Clima/Nível) alterna simulação de chuva no visor [W]
            touch_rain_preview = true;
        } else if (!pip_visible && ty >= 112 && ty <= 130 && tx >= 220) {
            // Toque no Callout Direito (Postura/Calibração) recalibra a postura pessoal [C]
            touch_calibrate = true;
        } else if (ty >= 158 && ty <= 176 && tx >= 180) {
            touch_replay_vn = true;     // Clique na aba direita da caixa de diálogo toca a última nota de voz [N]
        } else if (ty >= 158 && ty <= 176 && tx < 140) {
            touch_rpg_modal = true;     // Clique na aba esquerda ("SISTEMA ORÁCULO:") abre o painel de RPG/Hábitos
        } else if (ty >= 30 && ty <= 64 && tx >= 68 && tx <= 252) {
            touch_head_pat = true;      // Clique na testa / topo da cabeça faz carinho imediato (Cafuné)!
        } else {
            touch_poke_face = true;     // Clique no rosto central dispara interação com o Oráculo
            blink_bounce_until_ms = now_ms + 240U;
        }
    }

    if (touch_head_pat) {
        head_pat_until_ms = now_ms + 2600U;
        blink_bounce_until_ms = now_ms + 260U;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 3400U;
        if (now_ms - last_head_pat_reward_ms > 2400U) {
            last_head_pat_reward_ms = now_ms;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 4, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(5);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            spawnHudPopup("CAFUNE! +5 XP", 118.0f, 44.0f, COLOR_CYBER_GREEN, 1800U);
        }
    }

    if (audio_data.rpg_modal_toggle_triggered || touch_rpg_modal) {
        rpg_panel_open = !rpg_panel_open;
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
    }

    // 1. Camada 1: Percepção Acústica Imediata + IMU (Susto, Chacoalhão e Glitch de Vídeo)
    updateAcousticReactions(audio_data, env_data, now_ms);

    // 2. Camada 1: Monitor de Presença, Foco Contínuo, Postura, Gestos e Ausência
    updatePresenceAndFocus(gaze_data, audio_data, now_ms);

    // 2.1. Controle de Missão Tática Pomodoro (Tecla [F] ou Toque na Célula 3 do HUD)
    if (audio_data.pomodoro_toggle_triggered || touch_pomodoro) {
        pomodoro_active = !pomodoro_active;
        if (pomodoro_active) {
            pomodoro_end_ms = now_ms + (25U * 60U * 1000U); // 25 minutos reais de missão de combate
            M5_LOGI("[POMODORO] Missao de Foco Tatico iniciada (25 min)!");
            diary_service.logEvent("POMODORO", "Iniciou um pomodoro de 25 min");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_start.wav").c_str());
            if (!oracle_brain.isWaitingAi()) {
                triggerSarcasticOracle(hal_sensors, "POMODORO_START");
            }
        } else {
            M5_LOGI("[POMODORO] Missao de Foco Tatico cancelada pelo usuario.");
            diary_service.logEvent("POMODORO", "Abandonou o pomodoro antes do fim");
            oracle_brain.recordHabitPomodoroResult(false);
            speakWithLipSync(hal_sensors, "Missão Pomodoro abortada! Fugindo do foco tão cedo?");
        }
    }

    if (pomodoro_active && now_ms >= pomodoro_end_ms) {
        pomodoro_active = false;
        M5_LOGI("[POMODORO] Missao de Foco Tatico de 25 min concluida!");
        diary_service.logEvent("POMODORO", "Concluiu um pomodoro de 25 min");
        oracle_brain.recordHabitPomodoroResult(true);
        oracle_brain.saveEventSnapshotToSd("POMODORO_DONE");
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_fanfare.wav").c_str());
        if (!oracle_brain.isWaitingAi()) {
            triggerSarcasticOracle(hal_sensors, "POMODORO_DONE");
        }
    }

    // 2.1b. Controles de Multimídia, Visão 2.0 e Gráficos HD do Cartão SD (Teclado ou Toque nos Hotspots):
    // [G] Galeria Timelapse PIP, [B] Gerador Ambiental, [N] Replay Caixa-Preta, [T] Temas 16-bit SD, [Z] Zoom HD, [X] Modo Óptico, [C] Calibrar Postura
    if (audio_data.gallery_timelapse_triggered || touch_timelapse) {
        timelapse_files = oracle_brain.listSnapshotPipFilesFromSd();
        if (timelapse_files.empty()) {
            oracle_brain.saveEventSnapshotToSd("TIMELAPSE_INIT");
            timelapse_files = oracle_brain.listSnapshotPipFilesFromSd();
        }
        if (!timelapse_files.empty()) {
            timelapse_active = true;
            timelapse_until_ms = now_ms + 8000U;
            timelapse_last_step_ms = 0;
            timelapse_frame_index = 0;
            pip_display_until_ms = timelapse_until_ms;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
            speakWithLipSync(hal_sensors, "Reproduzindo galeria Timelapse do Cartão SD no visor óptico.");
        } else {
            speakWithLipSync(hal_sensors, "Nenhum snapshot encontrado no Cartão SD ainda.");
        }
    }

    if (timelapse_active) {
        if (now_ms >= timelapse_until_ms || timelapse_files.empty()) {
            timelapse_active = false;
        } else if (now_ms - timelapse_last_step_ms >= 550U) {
            timelapse_last_step_ms = now_ms;
            const std::string& raw_path = timelapse_files[timelapse_frame_index % timelapse_files.size()];
            oracle_brain.loadPipRawFileFromSd(raw_path.c_str(), timelapse_pip_buffer);
            timelapse_frame_index++;
        }
    }

    if (audio_data.ambient_audio_toggle_triggered || touch_ambient) {
        ambient_focus_enabled = !ambient_focus_enabled;
        last_ambient_loop_ms = 0;
        if (ambient_focus_enabled) {
            M5_LOGI("[SD-AMBIENT] Gerador Acustico de Foco Binaural/Espacial ATIVADO via Cartao SD.");
            speakWithLipSync(hal_sensors, "Gerador acústico espacial do Cartão SD ativado para blindagem de foco.");
        } else {
            M5_LOGI("[SD-AMBIENT] Gerador Acustico de Foco DESATIVADO.");
            M5.Speaker.stop();
            speakWithLipSync(hal_sensors, "Gerador acústico espacial desativado.");
        }
    }

    if (ambient_focus_enabled && !lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
        if (last_ambient_loop_ms == 0 || (now_ms - last_ambient_loop_ms >= 3600U && !M5.Speaker.isPlaying())) {
            last_ambient_loop_ms = now_ms;
            playSdSoundEffect(hal_sensors, (std::string(SD_AMBIENT_DIR) + "/focus_space_hum.wav").c_str());
        }
    }

    if ((audio_data.voice_note_replay_triggered || touch_replay_vn) && !lip_sync_state.is_speaking) {
        M5_LOGI("[SD-BLACKBOX] Reproduzindo ultima nota de voz do usuario arquivada no Cartao SD...");
        if (!playSdSoundEffect(hal_sensors, (std::string(SD_VOICE_NOTES_DIR) + "/voice_last.wav").c_str())) {
            speakWithLipSync(hal_sensors, "Ainda não há gravações de voz na caixa-preta do Cartão SD.");
        }
    }

    if (audio_data.theme_cycle_triggered || touch_theme) {
        active_theme_index = static_cast<uint8_t>((active_theme_index + 1U) % SD_THEME_COUNT);
        loadSdThemeToBuffer(active_theme_index);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        static const char* const THEME_VOICE_ANNOUNCEMENTS[SD_THEME_COUNT] = {
            "Tema gráfico 16-bit do Cartão SD: Cockpit Cyber Ciano carregado.",
            "Tema gráfico 16-bit do Cartão SD: Operações Táticas Rubi carregado.",
            "Tema gráfico 16-bit do Cartão SD: Overclock Solar Âmbar carregado.",
            "Tema gráfico 16-bit do Cartão SD: Matriz Bio-Esmeralda carregado."
        };
        speakWithLipSync(hal_sensors, THEME_VOICE_ANNOUNCEMENTS[active_theme_index]);
    }

    if (audio_data.pip_zoom_toggle_triggered || touch_pip_zoom) {
        pip_hd_zoom_active = !pip_hd_zoom_active;
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        if (pip_hd_zoom_active) {
            pip_display_until_ms = now_ms + 12000U;
            speakWithLipSync(hal_sensors, "Visor de Zoom Óptico HD 160 por 120 ativado.");
        } else {
            speakWithLipSync(hal_sensors, "Zoom Óptico HD recolhido.");
        }
    }

    if (audio_data.pip_color_mode_triggered || touch_pip_optic) {
        pip_optic_mode = static_cast<uint8_t>((pip_optic_mode + 1U) % 3U);
        pip_display_until_ms = now_ms + 10000U;
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        static const char* const OPTIC_MODE_ANNOUNCEMENTS[3] = {
            "Visor PIP alterado para Filtro Ciano Tático.",
            "Visor PIP alterado para Cor Real 16 bits RGB 565.",
            "Visor PIP alterado para Espectro Termográfico Infravermelho."
        };
        speakWithLipSync(hal_sensors, OPTIC_MODE_ANNOUNCEMENTS[pip_optic_mode]);
    }

    if (audio_data.calibrate_posture_triggered || touch_calibrate) {
        hal_sensors.recalibratePostureBaseline();
        bad_posture_since_ms = 0;
        pip_display_until_ms = now_ms + 6000U;
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        speakWithLipSync(hal_sensors, "Baseline de postura pessoal recalibrada! Agora sua posição atual é a referência.");
    }

    if (audio_data.rain_preview_toggle_triggered || touch_rain_preview) {
        rain_simulation_override = !rain_simulation_override;
        if (rain_simulation_override) {
            initVisorRainDrops();
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            speakWithLipSync(hal_sensors, "Efeito de gotas de chuva escorrendo no visor ativado!");
        } else {
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            speakWithLipSync(hal_sensors, "Simulação de chuva desativada. Seguindo clima externo em tempo real.");
        }
    }

    if (isRainingOnVisor()) {
        updateVisorRainDrops(now_ms);
    }

    // 2.2. Monitor de Postura Calibrada ("Modo Camarão" ou rosto colado no monitor em relação ao baseline do usuário)
    bool currently_slouching = (gaze_data.face_detected && gaze_data.is_slouching);
    if (was_slouching_pose && !currently_slouching) {
        // Assim que is_slouching voltar para false, toca um efeito sonoro curto de alívio
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        spawnHudPopup("POSTURA OK!", 122.0f, 44.0f, COLOR_CYBER_GREEN, 1600U);
    }
    was_slouching_pose = currently_slouching;

    if (gaze_data.face_detected && (gaze_data.is_slouching || gaze_data.is_too_close)) {
        if (bad_posture_since_ms == 0) {
            bad_posture_since_ms = now_ms;
        }
    } else {
        bad_posture_since_ms = 0;
    }

    // 2.25. Recompensa Instantânea de Git Commit (transição de prev_git_modified_files > 0 para 0)
    if (prev_git_modified_files > 0U && env_data.git_modified_files == 0U) {
        git_commits_today++;
        diary_service.logEvent("GIT_COMMIT", "Fez um commit (" + std::to_string(git_commits_today) + "º de hoje)");
        oracle_brain.addXpPoints(50);
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 10, 0, MAX_STAT_VALUE);
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 5000U;
        blink_bounce_until_ms = now_ms + 300U;
        spawnHudPopup("COMMIT! +50 XP", 116.0f, 44.0f, COLOR_CYBER_GREEN, 2400U);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_fanfare.wav").c_str());
        syncPersistentState(hal_sensors);
        if (!oracle_brain.isWaitingAi()) {
            triggerSarcasticOracle(hal_sensors, "GIT_COMMIT_DONE");
        }
        M5_LOGI("[GIT-COMMIT] Transicao git_modified_files %u -> 0 detectada! +50 XP, +10 Humor e gatilho GIT_COMMIT_DONE.",
                static_cast<unsigned int>(prev_git_modified_files));
    }
    prev_git_modified_files = env_data.git_modified_files;

    // 2.28. Consulta relógio local para o Ritual de Encerramento de Expediente (18:30 automático ou Palma após 17:30)
    std::time_t now_epoch = std::time(nullptr);
    std::tm local_tm_buf = {};
#if defined(_WIN32)
    const bool has_local_tm = (localtime_s(&local_tm_buf, &now_epoch) == 0);
#else
    const bool has_local_tm = (localtime_r(&now_epoch, &local_tm_buf) != nullptr);
#endif
    bool is_after_1730 = false;
    if (has_local_tm) {
        if (local_tm_buf.tm_hour < 12 && shift_debrief_done_today) {
            shift_debrief_done_today = false;
            git_commits_today = 0;
        }
        is_after_1730 = (local_tm_buf.tm_hour > 17) || (local_tm_buf.tm_hour == 17 && local_tm_buf.tm_min >= 30);
        if (!shift_debrief_done_today &&
            local_tm_buf.tm_hour == 18 &&
            local_tm_buf.tm_min == 30 &&
            gaze_data.face_detected &&
            internal_stats.focus_minutes >= 15) {
            shift_debrief_done_today = true;
            debrief_overlay_until_ms = now_ms + 8000U;
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 8000U;
            spawnHudPopup("FIM DE TURNO!", 116.0f, 44.0f, COLOR_CYBER_GREEN, 2400U);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_fanfare.wav").c_str());
            if (!oracle_brain.isWaitingAi()) {
                triggerSarcasticOracle(hal_sensors, "END_OF_SHIFT_DEBRIEF");
            }
            M5_LOGI("[DEBRIEFING] Ritual automatico de encerramento de expediente (18:30) acionado!");
        }
    }

    // 2.3. Monitor de Celular / Doomscrolling (olhando para baixo para o smartphone na mesa)
    if (gaze_data.face_detected && gaze_data.is_looking_down_phone) {
        if (phone_look_down_since_ms == 0) {
            phone_look_down_since_ms = now_ms;
        }
    } else {
        phone_look_down_since_ms = 0;
    }

    // 2.4. Detector de Procrastinação Contextual (Janela ativa de entretenimento/rede social no PC)
    if (gaze_data.face_detected && isDistractionWindow(env_data.active_window_title)) {
        if (procrastination_since_ms == 0) {
            procrastination_since_ms = now_ms;
        }
    } else {
        procrastination_since_ms = 0;
    }

    // 2.4b. Visão 2.0: Monitor Multi-Rosto ("Intruso / Pair Programming", face_count >= 2)
    if (gaze_data.face_detected && gaze_data.face_count >= 2U) {
        if (intruder_since_ms == 0) {
            intruder_since_ms = now_ms;
        }
    } else {
        intruder_since_ms = 0;
    }

    // 2.4c. Visão 2.0: Monitor de Bocejo / Fadiga Visual Real (is_yawning)
    if (gaze_data.face_detected && gaze_data.is_yawning) {
        if (yawn_since_ms == 0) {
            yawn_since_ms = now_ms;
        }
    } else {
        yawn_since_ms = 0;
    }

    // 2.4d. Visão 2.0: Monitor de Objeto Próximo na Lente / Alimentação Visual (object_held_close)
    if (gaze_data.object_held_close) {
        if (object_close_since_ms == 0) {
            object_close_since_ms = now_ms;
        }
    } else {
        object_close_since_ms = 0;
    }

    // 2.4e. Visão 2.0: Monitor de Luz Ambiente Baixa / Modo Corujão (ambient_lux < 32 com rosto presente)
    if (gaze_data.face_detected && env_data.ambient_lux > 0U && env_data.ambient_lux < 32U) {
        if (dark_room_since_ms == 0) {
            dark_room_since_ms = now_ms;
        }
    } else {
        dark_room_since_ms = 0;
    }

    // 2.5. Reação Imediata (< 50ms, custo zero de API) a Aceno de Mão (Hand Wave) na Câmera!
    static uint8_t local_phrase_cursor = 0U;

    if (gaze_data.hand_wave_detected && (now_ms - last_hand_wave_ms > 18000U)) {
        last_hand_wave_ms = now_ms;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        blink_bounce_until_ms = now_ms + 280U;
        wink_until_ms = now_ms + 520U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 5, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(5);
        spawnHudPopup("ACENO! +5 XP", 122.0f, 44.0f, COLOR_CYBER_GREEN, 1800U);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            static const char* const WAVE_PHRASES[] = {
                "Opa! Vi seu aceno na câmera! Sensores ópticos retribuindo o cumprimento!",
                "Saudações, comandante! Radar visual confirmou seu aceno na bancada!",
                "Aceno recebido na lente! Sistemas prontos para mais uma rodada de código!"
            };
            speakWithLipSync(hal_sensors, WAVE_PHRASES[(local_phrase_cursor++) % 3U]);
        }
    }

    // 2.6. Visão 2.0: Reação Imediata (< 50ms, custo zero de API) a Gestos de Cabeça "SIM" (Nod) e "NÃO" (Shake)!
    if ((gaze_data.head_nod_yes || gaze_data.head_shake_no) && (now_ms - last_head_gesture_ms > 15000U)) {
        last_head_gesture_ms = now_ms;
        if (gaze_data.head_nod_yes) {
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 4000U;
            blink_bounce_until_ms = now_ms + 260U;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 4, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(4);
            spawnHudPopup("SIM! +4 XP", 128.0f, 44.0f, COLOR_CYBER_GREEN, 1700U);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
                static const char* const NOD_PHRASES[] = {
                    "Afirmativo! Detectei seu aceno positivo na câmera!",
                    "Confirmado no visual! Estamos na mesma sintonia, parceiro!",
                    "Sinal positivo recebido pela lente! Seguindo com o plano!"
                };
                speakWithLipSync(hal_sensors, NOD_PHRASES[(local_phrase_cursor++) % 3U]);
            }
        } else if (gaze_data.head_shake_no) {
            current_emotion = EMOTION_JUDGING;
            emotion_lock_until_ms = now_ms + 4000U;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 2, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(3);
            spawnHudPopup("NEGATIVO! +3 XP", 116.0f, 44.0f, COLOR_CYBER_ORANGE, 1700U);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
                static const char* const SHAKE_PHRASES[] = {
                    "Entendido! Vi você balançando a cabeça dizendo não para a lente.",
                    "Negativo visual registrado! Abortando essa hipótese na hora.",
                    "Captado! Você vetou a ideia só no movimento de cabeça."
                };
                speakWithLipSync(hal_sensors, SHAKE_PHRASES[(local_phrase_cursor++) % 3U]);
            }
        }
    }

    // 2.64. Tecla [L]: Modo Privado (não grava nada pessoal no cartão e não envia fotos à nuvem)
    if (audio_data.privacy_toggle_triggered) {
        privacy_mode = !privacy_mode;
        hal_sensors.setPrivacyMode(privacy_mode);
        oracle_brain.setPrivacyMode(privacy_mode);
        diary_service.setPrivacyMode(privacy_mode);
        diary_service.logEvent("PRIVACY", privacy_mode ? "Ativou o modo privado" : "Desativou o modo privado");
        spawnHudPopup(privacy_mode ? "MODO PRIVADO" : "PRIVADO OFF", 118.0f, 44.0f, COLOR_CYBER_ORANGE, 1900U);
        M5_LOGI("[PRIVACIDADE] Modo privado %s: fotos, notas de voz, transcricoes e memorias %s.",
                privacy_mode ? "ATIVADO" : "DESATIVADO",
                privacy_mode ? "NAO sao gravadas; fotos nao vao para a nuvem" : "voltam a ser gravadas");
    }

    // 2.65. Tecla [Q]: Alterna (Toggle) o Modo Silêncio Indefinido manualmente
    if (audio_data.silence_toggle_kbd_triggered) {
        is_muted_indefinitely = !is_muted_indefinitely;
        if (is_muted_indefinitely) {
            current_emotion = EMOTION_LISTENING;
            emotion_lock_until_ms = now_ms + 4500U;
            spawnHudPopup("MODO SILENCIO", 120.0f, 44.0f, COLOR_CYBER_ORANGE, 1800U);
            stopSpeakingImmediately(hal_sensors, "[MODO SILÊNCIO ATIVO — VOZ MUDA, INTERAÇÃO POR TEXTO ATIVA ([Q] OU JOINHA P/ SOM)]");
            M5_LOGI("[SILENCIO] Modo Silencio Indefinido ATIVADO via tecla [Q]! Interacao por texto mantida.");
            diary_service.logEvent("SILENCE", "Pediu silêncio ao Bicho");
        } else {
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 3500U;
            spawnHudPopup("VOZ ATIVA!", 126.0f, 44.0f, COLOR_CYBER_GREEN, 1800U);
            diary_service.logEvent("SILENCE", "Reativou a voz do Bicho");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            static const char* const UNMUTE_PHRASES[] = {
                "Modo silêncio encerrado! Voz tática do Bicho reativada na bancada.",
                "Cordas vocais cibernéticas online de novo! O que manda, chefe?",
                "Silêncio desativado! Estou de volta na frequência sonora da bancada."
            };
            speakWithLipSync(hal_sensors, UNMUTE_PHRASES[(local_phrase_cursor++) % 3U]);
            M5_LOGI("[SILENCIO] Modo Silencio Indefinido ENCERRADO via tecla [Q]!");
        }
    }

    // 2.7. Reação Imediata (< 50ms, 100% local) a Palma Aberta ✋ (entrar em silêncio) e Joinha 👍 (encerrar modo silêncio)!
    // (1) 👍 Joinha bem definido (Câmera ou Tecla [J]): Atua exclusivamente como encerramento do Modo Silêncio Indefinido!
    if (gaze_data.gesture_thumbs_up && is_muted_indefinitely && (now_ms - last_hand_gesture_react_ms > 3500U)) {
        last_hand_gesture_react_ms = now_ms;
        is_muted_indefinitely = false; // Joinha encerra o modo silêncio indefinido!
        diary_service.logEvent("SILENCE", "Reativou a voz do Bicho com um joinha");
        pip_display_until_ms = now_ms + 5500U;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 5000U;
        blink_bounce_until_ms = now_ms + 280U;
        wink_until_ms = now_ms + 560U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 8, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(10);
        spawnHudPopup("JOINHA: VOZ ATIVA!", 108.0f, 44.0f, COLOR_CYBER_GREEN, 2000U);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            speakWithLipSync(hal_sensors, "Joinha confirmado! Modo silêncio encerrado e sistemas de voz prontos na bancada!");
        }
        M5_LOGI("[GESTO-OPTICO] Joinha bem definido detectado! Modo Silencio Indefinido encerrado (+8 Humor, +10 XP).");
    }
    // (2) ✋ Palma Aberta ("Stop / Silêncio" ou Debriefing de Fim de Tarde após 17:30):
    else if (gaze_data.gesture_open_palm && !gaze_data.gesture_thumbs_up &&
             (lip_sync_state.is_speaking || !is_muted_indefinitely || (now_ms - last_hand_gesture_react_ms > 5500U))) {
        last_hand_gesture_react_ms = now_ms;
        if (is_after_1730 && !lip_sync_state.is_speaking && (now_ms >= debrief_overlay_until_ms)) {
            shift_debrief_done_today = true;
            debrief_overlay_until_ms = now_ms + 8000U;
            pip_display_until_ms = now_ms + 5000U;
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 8000U;
            spawnHudPopup("FIM DE TURNO!", 116.0f, 44.0f, COLOR_CYBER_GREEN, 2400U);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_fanfare.wav").c_str());
            if (!oracle_brain.isWaitingAi()) {
                triggerSarcasticOracle(hal_sensors, "END_OF_SHIFT_DEBRIEF");
            }
            M5_LOGI("[DEBRIEFING] Ritual manual de encerramento de expediente acionado por Palma Aberta apos 17:30!");
        } else {
            is_muted_indefinitely = true; // Permanece em silêncio de áudio até Joinha 👍 ou tecla [Q]!
            pip_display_until_ms = now_ms + 5000U;
            current_emotion = EMOTION_LISTENING;
            emotion_lock_until_ms = now_ms + 4500U;
            spawnHudPopup("PALMA: SILENCIO", 114.0f, 44.0f, COLOR_CYBER_ORANGE, 1900U);
            stopSpeakingImmediately(hal_sensors, "[GESTO: PALMA ABERTA — VOZ MUDA, INTERAÇÃO POR TEXTO ATIVA ([Q] OU JOINHA P/ SOM)]");
            M5_LOGI("[GESTO-OPTICO] Palma aberta (STOP/SILENCIO) detectada! Modo Silencio Indefinido ativado.");
            diary_service.logEvent("SILENCE", "Pediu silêncio ao Bicho com a palma da mão");
        }
    }
    // Atalhos de teclado dedicados ([K] Selfie Vitória):
    else if (gaze_data.gesture_victory && (now_ms - last_hand_gesture_react_ms > 5000U)) {
        last_hand_gesture_react_ms = now_ms;
        is_muted_indefinitely = false;
        pip_optic_mode = 1U;
        pip_display_until_ms = now_ms + 8000U;
        selfie_flash_until_ms = now_ms + 350U;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        wink_until_ms = now_ms + 620U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 6, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(8);
        spawnHudPopup("SELFIE! +8 XP", 118.0f, 44.0f, COLOR_CYBER_GREEN, 2000U);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        oracle_brain.saveEventSnapshotToSd("GESTURE_VICTORY_SELFIE");
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            static const char* const SELFIE_PHRASES[] = {
                "Selfie tática capturada e salva no álbum do Cartão SD!",
                "Foto registrada na memória externa! Ficou digna de capa de repositório!",
                "Clique cibernético concluído! Mais uma lembrança salva no cartão SD!"
            };
            speakWithLipSync(hal_sensors, SELFIE_PHRASES[(local_phrase_cursor++) % 3U]);
        }
    }

    // 2.8. Ouvido 2.0: Gesto Acústico em Tempo Real (Assobio 🎵) — PC e CoreS3! (Duas Palmas desativadas)
    if (audio_data.whistle_detected && (now_ms - last_acoustic_gesture_ms > 8000U)) {
        last_acoustic_gesture_ms = now_ms;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        wink_until_ms = now_ms + 520U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 5, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(6);
        spawnHudPopup("ASSOBIO! +6 XP", 116.0f, 44.0f, COLOR_CYBER_GREEN, 1900U);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            static const char* const WHISTLE_PHRASES[] = {
                "Ouvi seu assobio sintonizado no microfone! Chamou o copiloto da bancada?",
                "Frequência de assobio detectada! Estou atento por aqui, chefe!",
                "Belo assobio! Meus filtros FFT captaram a nota musical na hora!"
            };
            speakWithLipSync(hal_sensors, WHISTLE_PHRASES[(local_phrase_cursor++) % 3U]);
        }
    }

    // Persiste automaticamente se o minuto de foco ou o humor mudou (ex.: susto ou joinha)
    if (internal_stats.focus_minutes != prev_focus || internal_stats.mood_score != prev_mood) {
        syncPersistentState(hal_sensors);
    }

    // Ouvido: captura o WAV da frase assim que o VAD a encerra. Se o Oráculo estiver ocupado/em cooldown,
    // a frase espera até VOICE_PENDING_MAX_WAIT_MS em vez de ser descartada.
    if (audio_data.vad_phrase_completed) {
        pending_voice_wav_b64 = hal_sensors.recordUserAudioWav();
        pending_voice_until_ms = now_ms + VOICE_PENDING_MAX_WAIT_MS;
        spawnHudPopup("OUVI!", 140.0f, 44.0f, COLOR_CYBER_ICE_LIGHT, 900U);
    } else if (pending_voice_until_ms != 0U && now_ms > pending_voice_until_ms) {
        M5_LOGI("[OUVIDO-VAD] Frase pendente expirou: Oraculo ocupado por mais de %u ms.",
                static_cast<unsigned int>(VOICE_PENDING_MAX_WAIT_MS));
        pending_voice_until_ms = 0U;
        pending_voice_wav_b64.clear();
    }

    // 3. Camada 2: Gatilho do Cérebro Multimodal (Toque, Voz, Objeto na Lente, Intruso, Bocejo, Postura, Celular, Briefing)
    // Mesmo em Modo Silêncio (is_muted_indefinitely), o Bicho continua interagindo e falando por texto na tela!
    if (!oracle_brain.isWaitingAi() && (now_ms - last_oracle_trigger_ms > ORACLE_COOLDOWN_MS)) {
        std::string world_trigger;
        const ExternalWorldContext& world_ctx = world_context_service.getWorldContext();

        if (touch_poke_face || audio_data.interaction_triggered) {
            is_muted_indefinitely = false; // Toque manual explícito encerra o modo silêncio
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            oracle_brain.saveEventSnapshotToSd("TOUCH_POKE");
            triggerSarcasticOracle(hal_sensors, "TOUCH_POKE");
        } else if (audio_data.object_scan_triggered ||
                   (object_close_since_ms > 0 &&
                    (now_ms - object_close_since_ms >= 1400U) &&
                    (now_ms - last_object_inspect_ms >= 45000U))) {
            if (audio_data.object_scan_triggered) {
                is_muted_indefinitely = false;
            }
            last_object_inspect_ms = now_ms;
            object_close_since_ms = 0;
            pip_display_until_ms = now_ms + 9000U;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
            oracle_brain.saveEventSnapshotToSd("OBJECT_INSPECTION");
            triggerSarcasticOracle(hal_sensors, "OBJECT_INSPECTION");
        } else if (pending_voice_until_ms != 0U) {
            pending_voice_until_ms = 0U;
            oracle_brain.recordHabitVoiceInteraction();
            oracle_brain.saveEventSnapshotToSd("VOICE_HANDS_FREE");
            triggerSarcasticOracle(hal_sensors, "VOICE_HANDS_FREE");
        } else if (audio_data.is_loud_peak && audio_data.noise_level_db > 85.0f) {
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_glitch_shock.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "HIGH_NOISE");
        } else if (intruder_since_ms > 0 &&
                   (now_ms - intruder_since_ms >= 1800U) &&
                   (now_ms - last_intruder_alert_ms >= 90000U)) {
            last_intruder_alert_ms = now_ms;
            intruder_since_ms = 0;
            pip_display_until_ms = now_ms + 9000U;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
            oracle_brain.saveEventSnapshotToSd("INTRUDER_DETECTED");
            triggerSarcasticOracle(hal_sensors, "INTRUDER_DETECTED");
        } else if (yawn_since_ms > 0 &&
                   (now_ms - yawn_since_ms >= 1200U) &&
                   (now_ms - last_yawn_alert_ms >= 90000U)) {
            last_yawn_alert_ms = now_ms;
            yawn_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("YAWN_FATIGUE");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "YAWN_FATIGUE");
        } else if (!daily_briefing_done &&
                   hal_sensors.getSdConfig().morning_briefing_enabled &&
                   world_ctx.has_valid_sync &&
                   now_ms > 9000U &&
                   !lip_sync_state.is_speaking) {
            daily_briefing_done = true;
            M5_LOGI("[BRIEFING] Disparando Briefing Tatico da Sessao com clima, dolar e status de repositorio!");
            triggerSarcasticOracle(hal_sensors, "DAILY_BRIEFING");
        } else if (bad_posture_since_ms > 0 &&
                   (now_ms - bad_posture_since_ms >= 5000U) &&
                   (now_ms - last_posture_alert_ms >= 90000U)) {
            last_posture_alert_ms = now_ms;
            bad_posture_since_ms = 0;
            oracle_brain.recordHabitSlouchAlert();
            oracle_brain.saveEventSnapshotToSd("BAD_POSTURE");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "BAD_POSTURE");
        } else if (phone_look_down_since_ms > 0 &&
                   (now_ms - phone_look_down_since_ms >= 4500U) &&
                   (now_ms - last_phone_alert_ms >= 90000U)) {
            last_phone_alert_ms = now_ms;
            phone_look_down_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("PHONE_DOOMSCROLL");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "PHONE_DOOMSCROLL");
        } else if (procrastination_since_ms > 0 &&
                   (now_ms - procrastination_since_ms >= 8000U) &&
                   (now_ms - last_procrastination_alert_ms >= 120000U)) {
            last_procrastination_alert_ms = now_ms;
            procrastination_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("PROCRASTINATION");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "PROCRASTINATION_DETECTED");
        } else if (dark_room_since_ms > 0 &&
                   (now_ms - dark_room_since_ms >= 15000U) &&
                   (now_ms - last_night_owl_alert_ms >= 240000U)) {
            last_night_owl_alert_ms = now_ms;
            dark_room_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("NIGHT_OWL_DARK");
            triggerSarcasticOracle(hal_sensors, "NIGHT_OWL_DARK");
        } else if (internal_stats.focus_minutes > 0 &&
                   (internal_stats.focus_minutes % FOCUS_LIMIT_MINUTES == 0) &&
                   prev_focus != internal_stats.focus_minutes) {
            triggerSarcasticOracle(hal_sensors, "OVERWORK");
        } else if (internal_stats.meeting_minutes >= MEETING_FATIGUE_MINUTES &&
                   audio_data.is_continuous_speech &&
                   (now_ms - last_oracle_trigger_ms > 25000U)) {
            triggerSarcasticOracle(hal_sensors, "LONG_MEETING");
        } else if (world_context_service.checkWorldTriggers(now_ms, world_trigger)) {
            triggerSarcasticOracle(hal_sensors, world_trigger.c_str());
        }
    }

    // 3.0. Diário: virada de dia, capítulos e resumos prontos (fatos duradouros vão para o perfil permanente)
    {
        std::vector<std::string> lasting_facts;
        diary_service.update(now_ms, oracle_brain.isWaitingAi(), lasting_facts);
        for (const std::string& fact : lasting_facts) {
            oracle_brain.addPermanentProfileFact(fact);
        }
    }

    // 3.1. Coleta resposta assíncrona do Cérebro (Gemini Flash-Lite ou Fallback Offline) quando pronta
    // Não fala por cima do usuário: segura a resposta enquanto o VAD mede uma frase em andamento
    // (se essa frase virar uma nova pergunta, a nova consulta substitui a resposta retida)
    OracleBrainResponse brain_result;
    if (!audio_data.vad_speech_active && pending_voice_until_ms == 0U &&
        oracle_brain.pollCompletedResponse(brain_result)) {
        if (!brain_result.new_memory.empty() || !brain_result.permanent_fact.empty()) {
            oracle_brain.addStructuredMemory(
                brain_result.new_memory,
                brain_result.permanent_fact,
                brain_result.memory_tags,
                "",
                internal_stats.mood_score
            );
        }

        // Se o Oráculo detectou alimentação visual na câmera (café, água, energético, lanche)
        if (brain_result.hunger_delta < 0) {
            oracle_brain.recordHabitVisualFeeding();
            oracle_brain.saveEventSnapshotToSd("VISUAL_FEEDING");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        }

        OracleResponsePayload payload;
        std::strncpy(payload.reply_text, brain_result.reply_text.c_str(), sizeof(payload.reply_text) - 1);
        payload.reply_text[sizeof(payload.reply_text) - 1] = '\0';
        payload.target_emotion = brain_result.target_emotion;
        payload.mood_delta = brain_result.mood_delta;
        payload.energy_delta = brain_result.energy_delta;
        payload.hunger_delta = brain_result.hunger_delta;

        M5_LOGI("[CEREBRO-%s] Emo=%d | dHumor=%+d | dEnergia=%+d | dFome=%+d | Mem='%s' | Perm='%s' | Fala='%s'",
                brain_result.from_cloud_api ? "GEMINI-FLASH-LITE" : "OFFLINE",
                static_cast<int>(brain_result.target_emotion),
                static_cast<int>(brain_result.mood_delta),
                static_cast<int>(brain_result.energy_delta),
                static_cast<int>(brain_result.hunger_delta),
                brain_result.new_memory.c_str(),
                brain_result.permanent_fact.c_str(),
                payload.reply_text);

        if (std::strcmp(payload.reply_text, "IGNORE") != 0 &&
            std::strcmp(payload.reply_text, "[IGNORE]") != 0) {
            oracle_brain.recordSpokenPhraseToSd(
                payload.reply_text,
                brain_result.from_cloud_api ? "GEMINI" : "OFFLINE",
                "ORACLE_RESPONSE"
            );
        }

        // Diário: registra a interação (voz com transcrição conforme config; demais gatilhos pelo fato observado)
        if (std::strcmp(payload.reply_text, "IGNORE") != 0 && std::strcmp(payload.reply_text, "[IGNORE]") != 0) {
            const std::string& reason = oracle_brain.getLastTriggerReason();
            if (reason == "VOICE_HANDS_FREE" || reason == "VOICE_COMMAND") {
                diary_service.logConversation(brain_result.user_said, payload.reply_text, brain_result.memory_tags);
            } else if (privacy_mode) {
                diary_service.logEvent(reason.c_str(), "Interação com o Bicho (modo privado)");
            } else if (!brain_result.new_memory.empty()) {
                diary_service.logEvent(reason.c_str(), brain_result.new_memory);
            } else {
                diary_service.logEvent(reason.c_str(), std::string("Bicho comentou: \"") + payload.reply_text + "\"");
            }
        }

        applyOracleResponse(hal_sensors, payload);
    }

    // 4. Atualiza o motor de Lip-Sync (WAV PCM via RMS ou fallback procedural) antes da interpolação facial
    updateLipSyncStep(hal_sensors, now_ms);

    // 5. Atualiza alvos da geometria facial e executa Easing (interpolação suave)
    computeTargetFacePose(gaze_data, now_ms);
    interpolateFacePose();
}

void PetController::updatePresenceAndFocus(const GazeTargetData& gaze_data, const AudioSensorData& audio_data, uint32_t now_ms) {
    // Retorno imediato de ausência: acorda animado ao rever o usuário
    if (gaze_data.face_detected && !was_user_present) {
        was_user_present = true;
        if (internal_stats.absence_minutes >= 2U) {
            diary_service.logEvent("RETURN", "Voltou à mesa depois de " +
                                   std::to_string(internal_stats.absence_minutes) + " min fora");
        }
        internal_stats.absence_minutes = 0;
        if (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) {
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 2500;
        }
    } else if (!gaze_data.face_detected && was_user_present) {
        was_user_present = false;
    }

    // Escala de tempo 1:1 em tempo real (60000 ms = 1 minuto real)
    uint32_t minute_interval_ms = 60000 / TIME_SCALE_SIMULATION;
    if (now_ms - last_minute_tick_ms >= minute_interval_ms) {
        last_minute_tick_ms = now_ms;

        // Desgaste metabólico suave (limitado a 70 passivamente para não travar em EMOTION_GRUMPY sozinho)
        if (internal_stats.focus_minutes > 0 && (internal_stats.focus_minutes % 20 == 0) && internal_stats.hunger_level < 70) {
            internal_stats.hunger_level = clampStatValue(internal_stats.hunger_level + 1, 0, 70);
        }
        // Recuperação passiva de bom humor quando o usuário está trabalhando junto na bancada
        if (gaze_data.face_detected && internal_stats.mood_score < 85 && (internal_stats.focus_minutes % 4 == 0)) {
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 1, 0, MAX_STAT_VALUE);
        }

        if (gaze_data.face_detected) {
            internal_stats.absence_minutes = 0;

            if (!audio_data.is_continuous_speech) {
                // Usuário presente e em silêncio -> incrementa contador de foco livremente e matriz de hábitos no SD
                internal_stats.focus_minutes++;
                oracle_brain.recordHabitFocusMinute();
                if (internal_stats.focus_minutes % 30U == 0U) {
                    diary_service.logEvent("FOCUS", "Acumulou " + std::to_string(internal_stats.focus_minutes) +
                                           " min de foco na sessão");
                }
                if (internal_stats.meeting_minutes > 0) {
                    internal_stats.meeting_minutes--;
                }
            } else {
                // Voz contínua detectada -> incrementa contador de reunião
                internal_stats.meeting_minutes++;
            }
        } else {
            // Usuário ausente (sem rosto detectado)
            internal_stats.absence_minutes++;
            if (internal_stats.absence_minutes == 2U) {
                diary_service.logEvent("LEAVE", "Saiu da mesa");
            }
            if (internal_stats.focus_minutes > 0) {
                internal_stats.focus_minutes--;
            }
        }

        buildStateJson(last_state_json, sizeof(last_state_json));
    }

    // Se o usuário estiver falando um comando curto Hands-Free (VAD), entra imediatamente em ESCUTA ATIVA
    if (audio_data.vad_speech_active && !lip_sync_state.is_speaking) {
        current_emotion = EMOTION_LISTENING;
        return;
    }

    // Avaliação de estados automáticos se não houver emoção temporária travada (susto/oráculo)
    if (now_ms < emotion_lock_until_ms || now_ms < startle_until_ms) {
        return;
    }

    if (!gaze_data.face_detected) {
        if (internal_stats.absence_minutes >= ABSENCE_SLEEP_MINUTES) {
            current_emotion = EMOTION_SLEEPING;
        } else if (internal_stats.absence_minutes >= 1) {
            current_emotion = EMOTION_SLEEPY;
        }
        return;
    }

    if (audio_data.is_continuous_speech) {
        if (internal_stats.meeting_minutes >= MEETING_FATIGUE_MINUTES) {
            current_emotion = EMOTION_JUDGING; // Sobrancelha arqueada irônica em reuniões longas
        } else {
            current_emotion = EMOTION_LISTENING;
        }
        return;
    }

    // Espelhamento de Sorriso Local (0 custo de API): se o usuário sorri para a câmera, o Bicho sorri de volta!
    if (gaze_data.is_smiling) {
        current_emotion = EMOTION_HAPPY;
        return;
    }

    // Se o usuário estiver curvado ("camarão"), colado na tela, mexendo no celular ou em janela de distração, olha com sobrancelha irônica
    if (gaze_data.is_slouching || gaze_data.is_too_close || gaze_data.is_looking_down_phone || procrastination_since_ms > 2500U) {
        current_emotion = EMOTION_JUDGING;
        return;
    }

    // Se estiver em missão Pomodoro ativa, mantém expressão de COMBATE concentrado (EMOTION_FOCUSED)
    if (pomodoro_active) {
        current_emotion = EMOTION_FOCUSED;
        return;
    }

    // Reserva EMOTION_GRUMPY apenas para casos extremos de fome de dados (> 90) ou humor crítico (< 15)
    if (internal_stats.hunger_level >= 90 || internal_stats.mood_score < 15) {
        current_emotion = EMOTION_GRUMPY;
    } else if (internal_stats.focus_minutes >= 6 && ((now_ms / 10000U) % 2U == 1U)) {
        current_emotion = EMOTION_FOCUSED;
    } else if (internal_stats.mood_score >= 60 && ((now_ms / 10000U) % 2U == 0U)) {
        current_emotion = EMOTION_HAPPY;
    } else {
        current_emotion = EMOTION_IDLE;
    }
}

void PetController::updateAcousticReactions(const AudioSensorData& audio_data, const EnvironmentSensorData& env_data, uint32_t now_ms) {
    // Detecta chacoalhão físico via acelerômetro IMU BMI270
    imu_shake_detected = (std::fabs(env_data.accel_x) > 1.55f ||
                          std::fabs(env_data.accel_y) > 1.55f ||
                          std::fabs(env_data.accel_z - 1.0f) > 0.75f);

    if (audio_data.is_loud_peak || imu_shake_detected) {
        current_emotion = imu_shake_detected ? EMOTION_DIZZY : EMOTION_STARTLED;
        startle_until_ms = now_ms + STARTLE_DURATION_MS;
        emotion_lock_until_ms = startle_until_ms;

        // Perde poucos pontos de humor com o susto acústico (nunca zera de uma vez)
        if (audio_data.is_loud_peak) {
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score - 4, 30, MAX_STAT_VALUE);
            buildStateJson(last_state_json, sizeof(last_state_json));
        }
    }

    // Algoritmo de Glitch de Vídeo: quando assustado ou chacoalhado, sorteia 1 a 3 faixas horizontais por 2 a 3 frames
    bool should_glitch = (current_emotion == EMOTION_STARTLED || current_emotion == EMOTION_DIZZY ||
                          now_ms < startle_until_ms || imu_shake_detected);
    if (should_glitch) {
        if (glitch_frames_remaining == 0) {
            glitch_frames_remaining = static_cast<uint8_t>(2U + (now_ms % 2U)); // 2 a 3 frames por pacote de distorção
            active_glitch_band_count = static_cast<uint8_t>(1U + ((now_ms / 3U) % 3U)); // 1 a 3 faixas horizontais
            for (uint8_t i = 0; i < active_glitch_band_count; ++i) {
                uint32_t seed = now_ms + (static_cast<uint32_t>(i) * 73U);
                int32_t b_y = 24 + static_cast<int32_t>((seed * 29U) % (SCREEN_HEIGHT - 64));
                int32_t b_h = 8 + static_cast<int32_t>((seed * 13U) % 16);
                if (b_y + b_h >= SCREEN_HEIGHT) {
                    b_h = SCREEN_HEIGHT - b_y - 1;
                }
                int32_t mag = 5 + static_cast<int32_t>((seed * 17U) % 12);
                int32_t dir = ((seed & 1U) == 0U) ? 1 : -1;
                glitch_bands[i] = {b_y, b_h, mag * dir};
            }
        }
    } else {
        glitch_frames_remaining = 0;
        active_glitch_band_count = 0;
    }
}

void PetController::triggerSarcasticOracle(HalSensors& hal_sensors, const char* custom_reason) {
    uint32_t now_ms = M5.millis();
    last_oracle_trigger_ms = now_ms;
    pip_display_until_ms = now_ms + 7500U; // Exibe o visor PIP de inspeção da câmera por 7.5s

    // Interagir com o Bicho (clique ou voz) reduz a "fome de atenção/dados", melhora o humor e dá +XP!
    if (custom_reason != nullptr &&
        (std::strcmp(custom_reason, "TOUCH_POKE") == 0 || std::strcmp(custom_reason, "VOICE_HANDS_FREE") == 0)) {
        internal_stats.hunger_level = clampStatValue(internal_stats.hunger_level - 15, 0, MAX_STAT_VALUE);
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 6, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(4);
    }
    buildStateJson(last_state_json, sizeof(last_state_json));

    const GazeTargetData& gaze_data = hal_sensors.getGazeData();
    const AudioSensorData& audio_data = hal_sensors.getAudioData();
    const EnvironmentSensorData& env_data = hal_sensors.getEnvironmentData();

    char host_posture_buf[896];
    std::snprintf(
        host_posture_buf,
        sizeof(host_posture_buf),
        "PC Host CPU: %u%% | RAM: %u%% | Janela Ativa: '%s' (%s) | Git Modificados: %u arq | Postura: %s (Calibrada: %s) | Rostos na Câmera: %u%s | Direção Olhar (Yaw): %+.0f graus (%s) | Bocejo/Fadiga: %s | Objeto na Lente: %s | Luz Ambiente: %u lux (%s) | Olhando Celular: %s | Sorriso: %s | Distância Tela: %s | Inclinação Cabeça: %.1f graus | Ouvido DSP: %s (Piso %.0f dB) | Missão Pomodoro: %s | Nível RPG: Lv.%02u (%s)",
        static_cast<unsigned int>(env_data.cpu_usage_percent),
        static_cast<unsigned int>(env_data.ram_usage_percent),
        env_data.active_window_title,
        isDistractionWindow(env_data.active_window_title) ? "DISTRAÇÃO/LAZER" : "PRODUTIVA",
        static_cast<unsigned int>(env_data.git_modified_files),
        gaze_data.is_slouching ? "CURVADO/CAMARÃO" : "ERETA/OK",
        gaze_data.posture_calibrated ? "SIM" : "AUTO",
        static_cast<unsigned int>(gaze_data.face_count),
        (gaze_data.face_count >= 2U) ? " (INTRUSO/2a PESSOA ATRÁS!)" : "",
        gaze_data.head_yaw_deg,
        (std::fabs(gaze_data.head_yaw_deg) > 18.0f) ? "OLHANDO MONITOR LATERAL" : "ENCARANDO O CUBO",
        gaze_data.is_yawning ? "SIM (BOCEJANDO)" : "NÃO",
        gaze_data.object_held_close ? "SIM (MOSTRANDO OBJETO/CANECA/HARDWARE)" : "NÃO",
        static_cast<unsigned int>(env_data.ambient_lux),
        (env_data.ambient_lux < 28U) ? "ESCURO/CORUJÃO" : "ILUMINADO",
        gaze_data.is_looking_down_phone ? "SIM (DOOMSCROLLING)" : "NÃO",
        gaze_data.is_smiling ? "SIM" : "NEUTRO",
        gaze_data.is_too_close ? "MUITO PERTO DO MONITOR" : "NORMAL",
        gaze_data.head_tilt_deg,
        audio_data.is_typing_cadence ? "DIGITANDO TECLADO MECÂNICO"
            : (audio_data.music_beat_active ? "BATIDA MUSICAL DETECTADA"
                : (audio_data.vad_keyword_mode ? "WAKE-WORD ('BICHO')" : "ESCUTA LIVRE")),
        audio_data.adaptive_noise_floor_db,
        pomodoro_active ? "ATIVA (EM COMBATE)" : "INATIVA",
        static_cast<unsigned int>(oracle_brain.getHabitMatrix().cyber_level),
        oracle_brain.getCyberRankTitle()
    );

    OracleContextPacket context_packet;
    context_packet.current_emotion = current_emotion;
    context_packet.mood_score = internal_stats.mood_score;
    context_packet.energy_level = internal_stats.energy_level;
    context_packet.hunger_level = internal_stats.hunger_level;
    context_packet.focus_minutes = internal_stats.focus_minutes;
    context_packet.noise_level_db = audio_data.noise_level_db;
    context_packet.is_user_present = gaze_data.face_detected;
    context_packet.is_in_meeting = audio_data.is_continuous_speech;
    context_packet.is_charging = env_data.is_charging;
    context_packet.vad_keyword_mode = audio_data.vad_keyword_mode;
    context_packet.trigger_reason = (custom_reason != nullptr) ? custom_reason : "TOUCH_POKE";
    context_packet.external_world_summary = world_context_service.buildExternalWorldSummary();
    context_packet.host_and_posture_summary = host_posture_buf;
    context_packet.diary_context_json = diary_service.buildContextJson();
    context_packet.allow_camera_image = !privacy_mode;

#if defined(ARDUINO)
    // No hardware real M5Stack CoreS3, captura o frame JPEG 320x240 diretamente da câmera GC0308 ativa no HalSensors
    if (!privacy_mode) {
        context_packet.jpeg_base64 = hal_sensors.captureCameraJpegBase64();
    }
#endif

    // Captura áudio WAV do microfone APENAS em gatilhos de voz/reunião para evitar que áudio antigo ou ruído sequestre gatilhos visuais
    bool is_voice_trigger = (context_packet.trigger_reason == "VOICE_HANDS_FREE" ||
                             context_packet.trigger_reason == "VOICE_COMMAND" ||
                             context_packet.trigger_reason == "LONG_MEETING");
    if (!is_voice_trigger) {
        context_packet.wav_base64.clear();
    } else if (!pending_voice_wav_b64.empty()) {
        context_packet.wav_base64.swap(pending_voice_wav_b64); // Áudio exato da frase capturado pelo VAD
        pending_voice_wav_b64.clear();
    } else {
        context_packet.wav_base64 = hal_sensors.recordUserAudioWav();
    }

    M5_LOGI("[ORACULO-CEREBRO] Disparando analise assincrona | Motivo: %s | AudioWAV: %u B64 | Host: (%s) | Mundo: (%s)",
            context_packet.trigger_reason.c_str(),
            static_cast<unsigned int>(context_packet.wav_base64.size()),
            context_packet.host_and_posture_summary.c_str(),
            context_packet.external_world_summary.c_str());

    // Dispara o Cérebro Multimodal em background (mantendo a UI a 60 FPS)
    oracle_brain.requestBrainAnalysis(context_packet);
}

void PetController::applyOracleResponse(HalSensors& hal_sensors, const OracleResponsePayload& response_payload) {
    // Se o filtro de Palavra-Chave ("Bicho" / "Oráculo" / "Cubo") detectou que a fala era conversa paralela, ignora silenciosamente!
    if (std::strcmp(response_payload.reply_text, "IGNORE") == 0 ||
        std::strcmp(response_payload.reply_text, "[IGNORE]") == 0) {
        M5_LOGI("[OUVIDO-2.0] Fala descartada pelo filtro de Palavra-Chave (sem 'Bicho'/'Oraculo'/'Cubo' ou conversa paralela).");
        // Feedback visual: o Bicho ouviu, mas entendeu que a fala não era para ele
        spawnHudPopup("NAO ERA COMIGO?", 108.0f, 44.0f, COLOR_CYBER_ICE_LIGHT, 2200U);
        current_emotion = EMOTION_IDLE;
        emotion_lock_until_ms = 0;
        return;
    }

    // Evita que o Gemini coloque EMOTION_GRUMPY sem necessidade se o humor/fome estiverem normais
    PetEmotionState chosen_emo = response_payload.target_emotion;
    if (chosen_emo == EMOTION_GRUMPY && internal_stats.hunger_level < 85 && internal_stats.mood_score >= 30) {
        chosen_emo = EMOTION_JUDGING;
    }
    current_emotion = chosen_emo;
    emotion_lock_until_ms = M5.millis() + 6500;

    int32_t safe_mood_delta = response_payload.mood_delta;
    if (safe_mood_delta < -2) {
        safe_mood_delta = +2; // Sarcasmo amigável diverte o Bicho em vez de deixá-lo deprimido/puto
    }
    internal_stats.mood_score = clampStatValue(internal_stats.mood_score + safe_mood_delta, 35, MAX_STAT_VALUE);
    internal_stats.energy_level = clampStatValue(internal_stats.energy_level + response_payload.energy_delta, 30, MAX_STAT_VALUE);
    internal_stats.hunger_level = clampStatValue(internal_stats.hunger_level + response_payload.hunger_delta, 0, 80);
    buildStateJson(last_state_json, sizeof(last_state_json));

    if (response_payload.hunger_delta < 0) {
        char feed_buf[28];
        std::snprintf(feed_buf, sizeof(feed_buf), "RECARGA %d FOME!", static_cast<int>(response_payload.hunger_delta));
        spawnHudPopup(feed_buf, 110.0f, 44.0f, COLOR_CYBER_GREEN, 2100U);
    } else if (safe_mood_delta > 0) {
        char mood_buf[28];
        std::snprintf(mood_buf, sizeof(mood_buf), "HUMOR +%d%%", static_cast<int>(safe_mood_delta));
        spawnHudPopup(mood_buf, 126.0f, 44.0f, getActiveOpticPrimaryColor(), 1700U);
    }

    // Salva evolução e humor do bicho após cada interação com o Oráculo
    syncPersistentState(hal_sensors);

    // Mesmo em Modo Silêncio (is_muted_indefinitely), speakWithLipSync anima legenda e boca sem emitir som!
    speakWithLipSync(hal_sensors, response_payload.reply_text, false); // Fala única: não vai para o cache
}


void PetController::computeTargetFacePose(const GazeTargetData& gaze_data, uint32_t now_ms) {
    // 1. Micro-Saccades Oculares Naturais + Sacadas Rápidas de Foco (micro_saccade_offset_x / micro_saccade_offset_y)
    if (now_ms >= next_saccade_ms) {
        next_saccade_ms = now_ms + 900U + ((now_ms * 37U) % 1800U);
        if (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) {
            saccade_offset_x = 0.0f;
            saccade_offset_y = 0.0f;
        } else {
            int32_t rx = static_cast<int32_t>((now_ms * 19U) % 5U) - 2; // -2 a +2 px
            int32_t ry = static_cast<int32_t>((now_ms * 29U) % 3U) - 1; // -1 a +1 px
            saccade_offset_x = static_cast<float>(rx) * 0.85f;
            saccade_offset_y = static_cast<float>(ry) * 0.75f;
        }
    }

    // 1.1. Sacadas Oculares de "Vida" em Foco (a cada 2500..4500 ms, deslocamento rápido de -2 a +2 px por 180 ms)
    if (gaze_data.face_detected && (current_emotion == EMOTION_FOCUSED || current_emotion == EMOTION_IDLE)) {
        if (now_ms >= next_micro_saccade_ms) {
            next_micro_saccade_ms = now_ms + 2500U + ((now_ms * 43U) % 2001U);
            micro_saccade_until_ms = now_ms + 180U;
            int32_t ms_x = static_cast<int32_t>((now_ms * 23U) % 5U) - 2; // -2 a +2 px
            int32_t ms_y = static_cast<int32_t>((now_ms * 31U) % 5U) - 2; // -2 a +2 px
            if (ms_x == 0 && ms_y == 0) {
                ms_x = ((now_ms & 1U) != 0U) ? 2 : -2;
            }
            micro_saccade_offset_x = static_cast<float>(ms_x);
            micro_saccade_offset_y = static_cast<float>(ms_y);
        }
    }
    if (now_ms >= micro_saccade_until_ms) {
        micro_saccade_offset_x = 0.0f;
        micro_saccade_offset_y = 0.0f;
    }

    // 1.2. Dilatação Pupilar Reativa à Luz Ambiente (env_data.ambient_lux suavizado por interpolação linear)
    float target_lux_offset = 0.0f;
    if (cached_env_data.ambient_lux > 0U && cached_env_data.ambient_lux < 80U) {
        target_lux_offset = 3.0f;  // Ambiente escuro (< 80 lux): dilata a pupila (+3 px no raio)
    } else if (cached_env_data.ambient_lux > 650U) {
        target_lux_offset = -2.0f; // Ambiente muito iluminado (> 650 lux): contrai a pupila (-2 px no raio)
    }
    pupil_lux_radius_offset += (target_lux_offset - pupil_lux_radius_offset) * 0.08f;

    // 1.3. Olhar de Soslaio (Side-Eye) para o Monitor Lateral (> 5000 ms contínuos olhando para o lado)
    bool looking_side_monitor = gaze_data.face_detected &&
                                (std::fabs(gaze_data.head_yaw_deg) > 18.0f || std::fabs(gaze_data.target_x) > 0.45f);
    if (looking_side_monitor) {
        if (side_monitor_since_ms == 0U) {
            side_monitor_since_ms = now_ms;
        }
        if (now_ms - side_monitor_since_ms > 5000U) {
            is_side_eye_active = true;
            if (std::fabs(gaze_data.target_x) > 0.15f) {
                side_eye_dir = (gaze_data.target_x > 0.0f) ? 1.0f : -1.0f;
            } else {
                side_eye_dir = (gaze_data.head_yaw_deg > 0.0f) ? 1.0f : -1.0f;
            }
        }
    } else {
        side_monitor_since_ms = 0U;
        is_side_eye_active = false;
    }

    // Alvo do rastreamento ocular normalizado [-1.0, 1.0] + micro-saccades e inclinação da cabeça (head roll)
    target_pose.current_eye_x = (gaze_data.target_x * FACE_GAZE_MAX_X) + saccade_offset_x + micro_saccade_offset_x;
    target_pose.current_eye_y = (gaze_data.target_y * FACE_GAZE_MAX_Y) + saccade_offset_y + micro_saccade_offset_y;

    // Reatividade em tempo real à digitação: olhos varrem horizontalmente como se estivessem lendo o código!
    if ((cached_audio_data.is_typing_active || cached_audio_data.is_typing_cadence) &&
        current_emotion != EMOTION_SLEEPING && current_emotion != EMOTION_STARTLED) {
        float scan_phase = std::fmod(static_cast<float>(now_ms) * 0.0032f, 1.0f);
        float read_sweep_x = (scan_phase * 2.0f - 1.0f) * 10.5f; // Varre da esquerda para a direita e salta de volta (carriage return)
        target_pose.current_eye_x = (target_pose.current_eye_x * 0.35f) + read_sweep_x + micro_saccade_offset_x;
        target_pose.current_eye_y = (target_pose.current_eye_y * 0.45f) + 3.5f + micro_saccade_offset_y;
    }

    if (is_side_eye_active && current_emotion != EMOTION_SLEEPING && current_emotion != EMOTION_STARTLED) {
        target_pose.current_eye_x = side_eye_dir * FACE_GAZE_MAX_X;
    }

    target_pose.eye_offset_x = target_pose.current_eye_x;
    target_pose.eye_offset_y = target_pose.current_eye_y;

    float clamped_tilt = gaze_data.head_tilt_deg;
    if (clamped_tilt < -25.0f) clamped_tilt = -25.0f;
    if (clamped_tilt > 25.0f)  clamped_tilt = 25.0f;
    target_pose.head_tilt_deg = gaze_data.face_detected ? clamped_tilt : 0.0f;

    // Respiração / Flutuação Orgânica contínua (mais lenta dormindo, mais viva acordado)
    float breath_speed = (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) ? 0.0018f : 0.0034f;
    float breath_amp   = (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) ? 2.4f : 1.6f;
    target_pose.breathing_offset_y = std::sin(static_cast<float>(now_ms) * breath_speed) * breath_amp;

    // 2. Controle de piscada natural aleatória + squash & stretch pós-piscada + piscadinha cúmplice (wink)
    if (!is_blinking && (now_ms - last_blink_ms > next_blink_interval_ms)) {
        is_blinking = true;
        last_blink_ms = now_ms;
    } else if (is_blinking && (now_ms - last_blink_ms > 125)) {
        is_blinking = false;
        last_blink_ms = now_ms;
        blink_bounce_until_ms = now_ms + 130U;
        next_blink_interval_ms = 2400 + ((now_ms * 17) % 2600);
    }

    // Piscadinha espontânea de cumplicidade quando o usuário sorri ou o humor está alto
    if (now_ms >= next_wink_ms) {
        if ((gaze_data.face_detected && gaze_data.is_smiling) ||
            (current_emotion == EMOTION_HAPPY && internal_stats.mood_score >= 75)) {
            wink_until_ms = now_ms + 420U;
        }
        next_wink_ms = now_ms + 9500U + ((now_ms * 29U) % 8500U);
    }

    // 3. Configuração padrão da pose + Dilatação Reativa da Pupila (fala/sorriso/foco + luz ambiente)
    float pupil_react = 0.0f;
    if (lip_sync_state.is_speaking || current_emotion == EMOTION_LISTENING || gaze_data.is_smiling || (now_ms < head_pat_until_ms)) {
        pupil_react = 1.8f;
    } else if (pomodoro_active || current_emotion == EMOTION_FOCUSED || cached_audio_data.is_typing_cadence) {
        pupil_react = -1.0f;
    }

    target_pose.eye_radius_current = FACE_EYE_DEFAULT_R;
    target_pose.eye_scale_x = 1.0f;
    target_pose.eye_scale_y = 1.0f;
    target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R + pupil_react + pupil_lux_radius_offset;
    target_pose.upper_eyelid_closure = 0.0f;  // Olhos totalmente abertos por padrão
    target_pose.lower_eyelid_closure = 0.16f; // Curvatura inferior sorridente e simpática
    target_pose.eyebrow_angle_deg = -5.0f;    // Sobrancelhas levemente erguidas no centro (/ \), expressão amigável
    target_pose.eyebrow_offset_y = -2.5f;
    target_pose.mouth_width_current = 18.0f;
    target_pose.mouth_curve_factor = 0.52f;   // Sorriso claro de companheiro de bancada
    target_pose.cheek_glow_intensity = (gaze_data.face_detected && gaze_data.is_smiling) ? 0.65f : 0.0f;
    target_pose.shake_offset_x = 0.0f;
    target_pose.shake_offset_y = 0.0f;

    switch (current_emotion) {
        case EMOTION_STARTLED:
            // Pupilas e olhos esticam verticalmente no susto (Squash & Stretch) + tremor intenso
            target_pose.eye_radius_current = FACE_EYE_DEFAULT_R + 3.0f;
            target_pose.eye_scale_x = 0.90f;
            target_pose.eye_scale_y = 1.18f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R - 2.5f + pupil_lux_radius_offset; // Pupila contrai em choque!
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_offset_y = -6.5f;
            target_pose.eyebrow_angle_deg = -15.0f;
            target_pose.mouth_width_current = 20.0f;
            target_pose.mouth_curve_factor = -0.68f;
            target_pose.shake_offset_x = (now_ms % 2 == 0) ? 4.5f : -4.5f;
            target_pose.shake_offset_y = ((now_ms / 2) % 2 == 0) ? 3.0f : -3.0f;
            break;

        case EMOTION_JUDGING:
            // Expressão irônica/marota estilo JARVIS: olho semi-cerrado desconfiado, sobrancelha direita arqueada lá no alto e smirk!
            target_pose.eye_scale_x = 1.05f;
            target_pose.eye_scale_y = 0.94f;
            target_pose.upper_eyelid_closure = 0.14f;
            target_pose.lower_eyelid_closure = 0.24f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = -2.0f;
            target_pose.mouth_width_current = 19.0f;
            target_pose.mouth_curve_factor = 0.46f;
            break;

        case EMOTION_GRUMPY:
            // Sobrancelhas franzidas em V (reservado APENAS quando estiver realmente faminto > 90 ou humor < 15)
            target_pose.eye_scale_x = 1.06f;
            target_pose.eye_scale_y = 0.90f;
            target_pose.upper_eyelid_closure = 0.32f;
            target_pose.lower_eyelid_closure = 0.08f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R - 1.5f + pupil_lux_radius_offset;
            target_pose.eyebrow_angle_deg = 15.0f;
            target_pose.eyebrow_offset_y = 3.0f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = -0.62f;
            target_pose.shake_offset_x = (now_ms % 6 == 0) ? 1.2f : 0.0f;
            break;

        case EMOTION_FOCUSED:
            // Olhar atento, vivo e amigável de parceiro de programação
            target_pose.eye_scale_x = 1.03f;
            target_pose.eye_scale_y = 0.97f;
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.18f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = -2.0f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = 0.42f;
            break;

        case EMOTION_HAPPY:
            // Pálpebra inferior sobe em arco formando olhos sorridentes (^ ^), bochechas acesas e sorriso largo
            target_pose.eye_scale_x = 1.08f;
            target_pose.eye_scale_y = 0.93f;
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.48f;
            target_pose.eyebrow_offset_y = -5.0f;
            target_pose.eyebrow_angle_deg = -9.0f;
            target_pose.mouth_width_current = 22.0f;
            target_pose.mouth_curve_factor = 0.96f;
            target_pose.cheek_glow_intensity = 0.92f;
            break;

        case EMOTION_LISTENING:
            // Olhos bem abertos e curiosos prestando atenção na voz do usuário + leve inclinação curiosa
            target_pose.eye_scale_x = 1.04f;
            target_pose.eye_scale_y = 1.08f;
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.20f;
            target_pose.eyebrow_angle_deg = -7.0f;
            target_pose.eyebrow_offset_y = -3.5f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = 0.52f;
            target_pose.head_tilt_deg += 4.5f;
            break;

        case EMOTION_SLEEPY:
            // Bocejando / quase dormindo
            target_pose.eye_scale_x = 1.04f;
            target_pose.eye_scale_y = 0.88f;
            target_pose.upper_eyelid_closure = 0.64f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = 3.0f;
            target_pose.mouth_curve_factor = 0.0f;
            if (!lip_sync_state.is_speaking) {
                target_pose.mouth_open_height = 11.0f;
                target_pose.mouth_width_current = 16.0f;
            }
            break;

        case EMOTION_SLEEPING:
            // Olhos totalmente fechados e respiração lenta tranquila
            target_pose.eye_scale_x = 1.06f;
            target_pose.eye_scale_y = 0.84f;
            target_pose.upper_eyelid_closure = 0.94f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_angle_deg = -2.0f;
            target_pose.eyebrow_offset_y = 4.0f;
            target_pose.mouth_width_current = 14.0f;
            target_pose.mouth_curve_factor = 0.28f;
            break;

        case EMOTION_DIZZY:
            // Sobrecarga / tontura com oscilação orbital das pupilas, espirais e olhos assimétricos
            target_pose.eye_radius_current = FACE_EYE_DEFAULT_R + 2.0f;
            target_pose.eye_scale_x = 1.0f + std::sin(static_cast<float>(now_ms) * 0.018f) * 0.12f;
            target_pose.eye_scale_y = 1.0f + std::cos(static_cast<float>(now_ms) * 0.018f) * 0.12f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R - 1.5f + pupil_lux_radius_offset;
            target_pose.upper_eyelid_closure = 0.15f;
            target_pose.eyebrow_angle_deg = -14.0f;
            target_pose.eyebrow_offset_y = -2.0f;
            target_pose.current_eye_x = std::sin(static_cast<float>(now_ms) * 0.025f) * FACE_GAZE_MAX_X;
            target_pose.current_eye_y = std::cos(static_cast<float>(now_ms) * 0.025f) * FACE_GAZE_MAX_Y;
            target_pose.shake_offset_x = (now_ms % 3 == 0) ? 3.5f : -3.5f;
            target_pose.shake_offset_y = ((now_ms / 3) % 2 == 0) ? 2.5f : -2.5f;
            break;

        case EMOTION_IDLE:
        case EMOTION_NEUTRAL:
        default:
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.16f;
            target_pose.eyebrow_angle_deg = -5.0f;
            target_pose.eyebrow_offset_y = -2.5f;
            target_pose.mouth_curve_factor = 0.52f;
            break;
    }

    // Reação imediata ao Cafuné / Carinho na cabeça: fecha os olhos em arco feliz (^ ^), inclina a cabeça e acende as bochechas!
    if (now_ms < head_pat_until_ms) {
        target_pose.eye_scale_x = 1.12f;
        target_pose.eye_scale_y = 0.88f;
        target_pose.upper_eyelid_closure = 0.0f;
        target_pose.lower_eyelid_closure = 0.62f;
        target_pose.eyebrow_offset_y = -5.5f;
        target_pose.eyebrow_angle_deg = -10.0f;
        target_pose.mouth_curve_factor = 1.0f;
        target_pose.cheek_glow_intensity = 1.0f;
        target_pose.head_tilt_deg = std::sin(static_cast<float>(now_ms) * 0.012f) * 8.5f;
    }

    // Olhar de Soslaio (Side-Eye): estreita levemente as pálpebras (+4 px de fechamento superior/inferior)
    if (is_side_eye_active && current_emotion != EMOTION_SLEEPING && current_emotion != EMOTION_STARTLED) {
        constexpr float side_eye_lid_delta = 4.0f / FACE_EYE_RY_DEFAULT; // +4 px de fechamento
        target_pose.upper_eyelid_closure = std::fmin(0.75f, target_pose.upper_eyelid_closure + side_eye_lid_delta);
        target_pose.lower_eyelid_closure = std::fmin(0.75f, target_pose.lower_eyelid_closure + side_eye_lid_delta);
    }

    // Efeito "Espelho Físico" de Postura (gaze_data.is_slouching): achata levemente a altura vertical dos olhos (-15%)
    if (gaze_data.face_detected && gaze_data.is_slouching) {
        target_pose.eye_scale_y *= 0.85f;
    }

    // Modo DJ Silencioso (Sincronia com BPM Real): bounce_offset_y = sin(now_ms * TWO_PI / music_beat_interval_ms) * 3.0f
    if (cached_audio_data.is_music_active && cached_audio_data.music_beat_interval_ms > 0U && current_emotion != EMOTION_SLEEPY) {
        constexpr float two_pi = 6.2831853f;
        bounce_offset_y = std::sin(static_cast<float>(now_ms) * two_pi / static_cast<float>(cached_audio_data.music_beat_interval_ms)) * 3.0f;
        float beat_pulse = std::fabs(bounce_offset_y) / 3.0f;
        target_pose.eyebrow_offset_y += bounce_offset_y;
        if (!lip_sync_state.is_speaking) {
            target_pose.head_tilt_deg += (bounce_offset_y / 3.0f) * 4.8f;
            target_pose.eye_scale_x += beat_pulse * 0.08f;
            target_pose.eye_scale_y += beat_pulse * 0.08f;
            target_pose.mouth_curve_factor = std::fmax(target_pose.mouth_curve_factor, 0.82f);
            target_pose.cheek_glow_intensity = std::fmax(target_pose.cheek_glow_intensity, 0.55f);
        }
    } else {
        bounce_offset_y = 0.0f;
    }

    // Squash & Stretch das pálpebras e lentes durante e logo após a piscada (estilo animação Disney/Cozmo)
    if (is_blinking) {
        target_pose.upper_eyelid_closure = 0.96f;
        target_pose.eye_scale_x = 1.14f;
        target_pose.eye_scale_y = 0.78f;
    } else if (now_ms < blink_bounce_until_ms) {
        target_pose.eye_scale_x = 0.94f;
        target_pose.eye_scale_y = 1.09f;
    }

    // Inicializa controles independentes esquerdo/direito a partir da base simétrica e aplica assimetrias expressivas
    target_pose.left_upper_lid        = target_pose.upper_eyelid_closure;
    target_pose.right_upper_lid       = target_pose.upper_eyelid_closure;
    target_pose.left_lower_lid        = target_pose.lower_eyelid_closure;
    target_pose.right_lower_lid       = target_pose.lower_eyelid_closure;
    target_pose.left_brow_angle_deg   = target_pose.eyebrow_angle_deg;
    target_pose.right_brow_angle_deg  = -target_pose.eyebrow_angle_deg;
    target_pose.left_brow_offset_y    = target_pose.eyebrow_offset_y;
    target_pose.right_brow_offset_y   = target_pose.eyebrow_offset_y;

    if (current_emotion == EMOTION_JUDGING) {
        // Olho esquerdo semi-cerrado analisando e olho direito bem aberto com sobrancelha arqueada lá no alto!
        target_pose.left_upper_lid       = std::fmax(target_pose.left_upper_lid, 0.28f);
        target_pose.right_upper_lid      = 0.0f;
        target_pose.right_lower_lid      = 0.26f;
        target_pose.left_brow_angle_deg  = -2.0f;
        target_pose.right_brow_angle_deg = 11.0f;
        target_pose.left_brow_offset_y   = -1.0f;
        target_pose.right_brow_offset_y  = -7.5f;
    } else if (current_emotion == EMOTION_LISTENING || cached_audio_data.is_typing_cadence) {
        // Leve assimetria curiosa nas sobrancelhas quando escutando ou lendo código
        target_pose.right_brow_offset_y -= 2.5f;
        target_pose.right_brow_angle_deg += 4.5f;
    } else if (current_emotion == EMOTION_DIZZY) {
        // Pálpebras e sobrancelhas desalinhadas em curto-circuito
        target_pose.left_upper_lid       = 0.32f;
        target_pose.right_upper_lid      = 0.04f;
        target_pose.left_brow_offset_y   = 2.0f;
        target_pose.right_brow_offset_y  = -5.0f;
    }

    // Piscadinha de um olho só (Wink no olho direito!) quando ativada por sorriso/cumplicidade
    if (now_ms < wink_until_ms && !is_blinking && current_emotion != EMOTION_SLEEPING) {
        target_pose.right_upper_lid      = 0.88f;
        target_pose.right_lower_lid      = 0.45f;
        target_pose.right_brow_offset_y += 2.5f;
        target_pose.mouth_curve_factor   = std::fmax(target_pose.mouth_curve_factor, 0.85f);
        target_pose.cheek_glow_intensity = std::fmax(target_pose.cheek_glow_intensity, 0.75f);
    }
}

void PetController::interpolateFacePose() {
    auto lerp_val = [](float current_v, float target_v, float factor) -> float {
        return current_v + (target_v - current_v) * factor;
    };

    current_pose.current_eye_x        = lerp_val(current_pose.current_eye_x, target_pose.current_eye_x, FACE_EASING_SPEED);
    current_pose.current_eye_y        = lerp_val(current_pose.current_eye_y, target_pose.current_eye_y, FACE_EASING_SPEED);
    current_pose.eye_offset_x         = current_pose.current_eye_x;
    current_pose.eye_offset_y         = current_pose.current_eye_y;
    current_pose.eye_radius_current   = lerp_val(current_pose.eye_radius_current, target_pose.eye_radius_current, 0.24f);
    current_pose.eye_scale_x          = lerp_val(current_pose.eye_scale_x, target_pose.eye_scale_x, 0.32f);
    current_pose.eye_scale_y          = lerp_val(current_pose.eye_scale_y, target_pose.eye_scale_y, 0.32f);
    current_pose.pupil_radius_current = lerp_val(current_pose.pupil_radius_current, target_pose.pupil_radius_current, 0.28f);
    current_pose.upper_eyelid_closure = lerp_val(current_pose.upper_eyelid_closure, target_pose.upper_eyelid_closure, 0.34f);
    current_pose.lower_eyelid_closure = lerp_val(current_pose.lower_eyelid_closure, target_pose.lower_eyelid_closure, 0.28f);
    current_pose.left_upper_lid       = lerp_val(current_pose.left_upper_lid, target_pose.left_upper_lid, 0.34f);
    current_pose.right_upper_lid      = lerp_val(current_pose.right_upper_lid, target_pose.right_upper_lid, 0.34f);
    current_pose.left_lower_lid       = lerp_val(current_pose.left_lower_lid, target_pose.left_lower_lid, 0.28f);
    current_pose.right_lower_lid      = lerp_val(current_pose.right_lower_lid, target_pose.right_lower_lid, 0.28f);
    current_pose.eyebrow_angle_deg    = lerp_val(current_pose.eyebrow_angle_deg, target_pose.eyebrow_angle_deg, 0.24f);
    current_pose.eyebrow_offset_y     = lerp_val(current_pose.eyebrow_offset_y, target_pose.eyebrow_offset_y, 0.24f);
    current_pose.left_brow_angle_deg  = lerp_val(current_pose.left_brow_angle_deg, target_pose.left_brow_angle_deg, 0.24f);
    current_pose.right_brow_angle_deg = lerp_val(current_pose.right_brow_angle_deg, target_pose.right_brow_angle_deg, 0.24f);
    current_pose.left_brow_offset_y   = lerp_val(current_pose.left_brow_offset_y, target_pose.left_brow_offset_y, 0.24f);
    current_pose.right_brow_offset_y  = lerp_val(current_pose.right_brow_offset_y, target_pose.right_brow_offset_y, 0.24f);
    current_pose.head_tilt_deg        = lerp_val(current_pose.head_tilt_deg, target_pose.head_tilt_deg, 0.24f);
    current_pose.breathing_offset_y   = lerp_val(current_pose.breathing_offset_y, target_pose.breathing_offset_y, 0.25f);
    current_pose.cheek_glow_intensity = lerp_val(current_pose.cheek_glow_intensity, target_pose.cheek_glow_intensity, 0.22f);
    current_pose.mouth_open_height    = lerp_val(current_pose.mouth_open_height, target_pose.mouth_open_height, 0.44f);
    current_pose.mouth_width_current  = lerp_val(current_pose.mouth_width_current, target_pose.mouth_width_current, 0.25f);
    current_pose.mouth_curve_factor   = lerp_val(current_pose.mouth_curve_factor, target_pose.mouth_curve_factor, 0.24f);
    current_pose.shake_offset_x       = target_pose.shake_offset_x;
    current_pose.shake_offset_y       = target_pose.shake_offset_y;

    // Interpola suavemente a deformação física progressiva de postura curvada (slouch_drop_y até +18 px)
    float target_slouch_drop = was_slouching_pose ? 18.0f : 0.0f;
    slouch_drop_y = lerp_val(slouch_drop_y, target_slouch_drop, 0.14f);
}

bool PetController::isRainingOnVisor() const {
    return world_context_service.getWorldContext().is_raining || rain_simulation_override;
}

void PetController::initVisorRainDrops() {
    for (uint8_t i = 0; i < VISOR_RAINDROP_COUNT; ++i) {
        VisorRainDrop& d = visor_raindrops[i];
        uint32_t seed = (static_cast<uint32_t>(i) + 1U) * 2654435761U;
        d.x = 14.0f + static_cast<float>(seed % 292U);
        d.y = 26.0f + static_cast<float>((seed >> 8) % 132U);
        d.trail_start_y = d.y - static_cast<float>(6U + ((seed >> 14) % 24U));
        if (d.trail_start_y < 24.0f) {
            d.trail_start_y = 24.0f;
        }
        d.radius = static_cast<uint8_t>(2U + ((seed >> 19) % 3U)); // 2 a 4 px
        d.base_vy = 0.55f + (static_cast<float>((seed >> 22) % 100U) / 100.0f) * 1.15f;
        d.vy = d.base_vy;
        d.meander_phase = static_cast<float>((seed >> 5) % 628U) * 0.01f;
        d.is_sliding = ((i % 3U) != 0U);
        d.next_slip_ms = 0U;
        d.trail_len = static_cast<uint8_t>(d.y - d.trail_start_y);
        d.splash_frames = 0U;
    }
}

void PetController::updateVisorRainDrops(uint32_t now_ms) {
    for (uint8_t i = 0; i < VISOR_RAINDROP_COUNT; ++i) {
        VisorRainDrop& d = visor_raindrops[i];

        // Se a gota atingiu a borda da caixa de diálogo ou base da tela, anima o splash antes de renascer no topo
        if (d.splash_frames > 0U) {
            d.splash_frames--;
            if (d.splash_frames == 0U) {
                uint32_t rseed = now_ms * 1664525U + (static_cast<uint32_t>(i) * 1013904223U);
                d.x = 12.0f + static_cast<float>(rseed % 296U);
                d.y = 25.0f + static_cast<float>((rseed >> 9) % 36U);
                d.trail_start_y = d.y;
                d.trail_len = 0U;
                d.radius = static_cast<uint8_t>(2U + ((rseed >> 16) % 3U));
                d.base_vy = 0.60f + (static_cast<float>((rseed >> 20) % 100U) / 100.0f) * 1.25f;
                d.vy = d.base_vy * 0.25f;
                d.is_sliding = false;
                d.next_slip_ms = now_ms + 180U + ((rseed >> 12) % 620U);
            }
            continue;
        }

        // Física de tensão superficial no vidro: alterna entre aderência/acúmulo lento e escorrimento rápido por gravidade
        if (now_ms >= d.next_slip_ms) {
            uint32_t h = now_ms * 747796405U + (static_cast<uint32_t>(i) * 2891336453U);
            if (!d.is_sliding) {
                d.is_sliding = true;
                d.vy = d.base_vy * (1.35f + static_cast<float>(d.radius) * 0.28f);
                d.next_slip_ms = now_ms + 320U + (h % 680U);
            } else {
                // Pequena freada por tensão superficial deixando micro-gotas pelo caminho
                d.is_sliding = ((h & 3U) == 0U);
                d.vy = d.is_sliding ? (d.base_vy * 0.85f) : (d.base_vy * 0.12f);
                d.next_slip_ms = now_ms + 140U + ((h >> 7) % 480U);
            }
        }

        d.y += d.vy;
        if (d.is_sliding) {
            float meander = std::sin(d.y * 0.09f + d.meander_phase) * 0.32f;
            d.x += meander;
            if (d.x < 10.0f) d.x = 10.0f;
            if (d.x > static_cast<float>(SCREEN_WIDTH - 10)) d.x = static_cast<float>(SCREEN_WIDTH - 10);
        }

        float raw_trail = d.y - d.trail_start_y;
        if (raw_trail > 42.0f) {
            d.trail_start_y = d.y - 42.0f;
            raw_trail = 42.0f;
        }
        d.trail_len = static_cast<uint8_t>(raw_trail > 0.0f ? raw_trail : 0.0f);

        // Colisão e respingo (splash) na borda superior da Caixa de Diálogo Tática (Y = 167) ou no rodapé externo
        bool hit_dialogue_rim = (d.x >= 10.0f && d.x <= 310.0f && d.y >= 166.0f && d.y <= 172.0f);
        bool hit_screen_bottom = (d.y >= static_cast<float>(SCREEN_HEIGHT - 5));
        if (hit_dialogue_rim || hit_screen_bottom) {
            if (hit_dialogue_rim) {
                d.y = 167.0f;
            }
            d.splash_frames = 7U;
        }
    }
}


void PetController::setDebugMode(bool enabled) {
    debug_mode = enabled;
}

bool PetController::isDebugMode() const {
    return debug_mode;
}

PetEmotionState PetController::getCurrentEmotion() const {
    return current_emotion;
}

const char* PetController::getEmotionLabel() const {
    if (oracle_brain.isWaitingAi()) {
        return "PROCESSANDO";
    }
    switch (current_emotion) {
        case EMOTION_IDLE:      return "EM ORBITA";
        case EMOTION_FOCUSED:   return "EM FOCO";
        case EMOTION_HAPPY:     return "PARCEIRO OK";
        case EMOTION_STARTLED:  return "ALERTA MAX!";
        case EMOTION_LISTENING: return "ESCUTA ATIVA";
        case EMOTION_JUDGING:   return "CURIOSO";
        case EMOTION_GRUMPY:    return "IMPACIENTE";
        case EMOTION_SLEEPY:    return "BAIXA ENERG";
        case EMOTION_SLEEPING:  return "HIBERNANDO";
        case EMOTION_DIZZY:     return "SOBRECARGA";
        case EMOTION_NEUTRAL:
        default:                return "SISTEMA OK";
    }
}

const PetInternalStats& PetController::getInternalStats() const {
    return internal_stats;
}

const LipSyncState& PetController::getLipSyncState() const {
    return lip_sync_state;
}

const char* PetController::getLastStateJson() const {
    return last_state_json;
}
