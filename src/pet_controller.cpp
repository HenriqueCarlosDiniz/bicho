#include "pet_state.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(ARDUINO)
#include <SD.h>
#else
#include <fstream>
#include <string>
#endif

// =============================================================================
// Constantes Visuais do Rosto "Cyber-Guerreiro" e HUD Militar (UPPER_CASE)
// =============================================================================
constexpr int32_t FACE_EYE_LEFT_X = 96;
constexpr int32_t FACE_EYE_RIGHT_X = 224;
constexpr int32_t FACE_EYE_BASE_Y = 82;
constexpr int32_t FACE_MOUTH_BASE_Y = 138;
constexpr float FACE_EYE_DEFAULT_R = 25.0f;   // Lentes ópticas de combate (~50x48 px) sem sobrepor callouts
constexpr float FACE_EYE_RX_DEFAULT = 26.0f;  // Semieixo horizontal
constexpr float FACE_EYE_RY_DEFAULT = 23.0f;  // Semieixo vertical
constexpr float FACE_PUPIL_DEFAULT_R = 9.0f;
constexpr float FACE_GAZE_MAX_X = 14.0f;
constexpr float FACE_GAZE_MAX_Y = 9.0f;
constexpr float FACE_EASING_SPEED = 0.24f;    // Resposta mais travada e rápida estilo servo de robô

// 1. Paleta do Grid Cibernético e Atmosfera de Fundo (RGB565)
constexpr uint16_t COLOR_CYBER_BG_NAVY   = 0x0843;   // Azul-marinho digital muito escuro (#080818)
constexpr uint16_t COLOR_HUD_PANEL_BG    = 0x0842;   // Fundo de painéis HUD azul-noite profundo (#080810, zero verde!)
constexpr uint16_t COLOR_HUD_INNER_BG    = 0x0863;   // Fundo interno da caixa de diálogo (#080C18)
constexpr uint16_t COLOR_HUD_BADGE_BG    = 0x10A5;   // Fundo de abas e badges em azul-aço escuro (#101428)
constexpr uint16_t COLOR_CYBER_GRID_BLUE = 0x1949;   // Azul semitransparente da grade 3D (#1E2A4A)
constexpr uint16_t COLOR_CYBER_GRID_DIM  = 0x10A5;   // Linha secundária de profundidade do grid
constexpr uint16_t COLOR_CYBER_BLUE_NEON = 0x05FF;   // Azul cibernético vibrante para HUD, retículos e bordas
constexpr uint16_t COLOR_CYBER_ICE_LIGHT = 0x9FFF;   // Azul-gelo suave para destaques harmônicos (substitui amarelo gritante)
constexpr uint16_t COLOR_CYBER_BLUE_DARK = 0x0966;   // Fundo escuro de painéis táticos azuis

// 2. Paleta de Combate: Lentes Ópticas, Laser, Alertas e Telemetria (RGB565)
constexpr uint16_t COLOR_OPTIC_NEON_RED  = 0xF966;   // Vermelho neon vibrante das lentes ópticas (#FF3333 / 0xF966)
constexpr uint16_t COLOR_WARRIOR_RED     = 0xE267;   // Vermelho intenso do badge militar (#E74C3C)
constexpr uint16_t COLOR_OPTIC_GLOW_DARK = 0x4821;   // Halo térmico vermelho escuro ao redor das lentes
constexpr uint16_t COLOR_OPTIC_GLOW_MID  = 0x9082;   // Halo térmico intermediário
constexpr uint16_t COLOR_OPTIC_CORE_DARK = 0x2000;   // Fundo interno escuro da câmara óptica
constexpr uint16_t COLOR_ARMOR_VISOR     = 0x2126;   // Moldura metálica azul-ardósia escura da viseira cibernética
constexpr uint16_t COLOR_ARMOR_HIGHLIGHT = 0x52AA;   // Chanfro iluminado da armadura óptica
constexpr uint16_t COLOR_CYBER_ORANGE    = 0xFD20;   // Âmbar dourado tático
constexpr uint16_t COLOR_CYBER_YELLOW    = 0xFFE0;   // Amarelo de warning eletrônico
constexpr uint16_t COLOR_SONAR_GREEN     = 0x07E8;   // Verde esmeralda tático
constexpr uint16_t COLOR_TEXT_SHADOW     = 0x0000;   // Sombra projetada preta

// Mantém constantes originais para compatibilidade de escopo
constexpr uint16_t COLOR_CANVAS_BG       = COLOR_CYBER_BG_NAVY;
constexpr uint16_t COLOR_BADGE_BORDER    = COLOR_CYBER_BLUE_NEON;
constexpr uint16_t COLOR_DIALOGUE_NEON   = COLOR_CYBER_BLUE_NEON;
constexpr uint16_t COLOR_ORACLE_TITLE    = COLOR_CYBER_BLUE_NEON;
constexpr uint16_t COLOR_FACE_CYAN       = 0x07FF;
constexpr uint16_t COLOR_FACE_FOCUS      = 0x367F;
constexpr uint16_t COLOR_FACE_HAPPY      = 0x07E8;
constexpr uint16_t COLOR_FACE_STARTLED   = 0xF800;
constexpr uint16_t COLOR_FACE_JUDGING    = 0xFD20;
constexpr uint16_t COLOR_FACE_GRUMPY     = 0xF986;
constexpr uint16_t COLOR_FACE_SLEEP      = 0x6B4D;
constexpr uint16_t COLOR_DIALOGUE_BORDER = 0x4A69;

// =============================================================================
// Banco Local de Respostas do "Oráculo Sarcástico" (Simulação da Camada 2)
// =============================================================================
static const OracleResponsePayload SARCASTIC_ORACLE_BANK[] = {
    {
        "Sistemas ópticos em HD online! Pronto para vigiar seus commits com bom humor.",
        EMOTION_HAPPY, +6, +4, 0
    },
    {
        "Você chama isso de código limpo ou é arte abstrata? Beba água.",
        EMOTION_JUDGING, +4, +5, 0
    },
    {
        "Mais uma xícara de café? Seu coração já está rodando em overclock de 240MHz.",
        EMOTION_HAPPY, +8, +8, -10
    },
    {
        "Olha só, um carinho! Quase esqueci que você me deixa vigiando sua mesa de graça.",
        EMOTION_HAPPY, +12, +4, 0
    },
    {
        "Cinco abas do StackOverflow abertas e nenhuma solução ainda? Interessante...",
        EMOTION_JUDGING, +3, +2, 0
    },
    {
        "Boa postura! Brincadeira, você parece um camarão curvado na frente do monitor.",
        EMOTION_JUDGING, +6, +2, 0
    }
};
constexpr uint8_t SARCASTIC_BANK_COUNT = sizeof(SARCASTIC_ORACLE_BANK) / sizeof(SARCASTIC_ORACLE_BANK[0]);

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
      rain_simulation_override(false),
      visor_raindrops(),
      saccade_offset_x(0.0f),
      saccade_offset_y(0.0f),
      next_saccade_ms(0)
{
    internal_stats = {20, 86, 88, 0, 0, 0};
    current_pose = {
        0.0f, 0.0f,
        0.0f, 0.0f,
        FACE_EYE_DEFAULT_R, FACE_PUPIL_DEFAULT_R,
        0.10f, 0.25f,
        -2.0f, -1.5f, 0.0f,
        MOUTH_CLOSED_HEIGHT, 26.0f, 0.55f,
        0.0f, 0.0f
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
    initVisorRainDrops();
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
    initOrGenerateSdAudioAssets();
    initOrGenerateSdGraphicsAssets();
    world_context_service.initWorldContext();
    buildStateJson(last_state_json, sizeof(last_state_json));
}

void PetController::restorePersistentState(HalSensors& hal_sensors) {
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

    // 0. Roteamento de Toque / Clique Direto na Tela (UI Clicável / Hotspots Táticos sem depender de teclado)
    bool touch_rising_edge = (gaze_data.touch_active && !prev_touch_active);
    prev_touch_active = gaze_data.touch_active;

    bool touch_poke_face    = false;
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

    if (touch_rising_edge) {
        int32_t tx = gaze_data.raw_screen_x;
        int32_t ty = gaze_data.raw_screen_y;
        bool pip_visible = pip_hd_zoom_active || debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms);
        int32_t pip_x0 = pip_hd_zoom_active ? 78 : 232;
        int32_t pip_y0 = pip_hd_zoom_active ? 30 : 34;
        int32_t pip_x1 = pip_x0 + (pip_hd_zoom_active ? 164 : 68);
        int32_t pip_y1 = pip_y0 + (pip_hd_zoom_active ? 132 : 60);

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
        } else {
            touch_poke_face = true;     // Clique no rosto central dispara carinho/interação com o Oráculo
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
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_pomodoro_start.wav").c_str());
            if (!oracle_brain.isWaitingAi()) {
                triggerSarcasticOracle(hal_sensors, "POMODORO_START");
            }
        } else {
            M5_LOGI("[POMODORO] Missao de Foco Tatico cancelada pelo usuario.");
            oracle_brain.recordHabitPomodoroResult(false);
            speakWithLipSync(hal_sensors, "Missão Pomodoro abortada! Fugindo do foco tão cedo?");
        }
    }

    if (pomodoro_active && now_ms >= pomodoro_end_ms) {
        pomodoro_active = false;
        M5_LOGI("[POMODORO] Missao de Foco Tatico de 25 min concluida!");
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
    if (gaze_data.face_detected && (gaze_data.is_slouching || gaze_data.is_too_close)) {
        if (bad_posture_since_ms == 0) {
            bad_posture_since_ms = now_ms;
        }
    } else {
        bad_posture_since_ms = 0;
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
    if (gaze_data.hand_wave_detected && (now_ms - last_hand_wave_ms > 12000U)) {
        last_hand_wave_ms = now_ms;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 5, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(5);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            speakWithLipSync(hal_sensors, "Opa! Vi seu aceno na câmera! Sensores ópticos retribuindo o cumprimento!");
        }
    }

    // 2.6. Visão 2.0: Reação Imediata (< 50ms, custo zero de API) a Gestos de Cabeça "SIM" (Nod) e "NÃO" (Shake)!
    if ((gaze_data.head_nod_yes || gaze_data.head_shake_no) && (now_ms - last_head_gesture_ms > 10000U)) {
        last_head_gesture_ms = now_ms;
        if (gaze_data.head_nod_yes) {
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 4000U;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 4, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(4);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi() && !is_muted_indefinitely) {
                speakWithLipSync(hal_sensors, "Afirmativo! Detectei seu aceno de cabeça dizendo SIM na câmera!");
            }
        } else if (gaze_data.head_shake_no) {
            current_emotion = EMOTION_JUDGING;
            emotion_lock_until_ms = now_ms + 4000U;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 2, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(3);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi() && !is_muted_indefinitely) {
                speakWithLipSync(hal_sensors, "Entendido! Vi você balançando a cabeça dizendo NÃO para o sensor óptico.");
            }
        }
    }

    // 2.65. Tecla [Q]: Alterna (Toggle) o Modo Silêncio Indefinido manualmente
    if (audio_data.silence_toggle_kbd_triggered) {
        is_muted_indefinitely = !is_muted_indefinitely;
        if (is_muted_indefinitely) {
            current_emotion = EMOTION_LISTENING;
            emotion_lock_until_ms = now_ms + 4500U;
            stopSpeakingImmediately(hal_sensors, "[SILÊNCIO INDEFINIDO ATIVO — FAÇA JOINHA, 2 PALMAS OU [Q] P/ SAIR]");
            M5_LOGI("[SILENCIO] Modo Silencio Indefinido ATIVADO via tecla [Q]!");
        } else {
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 3500U;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            speakWithLipSync(hal_sensors, "Modo silêncio encerrado! Voz tática do Bicho reativada na bancada.");
            M5_LOGI("[SILENCIO] Modo Silencio Indefinido ENCERRADO via tecla [Q]!");
        }
    }

    // 2.7. Visão 2.0: Reação Imediata (< 50ms, 100% local) aos 4 Gestos Táticos de Mão e Dedos!
    // (1) 👍 Joinha ("Thumbs Up" / [J]): Elogio visual instantâneo (+8% Humor, +10 XP, EMOTION_HAPPY) e encerra Modo Silêncio!
    if (gaze_data.gesture_thumbs_up && (now_ms - last_hand_gesture_react_ms > 4500U)) {
        last_hand_gesture_react_ms = now_ms;
        bool was_muted = is_muted_indefinitely;
        is_muted_indefinitely = false; // Joinha encerra o modo silêncio indefinido!
        pip_display_until_ms = now_ms + 5500U;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 5000U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 8, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(10);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            speakWithLipSync(hal_sensors, was_muted
                ? "Joinha confirmado! Modo silêncio encerrado e moral da bancada subiu mais 10 XP!"
                : "Joinha confirmado na lente! Valeu parceiro, moral da bancada subiu mais 10 XP!");
        }
        M5_LOGI("[GESTO-OPTICO] Joinha (THUMBS UP) detectado! +8 Humor, +10 XP e voz ativa.");
    }
    // (2) ✌️ Paz & Amor / Vitória ("V-Sign" / [K]): Dispara Selfie Tática Instantânea com Flash + Cor Real 16-bit no SD!
    else if (gaze_data.gesture_victory && (now_ms - last_hand_gesture_react_ms > 5000U)) {
        last_hand_gesture_react_ms = now_ms;
        is_muted_indefinitely = false; // Pose de vitória também encerra o modo silêncio
        pip_optic_mode = 1U; // Ativa Cor Real 16-bit RGB565 no visor para ver a foto
        pip_display_until_ms = now_ms + 8000U;
        selfie_flash_until_ms = now_ms + 350U;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 6, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(8);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
        oracle_brain.saveEventSnapshotToSd("GESTURE_VICTORY_SELFIE");
        if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            speakWithLipSync(hal_sensors, "Pose de vitória detectada! Selfie tática capturada e salva no álbum do Cartão SD!");
        }
        M5_LOGI("[GESTO-OPTICO] Paz e Amor (VICTORY V-SIGN) detectado! Selfie salva no Cartao SD.");
    }
    // (3) ✋ Palma Aberta ("Stop / Pause" / [H]): Corta a fala se estiver falando ou alterna/pausa o Pomodoro/Som
    else if (gaze_data.gesture_open_palm && (lip_sync_state.is_speaking || (now_ms - last_hand_gesture_react_ms > 5000U))) {
        last_hand_gesture_react_ms = now_ms;
        pip_display_until_ms = now_ms + 5000U;
        if (lip_sync_state.is_speaking) {
            current_emotion = EMOTION_NEUTRAL;
            emotion_lock_until_ms = now_ms + 3000U;
            stopSpeakingImmediately(hal_sensors, "[GESTO: PALMA ABERTA (STOP) — TRANSMISSÃO INTERROMPIDA]");
            M5_LOGI("[GESTO-OPTICO] Palma aberta (STOP) detectada durante fala! Transmissao de voz interrompida.");
        } else {
            is_muted_indefinitely = false;
            pomodoro_active = !pomodoro_active;
            if (pomodoro_active) {
                pomodoro_end_ms = now_ms + (25U * 60U * 1000U);
                current_emotion = EMOTION_FOCUSED;
                emotion_lock_until_ms = now_ms + 3500U;
                playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
                speakWithLipSync(hal_sensors, "Gesto de Palma detectado! Missão Pomodoro de 25 minutos iniciada sem tocar no teclado!");
            } else {
                ambient_focus_enabled = false;
                current_emotion = EMOTION_IDLE;
                emotion_lock_until_ms = now_ms + 3000U;
                stopSpeakingImmediately(hal_sensors, "[GESTO: PALMA ABERTA — MISSÃO POMODORO / SOM PAUSADOS]");
            }
            M5_LOGI("[GESTO-OPTICO] Palma aberta detectada! Pomodoro alternado para: %s", pomodoro_active ? "ATIVO" : "PAUSADO");
        }
    }
    // (4) 🤫 Dedo na Boca ("Shhh!"): Corta o áudio IMEDIATAMENTE e mantém Modo Silêncio Indefinido até o usuário encerrar!
    else if (!audio_data.silence_toggle_kbd_triggered &&
             gaze_data.gesture_shhh && !gaze_data.gesture_thumbs_up && !gaze_data.gesture_victory && !gaze_data.gesture_open_palm &&
             (lip_sync_state.is_speaking || !is_muted_indefinitely || (now_ms - last_hand_gesture_react_ms > 6000U))) {
        last_hand_gesture_react_ms = now_ms;
        is_muted_indefinitely = true; // Permanece em silêncio indefinidamente até ordem explícita do usuário!
        pip_display_until_ms = now_ms + 5000U;
        current_emotion = EMOTION_LISTENING;
        emotion_lock_until_ms = now_ms + 4500U;
        stopSpeakingImmediately(hal_sensors, "[GESTO: SHHH! SILÊNCIO INDEFINIDO — FAÇA JOINHA, 2 PALMAS OU [Q] P/ SAIR]");
        M5_LOGI("[GESTO-OPTICO] Dedo na boca (SHHH) detectado! Modo Silencio Indefinido ativado.");
    }

    // 2.8. Ouvido 2.0: Gestos Acústicos em Tempo Real (Duas Palmas 👏👏 e Assobio 🎵) — PC e CoreS3!
    if (audio_data.double_clap_detected && (now_ms - last_acoustic_gesture_ms > 3500U)) {
        last_acoustic_gesture_ms = now_ms;
        if (is_muted_indefinitely) {
            is_muted_indefinitely = false;
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 4000U;
            oracle_brain.addXpPoints(5);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            speakWithLipSync(hal_sensors, "Duas palmas detectadas no microfone! Modo silêncio encerrado, estou na escuta!");
        } else {
            pip_display_until_ms = (now_ms < pip_display_until_ms) ? 0U : (now_ms + 8000U);
            current_emotion = EMOTION_HAPPY;
            emotion_lock_until_ms = now_ms + 3500U;
            internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 4, 0, MAX_STAT_VALUE);
            oracle_brain.addXpPoints(5);
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            if (!lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
                speakWithLipSync(hal_sensors, "Duas palmas confirmadas no sensor acústico! Visor óptico comutado na tela!");
            }
        }
    } else if (audio_data.whistle_detected && (now_ms - last_acoustic_gesture_ms > 6000U)) {
        last_acoustic_gesture_ms = now_ms;
        current_emotion = EMOTION_HAPPY;
        emotion_lock_until_ms = now_ms + 4500U;
        internal_stats.mood_score = clampStatValue(internal_stats.mood_score + 5, 0, MAX_STAT_VALUE);
        oracle_brain.addXpPoints(6);
        playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
        if (!is_muted_indefinitely && !lip_sync_state.is_speaking && !oracle_brain.isWaitingAi()) {
            speakWithLipSync(hal_sensors, "Ouvi seu assobio sintonizado no microfone! Chamou o copiloto da bancada?");
        }
    }

    // Persiste automaticamente se o minuto de foco ou o humor mudou (ex.: susto ou joinha)
    if (internal_stats.focus_minutes != prev_focus || internal_stats.mood_score != prev_mood) {
        syncPersistentState(hal_sensors);
    }

    // 3. Camada 2: Gatilho do Cérebro Multimodal (Toque, Voz, Objeto na Lente, Intruso, Bocejo, Postura, Celular, Briefing)
    if (!oracle_brain.isWaitingAi() && (now_ms - last_oracle_trigger_ms > ORACLE_COOLDOWN_MS)) {
        std::string world_trigger;
        const ExternalWorldContext& world_ctx = world_context_service.getWorldContext();
        bool is_muted_now = is_muted_indefinitely;

        if (touch_poke_face || audio_data.interaction_triggered) {
            is_muted_indefinitely = false; // Toque manual explícito encerra o modo silêncio
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            oracle_brain.saveEventSnapshotToSd("TOUCH_POKE");
            triggerSarcasticOracle(hal_sensors, "TOUCH_POKE");
        } else if (audio_data.object_scan_triggered ||
                   (!is_muted_now &&
                    object_close_since_ms > 0 &&
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
        } else if (!is_muted_now && audio_data.vad_phrase_completed) {
            oracle_brain.recordHabitVoiceInteraction();
            oracle_brain.saveEventSnapshotToSd("VOICE_HANDS_FREE");
            triggerSarcasticOracle(hal_sensors, "VOICE_HANDS_FREE");
        } else if (!is_muted_now && audio_data.is_loud_peak && audio_data.noise_level_db > 85.0f) {
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_glitch_shock.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "HIGH_NOISE");
        } else if (!is_muted_now &&
                   intruder_since_ms > 0 &&
                   (now_ms - intruder_since_ms >= 1800U) &&
                   (now_ms - last_intruder_alert_ms >= 90000U)) {
            last_intruder_alert_ms = now_ms;
            intruder_since_ms = 0;
            pip_display_until_ms = now_ms + 9000U;
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav").c_str());
            oracle_brain.saveEventSnapshotToSd("INTRUDER_DETECTED");
            triggerSarcasticOracle(hal_sensors, "INTRUDER_DETECTED");
        } else if (!is_muted_now &&
                   yawn_since_ms > 0 &&
                   (now_ms - yawn_since_ms >= 1200U) &&
                   (now_ms - last_yawn_alert_ms >= 90000U)) {
            last_yawn_alert_ms = now_ms;
            yawn_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("YAWN_FATIGUE");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "YAWN_FATIGUE");
        } else if (!is_muted_now &&
                   !daily_briefing_done &&
                   hal_sensors.getSdConfig().morning_briefing_enabled &&
                   world_ctx.has_valid_sync &&
                   now_ms > 9000U &&
                   !lip_sync_state.is_speaking) {
            daily_briefing_done = true;
            M5_LOGI("[BRIEFING] Disparando Briefing Tatico da Sessao com clima, dolar e status de repositorio!");
            triggerSarcasticOracle(hal_sensors, "DAILY_BRIEFING");
        } else if (!is_muted_now &&
                   bad_posture_since_ms > 0 &&
                   (now_ms - bad_posture_since_ms >= 5000U) &&
                   (now_ms - last_posture_alert_ms >= 90000U)) {
            last_posture_alert_ms = now_ms;
            bad_posture_since_ms = 0;
            oracle_brain.recordHabitSlouchAlert();
            oracle_brain.saveEventSnapshotToSd("BAD_POSTURE");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "BAD_POSTURE");
        } else if (!is_muted_now &&
                   phone_look_down_since_ms > 0 &&
                   (now_ms - phone_look_down_since_ms >= 4500U) &&
                   (now_ms - last_phone_alert_ms >= 90000U)) {
            last_phone_alert_ms = now_ms;
            phone_look_down_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("PHONE_DOOMSCROLL");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "PHONE_DOOMSCROLL");
        } else if (!is_muted_now &&
                   procrastination_since_ms > 0 &&
                   (now_ms - procrastination_since_ms >= 8000U) &&
                   (now_ms - last_procrastination_alert_ms >= 120000U)) {
            last_procrastination_alert_ms = now_ms;
            procrastination_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("PROCRASTINATION");
            playSdSoundEffect(hal_sensors, (std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav").c_str());
            triggerSarcasticOracle(hal_sensors, "PROCRASTINATION_DETECTED");
        } else if (!is_muted_now &&
                   dark_room_since_ms > 0 &&
                   (now_ms - dark_room_since_ms >= 15000U) &&
                   (now_ms - last_night_owl_alert_ms >= 240000U)) {
            last_night_owl_alert_ms = now_ms;
            dark_room_since_ms = 0;
            oracle_brain.saveEventSnapshotToSd("NIGHT_OWL_DARK");
            triggerSarcasticOracle(hal_sensors, "NIGHT_OWL_DARK");
        } else if (!is_muted_now &&
                   internal_stats.focus_minutes > 0 &&
                   (internal_stats.focus_minutes % FOCUS_LIMIT_MINUTES == 0) &&
                   prev_focus != internal_stats.focus_minutes) {
            triggerSarcasticOracle(hal_sensors, "OVERWORK");
        } else if (!is_muted_now &&
                   internal_stats.meeting_minutes >= MEETING_FATIGUE_MINUTES &&
                   audio_data.is_continuous_speech &&
                   (now_ms - last_oracle_trigger_ms > 25000U)) {
            triggerSarcasticOracle(hal_sensors, "LONG_MEETING");
        } else if (!is_muted_now && world_context_service.checkWorldTriggers(now_ms, world_trigger)) {
            triggerSarcasticOracle(hal_sensors, world_trigger.c_str());
        }
    }

    // 3.1. Coleta resposta assíncrona do Cérebro (Gemini Flash-Lite ou Fallback Offline) quando pronta
    OracleBrainResponse brain_result;
    if (oracle_brain.pollCompletedResponse(brain_result)) {
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

#if defined(ARDUINO)
    // No hardware real M5Stack CoreS3, captura o frame JPEG 320x240 diretamente da câmera GC0308 ativa no HalSensors
    context_packet.jpeg_base64 = hal_sensors.captureCameraJpegBase64();
#endif

    // Captura áudio WAV do microfone APENAS em gatilhos de voz/reunião para evitar que áudio antigo ou ruído sequestre gatilhos visuais
    bool is_voice_trigger = (context_packet.trigger_reason == "VOICE_HANDS_FREE" ||
                             context_packet.trigger_reason == "VOICE_COMMAND" ||
                             context_packet.trigger_reason == "LONG_MEETING");
    context_packet.wav_base64 = is_voice_trigger ? hal_sensors.recordUserAudioWav() : "";

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

    // Salva evolução e humor do bicho após cada interação com o Oráculo
    syncPersistentState(hal_sensors);

    if (is_muted_indefinitely) {
        stopSpeakingImmediately(hal_sensors, response_payload.reply_text);
    } else {
        speakWithLipSync(hal_sensors, response_payload.reply_text);
    }
}

static void writeWavFileToSdHelper(const char* file_path, const std::vector<int16_t>& samples, uint32_t sample_rate_hz) {
    uint32_t pcm_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t riff_size = 36U + pcm_bytes;
    uint32_t byte_rate = sample_rate_hz * 2U;
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

#if defined(ARDUINO)
    File f = SD.open(file_path, FILE_WRITE);
    if (f) {
        f.write(header, 44);
        f.write(reinterpret_cast<const uint8_t*>(samples.data()), pcm_bytes);
        f.close();
    }
#else
    std::ofstream f(file_path, std::ios::binary | std::ios::trunc);
    if (f.is_open()) {
        f.write(reinterpret_cast<const char*>(header), 44);
        f.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(pcm_bytes));
    }
#endif
}

static bool sdFileExistsHelper(const char* file_path) {
#if defined(ARDUINO)
    return SD.exists(file_path);
#else
    std::ifstream f(file_path, std::ios::binary);
    return f.good();
#endif
}

void PetController::initOrGenerateSdAudioAssets() {
    if (!oracle_brain.isSdStorageReady()) {
        return;
    }

    constexpr uint32_t sfx_rate = VOICE_WAV_SAMPLE_RATE_HZ; // 24000 Hz HD
    constexpr float two_pi = 6.2831853f;

    // 1. sfx_purr_servo.wav: Chirp de servo óptico + ronronar harmônico (320 ms)
    std::string path_purr = std::string(SD_SFX_DIR) + "/sfx_purr_servo.wav";
    if (!sdFileExistsHelper(path_purr.c_str())) {
        size_t n = (sfx_rate * 320U) / 1000U;
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            float env = std::sin((static_cast<float>(i) / static_cast<float>(n)) * 3.1415926f);
            float servo_freq = 520.0f + 680.0f * (static_cast<float>(i) / static_cast<float>(n));
            float purr_mod = 0.65f + 0.35f * std::sin(two_pi * 26.0f * t);
            float sig = 0.55f * std::sin(two_pi * servo_freq * t) +
                        0.45f * std::sin(two_pi * 195.0f * t) * purr_mod;
            pcm[i] = static_cast<int16_t>(sig * env * 9500.0f);
        }
        writeWavFileToSdHelper(path_purr.c_str(), pcm, sfx_rate);
    }

    // 2. sfx_camera_scan.wav: Obturador cibernético + varredura sonar (280 ms)
    std::string path_cam = std::string(SD_SFX_DIR) + "/sfx_camera_scan.wav";
    if (!sdFileExistsHelper(path_cam.c_str())) {
        size_t n = (sfx_rate * 280U) / 1000U;
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            float f = (i < n / 3) ? 1480.0f : (920.0f + 1100.0f * (static_cast<float>(i) / static_cast<float>(n)));
            float env = 1.0f - (static_cast<float>(i) / static_cast<float>(n)) * 0.6f;
            float sig = 0.7f * std::sin(two_pi * f * t) + 0.3f * std::sin(two_pi * (f * 1.5f) * t);
            pcm[i] = static_cast<int16_t>(sig * env * 9200.0f);
        }
        writeWavFileToSdHelper(path_cam.c_str(), pcm, sfx_rate);
    }

    // 3. sfx_posture_alert.wav: Alerta tático duplo de postura (420 ms)
    std::string path_posture = std::string(SD_SFX_DIR) + "/sfx_posture_alert.wav";
    if (!sdFileExistsHelper(path_posture.c_str())) {
        size_t n = (sfx_rate * 420U) / 1000U;
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            bool second_beep = (i > n / 2);
            float f = second_beep ? 540.0f : 720.0f;
            float gate = ((i > (n * 42) / 100) && (i < (n * 54) / 100)) ? 0.0f : 1.0f;
            float sig = 0.65f * std::sin(two_pi * f * t) + 0.35f * std::sin(two_pi * (f * 2.0f) * t);
            pcm[i] = static_cast<int16_t>(sig * gate * 10500.0f);
        }
        writeWavFileToSdHelper(path_posture.c_str(), pcm, sfx_rate);
    }

    // 4. sfx_pomodoro_start.wav: Arpejo ascendente de início de missão (480 ms)
    std::string path_pomo_start = std::string(SD_SFX_DIR) + "/sfx_pomodoro_start.wav";
    if (!sdFileExistsHelper(path_pomo_start.c_str())) {
        size_t n = (sfx_rate * 480U) / 1000U;
        std::vector<int16_t> pcm(n);
        const float notes[4] = {440.0f, 554.37f, 659.25f, 880.0f};
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            size_t step = (i * 4U) / n;
            if (step > 3U) step = 3U;
            float f = notes[step];
            float sig = 0.70f * std::sin(two_pi * f * t) + 0.30f * std::sin(two_pi * (f * 1.5f) * t);
            pcm[i] = static_cast<int16_t>(sig * 9800.0f);
        }
        writeWavFileToSdHelper(path_pomo_start.c_str(), pcm, sfx_rate);
    }

    // 5. sfx_pomodoro_fanfare.wav: Fanfarra harmônica de conclusão Pomodoro (640 ms)
    std::string path_pomo_done = std::string(SD_SFX_DIR) + "/sfx_pomodoro_fanfare.wav";
    if (!sdFileExistsHelper(path_pomo_done.c_str())) {
        size_t n = (sfx_rate * 640U) / 1000U;
        std::vector<int16_t> pcm(n);
        const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.50f};
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            size_t step = (i * 4U) / n;
            if (step > 3U) step = 3U;
            float f = notes[step];
            float env = 1.0f - 0.35f * (static_cast<float>(i) / static_cast<float>(n));
            float sig = 0.60f * std::sin(two_pi * f * t) +
                        0.25f * std::sin(two_pi * (f * 1.25f) * t) +
                        0.15f * std::sin(two_pi * (f * 2.0f) * t);
            pcm[i] = static_cast<int16_t>(sig * env * 10500.0f);
        }
        writeWavFileToSdHelper(path_pomo_done.c_str(), pcm, sfx_rate);
    }

    // 6. sfx_glitch_shock.wav: Descarga eletromagnética de susto/impacto (300 ms)
    std::string path_shock = std::string(SD_SFX_DIR) + "/sfx_glitch_shock.wav";
    if (!sdFileExistsHelper(path_shock.c_str())) {
        size_t n = (sfx_rate * 300U) / 1000U;
        std::vector<int16_t> pcm(n);
        uint32_t lfsr = 0xACE1U;
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            lfsr ^= lfsr << 13;
            lfsr ^= lfsr >> 17;
            lfsr ^= lfsr << 5;
            float noise = (static_cast<float>(lfsr & 0xFFFFU) / 32768.0f) - 1.0f;
            float env = 1.0f - (static_cast<float>(i) / static_cast<float>(n));
            float fm = std::sin(two_pi * (880.0f - 520.0f * (1.0f - env)) * t);
            pcm[i] = static_cast<int16_t>((0.65f * fm + 0.35f * noise) * env * 11000.0f);
        }
        writeWavFileToSdHelper(path_shock.c_str(), pcm, sfx_rate);
    }

    // 7. focus_space_hum.wav: Gerador Acústico Espacial / Binaural de Foco (3.6 s em 24 kHz)
    std::string path_ambient = std::string(SD_AMBIENT_DIR) + "/focus_space_hum.wav";
    if (!sdFileExistsHelper(path_ambient.c_str())) {
        size_t n = (sfx_rate * 3600U) / 1000U;
        std::vector<int16_t> pcm(n);
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sfx_rate);
            float fade = 1.0f;
            size_t fade_samples = sfx_rate / 4U;
            if (i < fade_samples) {
                fade = static_cast<float>(i) / static_cast<float>(fade_samples);
            } else if (i + fade_samples > n) {
                fade = static_cast<float>(n - i) / static_cast<float>(fade_samples);
            }
            // Drone harmônico suave (216 Hz + 228 Hz batimento binaural de 12 Hz Alpha/Foco + harmônico 324 Hz)
            float drone = 0.48f * std::sin(two_pi * 216.0f * t) +
                          0.34f * std::sin(two_pi * 228.0f * t) +
                          0.18f * std::sin(two_pi * 324.0f * t);
            pcm[i] = static_cast<int16_t>(drone * fade * 3400.0f);
        }
        writeWavFileToSdHelper(path_ambient.c_str(), pcm, sfx_rate);
    }
}

static uint16_t packRgb565Helper(int32_t r8, int32_t g8, int32_t b8) {
    if (r8 < 0) r8 = 0;
    if (r8 > 255) r8 = 255;
    if (g8 < 0) g8 = 0;
    if (g8 > 255) g8 = 255;
    if (b8 < 0) b8 = 0;
    if (b8 > 255) b8 = 255;
    uint16_t c = static_cast<uint16_t>(
        ((static_cast<uint16_t>(r8 >> 3) & 0x1FU) << 11) |
        ((static_cast<uint16_t>(g8 >> 2) & 0x3FU) << 5) |
        (static_cast<uint16_t>(b8 >> 3) & 0x1FU)
    );
    if (c == COLOR_KEY_TRANSPARENT) {
        c ^= 0x0020U; // Evita colisão acidental com o color-key magenta (0xF81F)
    }
    return c;
}

static uint16_t packRgb565DitheredHelper(int32_t x, int32_t y, float r_f, float g_f, float b_f) {
    // Matriz de Bayer 2x2 para eliminar faixas de quantização (Mach banding) em gradientes escuros de 16-bit
    static const float bayer_2x2[2][2] = {
        { -1.5f,  0.5f },
        {  1.5f, -0.5f }
    };
    float d = bayer_2x2[y & 1][x & 1];
    int32_t r8 = static_cast<int32_t>(std::round(r_f + d * 1.8f));
    int32_t g8 = static_cast<int32_t>(std::round(g_f + d * 1.1f));
    int32_t b8 = static_cast<int32_t>(std::round(b_f + d * 1.8f));
    return packRgb565Helper(r8, g8, b8);
}

static bool writeRgb565AssetToSdHelper(const char* file_path, const uint16_t* pixels, size_t pixel_count) {
    size_t byte_len = pixel_count * sizeof(uint16_t);
#if defined(ARDUINO)
    File f = SD.open(file_path, FILE_WRITE);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const uint8_t*>(pixels), byte_len);
    f.close();
    return true;
#else
    std::ofstream f(file_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(byte_len));
    return f.good();
#endif
}

static bool readRgb565AssetFromSdHelper(const char* file_path, uint16_t* out_pixels, size_t pixel_count) {
    size_t byte_len = pixel_count * sizeof(uint16_t);
#if defined(ARDUINO)
    if (!SD.exists(file_path)) {
        return false;
    }
    File f = SD.open(file_path, FILE_READ);
    if (!f || f.size() < byte_len) {
        if (f) f.close();
        return false;
    }
    size_t read_bytes = f.read(reinterpret_cast<uint8_t*>(out_pixels), byte_len);
    f.close();
    return (read_bytes == byte_len);
#else
    std::ifstream f(file_path, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    f.read(reinterpret_cast<char*>(out_pixels), static_cast<std::streamsize>(byte_len));
    return f.good();
#endif
}

static const char* const SD_THEME_FILENAMES[SD_THEME_COUNT] = {
    "sd_card/gfx/themes/theme_0_cyber_cyan.rgb565",
    "sd_card/gfx/themes/theme_1_emerald_bio.rgb565",
    "sd_card/gfx/themes/theme_2_solar_amber.rgb565",
    "sd_card/gfx/themes/theme_3_tactical_red.rgb565"
};

static const char* const SD_EYE_LENS_FILENAMES[EYE_LENS_COLOR_COUNT] = {
    "sd_card/gfx/eyes/eye_lens_0_cyan.rgb565",
    "sd_card/gfx/eyes/eye_lens_1_emerald.rgb565",
    "sd_card/gfx/eyes/eye_lens_2_amber.rgb565",
    "sd_card/gfx/eyes/eye_lens_3_red.rgb565"
};

static const char* const SD_HUD_ICON_FILENAMES[HUD_ICON_COUNT] = {
    "sd_card/gfx/icons/icon_0_core.rgb565",
    "sd_card/gfx/icons/icon_1_focus.rgb565",
    "sd_card/gfx/icons/icon_2_sd.rgb565",
    "sd_card/gfx/icons/icon_3_audio.rgb565"
};

static constexpr const char* SD_GFX_PALETTE_VERSION_FILE = "sd_card/gfx/palette_v3.ver";

void PetController::initOrGenerateSdGraphicsAssets() {
    active_theme_bg_rgb565.assign(SCREEN_WIDTH * SCREEN_HEIGHT, COLOR_CYBER_BG_NAVY);
    for (uint8_t c = 0; c < EYE_LENS_COLOR_COUNT; ++c) {
        for (size_t p = 0; p < EYE_LENS_TEX_PIXELS; ++p) {
            eye_lens_textures_rgb565[c][p] = COLOR_KEY_TRANSPARENT;
        }
    }
    for (uint8_t ic = 0; ic < HUD_ICON_COUNT; ++ic) {
        for (size_t p = 0; p < HUD_ICON_PIXELS; ++p) {
            hud_icons_rgb565[ic][p] = COLOR_KEY_TRANSPARENT;
        }
    }

    // Verifica se o pacote de texturas no SD já está na revisão v3 (paleta corrigida e sem bandas)
    bool force_regenerate = !sdFileExistsHelper(SD_GFX_PALETTE_VERSION_FILE);

    // =========================================================================
    // 1. GERA E CARREGA AS 4 LENTES ÓPTICAS 64x64 HD (FAVO DE MEL + REFLEXO 3D)
    // =========================================================================
    struct OpticPaletteRgb {
        int32_t r_base, g_base, b_base;
        int32_t r_glow, g_glow, b_glow;
    };
    const OpticPaletteRgb optic_palettes[EYE_LENS_COLOR_COUNT] = {
        {   0, 185, 255,  110, 245, 255 }, // 0: Cyber Cyan (Padrão Coeso / Vigilância / Foco)
        {   0, 215, 115,  115, 255, 185 }, // 1: Bio Emerald (Tema Bio / Operacional)
        { 255, 150,  15,  255, 225, 100 }, // 2: Solar Amber (Sarcástico / Julgando com sobrancelha arqueada)
        { 250,  40,  58,  255, 135, 115 }  // 3: Tactical Red (Apenas quando Puto / Assustado / Tema Rubi)
    };

    // Geometria alinhada exatamente com FACE_EYE_RX_DEFAULT (26) e FACE_EYE_RY_DEFAULT (23)
    constexpr int32_t half_s = EYE_LENS_TEX_SIZE / 2; // 32
    constexpr int32_t outer_rx = 26;
    constexpr int32_t outer_ry = 23;
    constexpr int32_t outer_ch = 9;
    constexpr int32_t inner_rx = 23;
    constexpr int32_t inner_ry = 20;
    constexpr int32_t inner_ch = 8;

    for (uint8_t c = 0; c < EYE_LENS_COLOR_COUNT; ++c) {
        const char* path = SD_EYE_LENS_FILENAMES[c];
        if (force_regenerate || !oracle_brain.isSdStorageReady() || !readRgb565AssetFromSdHelper(path, eye_lens_textures_rgb565[c], EYE_LENS_TEX_PIXELS)) {
            const OpticPaletteRgb& pal = optic_palettes[c];
            for (int32_t py = 0; py < EYE_LENS_TEX_SIZE; ++py) {
                for (int32_t px = 0; px < EYE_LENS_TEX_SIZE; ++px) {
                    int32_t dx = px - half_s;
                    int32_t dy = py - half_s;
                    int32_t abs_dx = std::abs(dx);
                    int32_t abs_dy = std::abs(dy);

                    int32_t max_outer_x = outer_rx - ((abs_dy > (outer_ry - outer_ch)) ? (abs_dy - (outer_ry - outer_ch)) : 0);
                    if (abs_dy > outer_ry || abs_dx > max_outer_x) {
                        eye_lens_textures_rgb565[c][py * EYE_LENS_TEX_SIZE + px] = COLOR_KEY_TRANSPARENT;
                        continue;
                    }

                    int32_t max_inner_x = inner_rx - ((abs_dy > (inner_ry - inner_ch)) ? (abs_dy - (inner_ry - inner_ch)) : 0);
                    if (abs_dy > inner_ry || abs_dx > max_inner_x) {
                        // Moldura metálica 3D chanfrada com iluminação direcional superior-esquerda
                        float bevel_light = (static_cast<float>(-dx - dy) / 46.0f);
                        int32_t mr = static_cast<int32_t>(30.0f + bevel_light * 22.0f + pal.r_base * 0.14f);
                        int32_t mg = static_cast<int32_t>(42.0f + bevel_light * 28.0f + pal.g_base * 0.14f);
                        int32_t mb = static_cast<int32_t>(68.0f + bevel_light * 34.0f + pal.b_base * 0.16f);
                        eye_lens_textures_rgb565[c][py * EYE_LENS_TEX_SIZE + px] = packRgb565Helper(mr, mg, mb);
                        continue;
                    }

                    // Interior da Lente Óptica 3D: gradiente esférico + malha hexagonal (favo de mel) + reflexo especular de vidro
                    float norm_dist = std::sqrt(static_cast<float>(dx * dx) / static_cast<float>(inner_rx * inner_rx) +
                                                static_cast<float>(dy * dy) / static_cast<float>(inner_ry * inner_ry));
                    if (norm_dist > 1.0f) norm_dist = 1.0f;

                    // Curvatura esférica suave (centro luminoso, borda interna com sombreamento de profundidade)
                    float sphere_shade = 1.06f - 0.44f * (norm_dist * norm_dist);

                    // Padrão de malha hexagonal (Honeycomb Cyber-Mesh) sutil
                    int32_t hx = (px + ((py / 3) % 2) * 3) % 6;
                    int32_t hy = py % 6;
                    bool is_hex_grid = (hx == 0 || hy == 0);
                    float hex_factor = is_hex_grid ? 0.84f : 1.0f;

                    // Scanline óptica sutil
                    float scan_factor = ((py & 1) == 0) ? 1.03f : 0.94f;

                    float r_f = (pal.r_base * sphere_shade + pal.r_glow * (1.0f - norm_dist) * 0.25f) * hex_factor * scan_factor;
                    float g_f = (pal.g_base * sphere_shade + pal.g_glow * (1.0f - norm_dist) * 0.25f) * hex_factor * scan_factor;
                    float b_f = (pal.b_base * sphere_shade + pal.b_glow * (1.0f - norm_dist) * 0.25f) * hex_factor * scan_factor;

                    // Reflexo especular de vidro curvo no quadrante superior esquerdo (Lens Flare)
                    float spec_dx = static_cast<float>(dx + 9);
                    float spec_dy = static_cast<float>(dy + 8);
                    float spec_d2 = (spec_dx * spec_dx) / 36.0f + (spec_dy * spec_dy) / 20.0f;
                    if (spec_d2 < 1.0f) {
                        float spec_intensity = (1.0f - spec_d2) * (1.0f - spec_d2) * 0.70f;
                        r_f += (255.0f - r_f) * spec_intensity;
                        g_f += (255.0f - g_f) * spec_intensity;
                        b_f += (255.0f - b_f) * spec_intensity;
                    }

                    eye_lens_textures_rgb565[c][py * EYE_LENS_TEX_SIZE + px] =
                        packRgb565DitheredHelper(px, py, r_f, g_f, b_f);
                }
            }
            if (oracle_brain.isSdStorageReady()) {
                writeRgb565AssetToSdHelper(path, eye_lens_textures_rgb565[c], EYE_LENS_TEX_PIXELS);
            }
        }
    }

    // =========================================================================
    // 2. GERA E CARREGA OS 4 ÍCONES TÁTICOS 12x12 DO HUD NO CARTÃO SD
    // =========================================================================
    for (uint8_t ic = 0; ic < HUD_ICON_COUNT; ++ic) {
        const char* path = SD_HUD_ICON_FILENAMES[ic];
        if (force_regenerate || !oracle_brain.isSdStorageReady() || !readRgb565AssetFromSdHelper(path, hud_icons_rgb565[ic], HUD_ICON_PIXELS)) {
            for (int32_t y = 0; y < HUD_ICON_SIZE; ++y) {
                for (int32_t x = 0; x < HUD_ICON_SIZE; ++x) {
                    uint16_t col = COLOR_KEY_TRANSPARENT;
                    float cx = static_cast<float>(x) - 5.5f;
                    float cy = static_cast<float>(y) - 5.5f;
                    float r = std::sqrt(cx * cx + cy * cy);

                    if (ic == 0) {
                        // Ícone 0: Reator de Energia / Raio Cyber (Ciano + Branco Gelo)
                        if (r <= 5.2f && r >= 3.8f) {
                            col = packRgb565Helper(0, 195, 255);
                        }
                        if ((y >= 2 && y <= 6 && x == 6 - (y - 2) / 2) ||
                            (y == 6 && x >= 4 && x <= 7) ||
                            (y >= 6 && y <= 9 && x == 5 + (9 - y) / 2)) {
                            col = packRgb565Helper(180, 250, 255);
                        }
                    } else if (ic == 1) {
                        // Ícone 1: Retículo de Mira / Pomodoro HD (Ciano + Núcleo Branco)
                        if (r <= 5.2f && r >= 3.7f) {
                            col = packRgb565Helper(0, 200, 255);
                        } else if ((x == 5 || x == 6) && (y <= 2 || y >= 9)) {
                            col = packRgb565Helper(160, 245, 255);
                        } else if ((y == 5 || y == 6) && (x <= 2 || x >= 9)) {
                            col = packRgb565Helper(160, 245, 255);
                        } else if (r <= 1.5f) {
                            col = packRgb565Helper(255, 255, 255);
                        }
                    } else if (ic == 2) {
                        // Ícone 2: Chip MicroSD 64GB (Moldura Ciano + Pinos Ciano Claro)
                        if (x >= 2 && x <= 9 && y >= 1 && y <= 10) {
                            if (x == 2 && y <= 3 && (x + y < 5)) {
                                col = COLOR_KEY_TRANSPARENT;
                            } else if (x == 2 || x == 9 || y == 1 || y == 10) {
                                col = packRgb565Helper(0, 215, 255);
                            } else if (y >= 3 && y <= 5 && (x == 4 || x == 6 || x == 8)) {
                                col = packRgb565Helper(175, 245, 255);
                            } else {
                                col = packRgb565Helper(12, 28, 56);
                            }
                        }
                    } else {
                        // Ícone 3: Ondas Acústicas HD (Ciano Brilhante)
                        if (x >= 2 && x <= 4 && y >= 4 && y <= 7) {
                            col = packRgb565Helper(160, 250, 255);
                        } else if (cx > 0.0f && ((r >= 2.6f && r <= 3.5f) || (r >= 4.5f && r <= 5.4f)) && std::fabs(cy) <= 4.2f) {
                            col = packRgb565Helper(0, 210, 255);
                        }
                    }
                    hud_icons_rgb565[ic][y * HUD_ICON_SIZE + x] = col;
                }
            }
            if (oracle_brain.isSdStorageReady()) {
                writeRgb565AssetToSdHelper(path, hud_icons_rgb565[ic], HUD_ICON_PIXELS);
            }
        }
    }

    // =========================================================================
    // 3. GERA OS 4 TEMAS DE COCKPIT 320x240 16-BIT NO CARTÃO SD (153.6 KB CADA)
    // =========================================================================
    struct CockpitThemeSpec {
        float bg_r, bg_g, bg_b;
        float glow_r, glow_g, glow_b;
        float grid_r, grid_g, grid_b;
    };
    const CockpitThemeSpec theme_specs[SD_THEME_COUNT] = {
        {  6.0f,  9.0f, 22.0f,    0.0f, 52.0f, 105.0f,   18.0f, 44.0f, 82.0f }, // 0: Stealth Matrix Cyan
        {  5.0f, 14.0f, 18.0f,    0.0f, 75.0f,  58.0f,   14.0f, 64.0f, 52.0f }, // 1: Bio-Synth Emerald
        { 16.0f, 10.0f, 14.0f,   95.0f, 52.0f,  10.0f,   72.0f, 44.0f, 16.0f }, // 2: Solar Overclock Amber
        { 16.0f,  6.0f, 14.0f,   95.0f, 16.0f,  36.0f,   68.0f, 18.0f, 36.0f }  // 3: Cyber-Ops Rubi
    };

    std::vector<uint16_t> temp_theme(SCREEN_WIDTH * SCREEN_HEIGHT, COLOR_CYBER_BG_NAVY);
    for (uint8_t t = 0; t < SD_THEME_COUNT; ++t) {
        const char* t_path = SD_THEME_FILENAMES[t];
        if (force_regenerate || !sdFileExistsHelper(t_path)) {
            const CockpitThemeSpec& sp = theme_specs[t];
            for (int32_t y = 0; y < SCREEN_HEIGHT; ++y) {
                for (int32_t x = 0; x < SCREEN_WIDTH; ++x) {
                    float nx = (static_cast<float>(x) - 160.0f) / 160.0f;
                    float ny = (static_cast<float>(y) - 92.0f) / 120.0f;
                    float radial = std::sqrt(nx * nx + ny * ny);
                    if (radial > 1.35f) radial = 1.35f;

                    // Iluminação atmosférica radial suave em 16-bit com dithering Bayer 2x2 (zero Mach banding!)
                    float center_boost = std::max(0.0f, 1.0f - radial * 0.85f);
                    center_boost = center_boost * center_boost;
                    float r = sp.bg_r + sp.glow_r * center_boost * 0.30f;
                    float g = sp.bg_g + sp.glow_g * center_boost * 0.30f;
                    float b = sp.bg_b + sp.glow_b * center_boost * 0.30f;

                    // Horizonte cibernético sutil (Y = 112) e linhas de perspectiva 3D
                    if (y == 112 && x >= 16 && x < SCREEN_WIDTH - 16) {
                        r += sp.grid_r * 0.75f;
                        g += sp.grid_g * 0.75f;
                        b += sp.grid_b * 0.75f;
                    } else if (y > 112 && y < 166) {
                        float dy_floor = static_cast<float>(y - 112);
                        for (int32_t beam = -4; beam <= 4; ++beam) {
                            float beam_x = 160.0f + beam * (14.0f + dy_floor * 0.24f);
                            float dist_b = std::fabs(static_cast<float>(x) - beam_x);
                            if (dist_b < 1.1f) {
                                float w = (1.0f - (dist_b / 1.1f)) * (1.0f - dy_floor / 64.0f);
                                r += sp.grid_r * w * 0.60f;
                                g += sp.grid_g * w * 0.60f;
                                b += sp.grid_b * w * 0.60f;
                            }
                        }
                    }

                    // Trilhas de circuito impresso (PCB) sutis nas diagonais dos cantos superiores
                    int32_t corner_dx = (x < 160) ? x : (SCREEN_WIDTH - 1 - x);
                    int32_t corner_dy = (y < 120) ? y : (SCREEN_HEIGHT - 1 - y);
                    if (corner_dy >= 32 && corner_dy <= 158 && corner_dx >= 14 && corner_dx <= 48) {
                        if ((corner_dx + corner_dy == 76) || (corner_dx == 22 && corner_dy >= 36 && corner_dy <= 52)) {
                            r += sp.grid_r * 0.45f;
                            g += sp.grid_g * 0.45f;
                            b += sp.grid_b * 0.45f;
                        }
                    }

                    temp_theme[y * SCREEN_WIDTH + x] = packRgb565DitheredHelper(x, y, r, g, b);
                }
            }
            if (oracle_brain.isSdStorageReady()) {
                writeRgb565AssetToSdHelper(t_path, temp_theme.data(), temp_theme.size());
            }
        }
    }

    if (force_regenerate && oracle_brain.isSdStorageReady()) {
        const uint16_t ver_tag = 0x0003U;
        writeRgb565AssetToSdHelper(SD_GFX_PALETTE_VERSION_FILE, &ver_tag, 1);
    }

    sd_gfx_ready = loadSdThemeToBuffer(active_theme_index);
    M5_LOGI("[SD-GFX] Ativos graficos 16-bit HD v3 carregados do Cartao SD (Tema=%u, Olhos=64x64, Icones=12x12).",
            static_cast<unsigned int>(active_theme_index));
}

bool PetController::loadSdThemeToBuffer(uint8_t theme_index) {
    uint8_t idx = static_cast<uint8_t>(theme_index % SD_THEME_COUNT);
    if (active_theme_bg_rgb565.size() != static_cast<size_t>(SCREEN_WIDTH * SCREEN_HEIGHT)) {
        active_theme_bg_rgb565.assign(SCREEN_WIDTH * SCREEN_HEIGHT, COLOR_CYBER_BG_NAVY);
    }
    if (oracle_brain.isSdStorageReady() &&
        readRgb565AssetFromSdHelper(SD_THEME_FILENAMES[idx], active_theme_bg_rgb565.data(), active_theme_bg_rgb565.size())) {
        sd_gfx_ready = true;
        return true;
    }
    return false;
}

uint8_t PetController::getActiveOpticColorIndex() const {
    switch (current_emotion) {
        case EMOTION_GRUMPY:
        case EMOTION_STARTLED:
        case EMOTION_DIZZY:
            return 3U; // Vermelho Tático (Reservado apenas para fúria real ou susto!)
        case EMOTION_JUDGING:
        case EMOTION_SLEEPY:
            return 2U; // Âmbar Dourado (Sarcástico / Sobrancelha arqueada / Baixa energia)
        case EMOTION_HAPPY:
        case EMOTION_IDLE:
        case EMOTION_NEUTRAL:
        case EMOTION_FOCUSED:
        case EMOTION_LISTENING:
        case EMOTION_SLEEPING:
        default:
            // Segue a paleta do Tema de Cockpit ativo (0: Ciano, 1: Esmeralda, 2: Âmbar, 3: Rubi) para harmonia cromática total!
            return static_cast<uint8_t>(active_theme_index % EYE_LENS_COLOR_COUNT);
    }
}

uint16_t PetController::getActiveOpticPrimaryColor() const {
    switch (getActiveOpticColorIndex()) {
        case 1U: return COLOR_SONAR_GREEN;     // 0x07E8 Verde Esmeralda
        case 2U: return COLOR_CYBER_ORANGE;    // 0xFD20 Âmbar Dourado
        case 3U: return COLOR_OPTIC_NEON_RED;  // 0xF966 Vermelho Tático
        case 0U:
        default: return COLOR_CYBER_BLUE_NEON; // 0x05FF Ciano Cibernético
    }
}

uint16_t PetController::getActiveOpticCoreColor() const {
    switch (getActiveOpticColorIndex()) {
        case 1U: return 0x0204; // Fundo esmeralda escuro
        case 2U: return 0x3100; // Fundo âmbar escuro
        case 3U: return COLOR_OPTIC_CORE_DARK;
        case 0U:
        default: return 0x0126; // Fundo azul-ciano profundo
    }
}

bool PetController::playSdSoundEffect(HalSensors& hal_sensors, const char* relative_sd_path) {
    if (relative_sd_path == nullptr || !oracle_brain.isSdStorageReady()) {
        return false;
    }

    // Para o canal de áudio antes de realocar sfx_wav_buffer para evitar leitura concorrente na thread de som SDL2/I2S
    M5.Speaker.stop();

#if defined(ARDUINO)
    if (!SD.exists(relative_sd_path)) {
        return false;
    }
    File f = SD.open(relative_sd_path, FILE_READ);
    if (!f || f.size() <= VOICE_WAV_HEADER_OFFSET) {
        return false;
    }
    size_t sz = f.size();
    sfx_wav_buffer.resize(sz);
    f.read(sfx_wav_buffer.data(), sz);
    f.close();
#else
    std::ifstream f(relative_sd_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return false;
    }
    std::streamsize sz = f.tellg();
    if (sz <= static_cast<std::streamsize>(VOICE_WAV_HEADER_OFFSET)) {
        return false;
    }
    f.seekg(0, std::ios::beg);
    sfx_wav_buffer.resize(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(sfx_wav_buffer.data()), sz)) {
        return false;
    }
#endif

    hal_sensors.switchAudioBusMode(AUDIO_MODE_SPEAKER);
    M5.Speaker.playWav(sfx_wav_buffer.data(), sfx_wav_buffer.size());
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

bool PetController::generateAndLoadVoiceWav(const char* reply_text) {
    if (reply_text == nullptr || reply_text[0] == '\0') {
        return false;
    }

    // 1. Calcula hash determinístico FNV-1a 32-bit da frase para Cache de Voz Neural 24kHz no Cartão SD
    uint32_t fnv_hash = 2166136261U;
    for (const char* p = reply_text; *p != '\0'; ++p) {
        fnv_hash ^= static_cast<uint8_t>(*p);
        fnv_hash *= 16777619U;
    }

    char cache_path[128];
    std::snprintf(cache_path, sizeof(cache_path), "%s/tts_%08x.wav", SD_TTS_CACHE_DIR, static_cast<unsigned int>(fnv_hash));

    bool loaded_raw_wav = false;

#if defined(ARDUINO)
    // =========================================================================
    // ARQUITETURA HÍBRIDA (CAMINHO 3 + CAMINHO 1) NO M5STACK CORES3 (ESP32-S3)
    // =========================================================================
    // Passo 1 (Caminho 3): Verifica se o WAV 24kHz já está em cache no microSD (0ms de latência e 100% offline)
    if (oracle_brain.isSdStorageReady() && SD.exists(cache_path)) {
        File cached_wav = SD.open(cache_path, FILE_READ);
        if (cached_wav && cached_wav.size() > VOICE_WAV_HEADER_OFFSET) {
            size_t fsize = cached_wav.size();
            wav_buffer.resize(fsize);
            cached_wav.read(wav_buffer.data(), fsize);
            cached_wav.close();
            loaded_raw_wav = true;
            M5_LOGI("[SD-TTS] Voz neural 24kHz carregada em 0ms do cache SD: %s", cache_path);
        }
    }

    // Passo 2 (Caminho 1): Frase inédita da IA -> Sintetiza na nuvem via Gemini 2.5 Flash TTS (Wi-Fi direto, sem PC)
    if (!loaded_raw_wav) {
        if (oracle_brain.synthesizeCloudNeuralTtsWav(reply_text, wav_buffer)) {
            loaded_raw_wav = true;
            // Salva a voz limpa em 24kHz no cartão microSD para nunca mais precisar baixar a mesma frase
            if (oracle_brain.isSdStorageReady()) {
                File out_wav = SD.open(cache_path, FILE_WRITE);
                if (out_wav) {
                    out_wav.write(wav_buffer.data(), wav_buffer.size());
                    out_wav.close();
                    M5_LOGI("[SD-TTS] Voz Cloud Gemini 24kHz salva no cache microSD: %s", cache_path);
                }
            }
        }
    }
#else
    // =========================================================================
    // ARQUITETURA HÍBRIDA (CAMINHO 3 + CAMINHO 1) NO SIMULADOR PC
    // =========================================================================
    // Passo 1 (Caminho 3): Verifica se o arquivo WAV já está salvo no cache do Cartão SD (0ms de latência)
    {
        std::ifstream cached_file(cache_path, std::ios::binary | std::ios::ate);
        if (cached_file.is_open()) {
            std::streamsize c_size = cached_file.tellg();
            if (c_size > static_cast<std::streamsize>(VOICE_WAV_HEADER_OFFSET)) {
                cached_file.seekg(0, std::ios::beg);
                wav_buffer.resize(static_cast<size_t>(c_size));
                if (cached_file.read(reinterpret_cast<char*>(wav_buffer.data()), c_size)) {
                    loaded_raw_wav = true;
                    M5_LOGI("[SD-TTS] Voz neural 24kHz carregada instantaneamente (0ms) do cache SD: %s", cache_path);
                }
            }
        }
    }

    // Passo 2 (Caminho 1): Sintetiza frase inédita em 24kHz HD (via edge-tts local ou Gemini Cloud TTS) e salva no SD
    if (!loaded_raw_wav) {
        static int8_t external_tts_status = -1;
        if (external_tts_status == -1) {
            int check_res = std::system("edge-tts --version >nul 2>&1 && ffmpeg -version >nul 2>&1");
            external_tts_status = (check_res == 0) ? 1 : 0;
        }

        if (external_tts_status == 1) {
            std::remove("temp_voice.wav");
            {
                std::ofstream txt_file("temp_voice_text.txt", std::ios::binary | std::ios::trunc);
                if (txt_file.is_open()) {
                    txt_file.write(reply_text, static_cast<std::streamsize>(std::strlen(reply_text)));
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
                    if (file_size > static_cast<std::streamsize>(VOICE_WAV_HEADER_OFFSET)) {
                        wav_file.seekg(0, std::ios::beg);
                        wav_buffer.resize(static_cast<size_t>(file_size));
                        if (wav_file.read(reinterpret_cast<char*>(wav_buffer.data()), file_size)) {
                            loaded_raw_wav = true;
                        }
                    }
                }
            }
        }

        // Fallback automático para Gemini 2.5 Flash TTS Cloud caso edge-tts não esteja instalado
        if (!loaded_raw_wav) {
            loaded_raw_wav = oracle_brain.synthesizeCloudNeuralTtsWav(reply_text, wav_buffer);
        }

        // Persiste o WAV limpo 24kHz no cache do Cartão SD para reprodução 0ms nas próximas vezes
        if (loaded_raw_wav && oracle_brain.isSdStorageReady()) {
            std::ofstream sd_cache_out(cache_path, std::ios::binary | std::ios::trunc);
            if (sd_cache_out.is_open()) {
                sd_cache_out.write(reinterpret_cast<const char*>(wav_buffer.data()),
                                   static_cast<std::streamsize>(wav_buffer.size()));
                M5_LOGI("[SD-TTS] Frase sintetizada em 24kHz HD e salva no cache do Cartao SD: %s", cache_path);
            }
        }
    }
#endif

    if (!loaded_raw_wav || wav_buffer.size() <= VOICE_WAV_HEADER_OFFSET) {
        return false;
    }

    // Passo 3: Aplica DSP Emocional e Acústica Sci-Fi em C++ puro na RAM/PSRAM (Pitch Prosódico + High-Pass 180Hz + Comb)
    applyEmotionalSciFiDspToWav(wav_buffer, current_emotion);

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

    // Alterna barramento I2S para o alto-falante (AW88298 no CoreS3) durante a fala
    hal_sensors.switchAudioBusMode(AUDIO_MODE_SPEAKER);

    std::strncpy(lip_sync_state.full_text, reply_text, DIALOGUE_MAX_LEN - 1);
    lip_sync_state.full_text[DIALOGUE_MAX_LEN - 1] = '\0';
    lip_sync_state.visible_text[0] = '\0';
    lip_sync_state.total_text_length = static_cast<uint16_t>(std::strlen(lip_sync_state.full_text));
    lip_sync_state.current_char_index = 0;
    lip_sync_state.is_speaking = true;

    // 1. Tenta gerar e carregar voz neural em Português (WAV PCM 16-bit 16kHz Mono) no ambiente native_pc
    using_wav_voice = generateAndLoadVoiceWav(lip_sync_state.full_text);
    uint32_t start_ms = M5.millis();
    playback_start_ms = start_ms;
    lip_sync_state.next_char_timestamp_ms = start_ms;
    lip_sync_state.subtitle_expire_ms = 0;

    if (using_wav_voice && !wav_buffer.empty()) {
        // 2. Reproduz o buffer WAV de forma assíncrona no canal 0 sem bloquear o loop de 60 FPS
        M5.Speaker.playWav(wav_buffer.data(), wav_buffer.size());
    }
}

void PetController::playProceduralCharTone(char current_char) {
    // Ignora espaços na emissão sonora
    if (current_char == ' ') {
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
    if (!lip_sync_state.is_speaking) {
        target_pose.mouth_open_height = (current_emotion == EMOTION_STARTLED) ? 14.0f : MOUTH_CLOSED_HEIGHT;
        return;
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
        hal_sensors.switchAudioBusMode(AUDIO_MODE_MIC);
    }
}

void PetController::computeTargetFacePose(const GazeTargetData& gaze_data, uint32_t now_ms) {
    // 1. Micro-Saccades Oculares Naturais: pequenos saltos rápidos de fixação para dar vida biológica ao olhar
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

    // Alvo do rastreamento ocular normalizado [-1.0, 1.0] + micro-saccades e inclinação da cabeça (head roll)
    target_pose.current_eye_x = (gaze_data.target_x * FACE_GAZE_MAX_X) + saccade_offset_x;
    target_pose.current_eye_y = (gaze_data.target_y * FACE_GAZE_MAX_Y) + saccade_offset_y;
    target_pose.eye_offset_x = target_pose.current_eye_x;
    target_pose.eye_offset_y = target_pose.current_eye_y;

    float clamped_tilt = gaze_data.head_tilt_deg;
    if (clamped_tilt < -25.0f) clamped_tilt = -25.0f;
    if (clamped_tilt > 25.0f)  clamped_tilt = 25.0f;
    target_pose.head_tilt_deg = gaze_data.face_detected ? clamped_tilt : 0.0f;

    // 2. Controle de piscada natural aleatória
    if (!is_blinking && (now_ms - last_blink_ms > next_blink_interval_ms)) {
        is_blinking = true;
        last_blink_ms = now_ms;
    } else if (is_blinking && (now_ms - last_blink_ms > 125)) {
        is_blinking = false;
        last_blink_ms = now_ms;
        next_blink_interval_ms = 2400 + ((now_ms * 17) % 2600);
    }

    // 3. Configuração padrão da pose + Dilatação Reativa da Pupila (dilata ao falar/ouvir/sorrir, contrai em foco profundo)
    float pupil_react = 0.0f;
    if (lip_sync_state.is_speaking || current_emotion == EMOTION_LISTENING || gaze_data.is_smiling) {
        pupil_react = 1.5f;
    } else if (pomodoro_active || current_emotion == EMOTION_FOCUSED) {
        pupil_react = -1.0f;
    }

    target_pose.eye_radius_current = FACE_EYE_DEFAULT_R;
    target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R + pupil_react;
    target_pose.upper_eyelid_closure = 0.0f;  // Olhos totalmente abertos por padrão (sem corte reto de "olhar bravo")
    target_pose.lower_eyelid_closure = 0.16f; // Curvatura inferior sorridente e simpática
    target_pose.eyebrow_angle_deg = -5.0f;    // Sobrancelhas levemente erguidas no centro (/ \), expressão amigável
    target_pose.eyebrow_offset_y = -2.5f;
    target_pose.mouth_width_current = 18.0f;
    target_pose.mouth_curve_factor = 0.48f;   // Sorriso claro de companheiro de bancada
    target_pose.shake_offset_x = 0.0f;
    target_pose.shake_offset_y = 0.0f;

    switch (current_emotion) {
        case EMOTION_STARTLED:
            // Pupilas e olhos dilatam no susto + tremor intenso na tela
            target_pose.eye_radius_current = FACE_EYE_DEFAULT_R + 3.0f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R + 3.5f;
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_offset_y = -6.0f;
            target_pose.eyebrow_angle_deg = -14.0f;
            target_pose.mouth_width_current = 20.0f;
            target_pose.mouth_curve_factor = -0.55f;
            target_pose.shake_offset_x = (now_ms % 2 == 0) ? 4.5f : -4.5f;
            target_pose.shake_offset_y = ((now_ms / 2) % 2 == 0) ? 3.0f : -3.0f;
            break;

        case EMOTION_JUDGING:
            // Expressão irônica/marota estilo JARVIS: olho aberto, uma sobrancelha arqueada lá no alto e sorriso de canto!
            target_pose.upper_eyelid_closure = 0.04f;
            target_pose.lower_eyelid_closure = 0.22f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = -2.0f;
            target_pose.mouth_width_current = 19.0f;
            target_pose.mouth_curve_factor = 0.42f;
            break;

        case EMOTION_GRUMPY:
            // Sobrancelhas franzidas em V (reservado APENAS quando estiver realmente faminto > 90 ou humor < 15)
            target_pose.upper_eyelid_closure = 0.28f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R - 1.0f;
            target_pose.eyebrow_angle_deg = 14.0f;
            target_pose.eyebrow_offset_y = 2.5f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = -0.50f;
            target_pose.shake_offset_x = (now_ms % 6 == 0) ? 1.0f : 0.0f;
            break;

        case EMOTION_FOCUSED:
            // Olhar atento, vivo e amigável de parceiro de programação (zero cara de bravo!)
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.18f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = -2.0f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = 0.38f;
            break;

        case EMOTION_HAPPY:
            // Pálpebra inferior sobe em arco formando olhos sorridentes (^ ^) e sobrancelhas altas relaxadas
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.44f;
            target_pose.eyebrow_offset_y = -4.5f;
            target_pose.eyebrow_angle_deg = -8.0f;
            target_pose.mouth_width_current = 22.0f;
            target_pose.mouth_curve_factor = 0.92f;
            break;

        case EMOTION_LISTENING:
            // Olhos bem abertos e curiosos prestando atenção na voz do usuário
            target_pose.upper_eyelid_closure = 0.0f;
            target_pose.lower_eyelid_closure = 0.18f;
            target_pose.eyebrow_angle_deg = -6.0f;
            target_pose.eyebrow_offset_y = -3.0f;
            target_pose.mouth_width_current = 18.0f;
            target_pose.mouth_curve_factor = 0.45f;
            break;

        case EMOTION_SLEEPY:
            // Bocejando / quase dormindo
            target_pose.upper_eyelid_closure = 0.62f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_angle_deg = -4.0f;
            target_pose.eyebrow_offset_y = 3.0f;
            target_pose.mouth_curve_factor = 0.0f;
            if (!lip_sync_state.is_speaking) {
                target_pose.mouth_open_height = 10.0f;
                target_pose.mouth_width_current = 16.0f;
            }
            break;

        case EMOTION_SLEEPING:
            // Olhos totalmente fechados e respiração lenta tranquila
            target_pose.upper_eyelid_closure = 0.92f;
            target_pose.lower_eyelid_closure = 0.0f;
            target_pose.eyebrow_angle_deg = -2.0f;
            target_pose.eyebrow_offset_y = 4.0f;
            target_pose.mouth_width_current = 14.0f;
            target_pose.mouth_curve_factor = 0.25f;
            break;

        case EMOTION_DIZZY:
            // Sobrecarga / tontura com oscilação orbital das pupilas e tremor
            target_pose.eye_radius_current = FACE_EYE_DEFAULT_R + 2.0f;
            target_pose.pupil_radius_current = FACE_PUPIL_DEFAULT_R - 2.0f;
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
            target_pose.mouth_curve_factor = 0.48f;
            break;
    }

    // Ouvido 2.0: Se detectar batida rítmica/música no ambiente (PC ou CoreS3), o Bicho balança as sobrancelhas e a cabeça no ritmo!
    if (cached_audio_data.music_beat_active && !lip_sync_state.is_speaking && current_emotion != EMOTION_SLEEPY) {
        float beat_wave = std::sin(cached_audio_data.music_beat_phase * 6.2831853f);
        target_pose.eyebrow_offset_y += beat_wave * 2.5f;
        target_pose.head_tilt_deg += beat_wave * 3.2f;
        target_pose.mouth_curve_factor = std::fmax(target_pose.mouth_curve_factor, 0.72f);
    }

    if (is_blinking) {
        target_pose.upper_eyelid_closure = 0.95f;
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
    current_pose.eye_radius_current   = lerp_val(current_pose.eye_radius_current, target_pose.eye_radius_current, 0.22f);
    current_pose.pupil_radius_current = lerp_val(current_pose.pupil_radius_current, target_pose.pupil_radius_current, 0.25f);
    current_pose.upper_eyelid_closure = lerp_val(current_pose.upper_eyelid_closure, target_pose.upper_eyelid_closure, 0.30f);
    current_pose.lower_eyelid_closure = lerp_val(current_pose.lower_eyelid_closure, target_pose.lower_eyelid_closure, 0.25f);
    current_pose.eyebrow_angle_deg    = lerp_val(current_pose.eyebrow_angle_deg, target_pose.eyebrow_angle_deg, 0.20f);
    current_pose.eyebrow_offset_y     = lerp_val(current_pose.eyebrow_offset_y, target_pose.eyebrow_offset_y, 0.20f);
    current_pose.head_tilt_deg        = lerp_val(current_pose.head_tilt_deg, target_pose.head_tilt_deg, 0.22f);
    current_pose.mouth_open_height    = lerp_val(current_pose.mouth_open_height, target_pose.mouth_open_height, 0.42f);
    current_pose.mouth_width_current  = lerp_val(current_pose.mouth_width_current, target_pose.mouth_width_current, 0.22f);
    current_pose.mouth_curve_factor   = lerp_val(current_pose.mouth_curve_factor, target_pose.mouth_curve_factor, 0.20f);
    current_pose.shake_offset_x       = target_pose.shake_offset_x;
    current_pose.shake_offset_y       = target_pose.shake_offset_y;
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

void PetController::renderVisorRainDrops(M5Canvas& canvas, uint32_t now_ms, bool foreground_layer) {
    if (!isRainingOnVisor()) {
        return;
    }

    if (!foreground_layer) {
        // Camada 0 (Profundidade de Cockpit): feixes diagonais rápidos de chuva externa atrás das órbitas (Y = 28..166)
        for (uint32_t s = 0; s < 16U; ++s) {
            uint32_t seed = (s * 97U) + 13U;
            int32_t base_x = static_cast<int32_t>((seed * 23U) % static_cast<uint32_t>(SCREEN_WIDTH));
            uint32_t speed_div = 5U + (s % 4U);
            int32_t sy = 28 + static_cast<int32_t>(((now_ms / speed_div) + (seed * 19U)) % 136U);
            int32_t slen = 6 + static_cast<int32_t>(s % 6U);
            if (sy + slen > 167) {
                slen = 167 - sy;
            }
            if (slen > 1) {
                uint16_t streak_col = ((s & 1U) == 0U) ? COLOR_CYBER_GRID_BLUE : COLOR_CYBER_BLUE_DARK;
                canvas.drawLine(base_x, sy, base_x - 2, sy + slen, streak_col);
            }
        }
        return;
    }

    // Camada 1 (Vidro Frontal do Visor): gotas 3D refrativas escorrendo com rastro de condensação e splash na borda
    for (uint8_t i = 0; i < VISOR_RAINDROP_COUNT; ++i) {
        const VisorRainDrop& d = visor_raindrops[i];
        int32_t ix = static_cast<int32_t>(std::round(d.x));
        int32_t iy = static_cast<int32_t>(std::round(d.y));

        if (d.splash_frames > 0U) {
            // Animação de coroa de respingo (splash) ao colidir com a borda superior da caixa do Oráculo
            int32_t prog = static_cast<int32_t>(8U - d.splash_frames); // 1..7
            int32_t spread_x = prog * 2 + 1;
            int32_t arc_h = (prog <= 4) ? (prog + 1) : (8 - prog);
            uint16_t sp_col = (prog <= 3) ? TFT_WHITE : COLOR_CYBER_ICE_LIGHT;

            canvas.drawFastHLine(ix - spread_x, iy, spread_x * 2 + 1, COLOR_CYBER_GRID_BLUE);
            canvas.drawPixel(ix - spread_x, iy - arc_h, sp_col);
            canvas.drawPixel(ix + spread_x, iy - arc_h, sp_col);
            canvas.drawPixel(ix - (spread_x / 2), iy - arc_h - 1, TFT_WHITE);
            canvas.drawPixel(ix + (spread_x / 2), iy - arc_h - 1, TFT_WHITE);
            continue;
        }

        int32_t r = static_cast<int32_t>(d.radius);

        // 1. Rastro úmido de micro-gotas de condensação deixadas pelo escorrimento no vidro
        if (d.trail_len > 3U) {
            int32_t top_y = iy - static_cast<int32_t>(d.trail_len);
            for (int32_t ty = top_y; ty < iy - r - 1; ty += 3) {
                float wave_x = std::sin(static_cast<float>(ty) * 0.09f + d.meander_phase) * 1.1f;
                int32_t bx = ix - static_cast<int32_t>(std::round(wave_x));
                bool is_bead = (((ty + i) % 6) == 0);
                if (is_bead) {
                    canvas.drawPixel(bx, ty, COLOR_CYBER_ICE_LIGHT);
                    canvas.drawPixel(bx, ty + 1, COLOR_CYBER_GRID_BLUE);
                } else {
                    canvas.drawPixel(bx, ty, COLOR_CYBER_GRID_BLUE);
                }
            }
        }

        // 2. Corpo da gota em formato de lágrima volumétrica 3D (sombra refrativa + menisco ciano + brilho especular branco)
        // Sombra inferior externa da gota para destacar sobre qualquer cor de fundo/olho
        canvas.fillCircle(ix, iy + 1, r, COLOR_CYBER_BLUE_DARK);

        // Cauda cônica superior da gota escorrendo
        int32_t tail_tip_y = iy - r - (d.is_sliding ? 4 : 2);
        canvas.fillTriangle(ix - r + 1, iy - 1, ix + r - 1, iy - 1, ix, tail_tip_y, COLOR_CYBER_GRID_BLUE);

        // Volume principal da gota
        canvas.fillCircle(ix, iy, r, COLOR_CYBER_GRID_BLUE);
        canvas.drawCircle(ix, iy, r, COLOR_CYBER_BLUE_NEON);

        // Menisco interno luminoso (refração na base da gota)
        if (r >= 3) {
            canvas.drawFastHLine(ix - 1, iy + r - 1, 3, COLOR_CYBER_ICE_LIGHT);
            canvas.fillRect(ix - 1, iy - 1, 2, 2, TFT_WHITE);
        } else {
            canvas.drawPixel(ix, iy + 1, COLOR_CYBER_ICE_LIGHT);
            canvas.drawPixel(ix - 1, iy - 1, TFT_WHITE);
        }
        // Brilho especular cristalino no topo esquerdo da gota
        canvas.drawPixel(ix - (r / 2), iy - (r / 2), TFT_WHITE);
    }
}

void PetController::renderCyberGridBackground(M5Canvas& canvas, uint32_t now_ms) {
    canvas.setSwapBytes(true);

    // 1. Se o tema 16-bit pré-renderizado no Cartão SD estiver carregado na RAM/PSRAM, faz blit direto de 320x240!
    if (sd_gfx_ready && active_theme_bg_rgb565.size() == static_cast<size_t>(SCREEN_WIDTH * SCREEN_HEIGHT)) {
        canvas.pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, active_theme_bg_rgb565.data());
    } else {
        canvas.fillSprite(COLOR_CYBER_BG_NAVY);
        for (int32_t gx = 20; gx < SCREEN_WIDTH; gx += 28) {
            canvas.drawFastVLine(gx, 0, SCREEN_HEIGHT, COLOR_CYBER_GRID_DIM);
        }
    }

    // 2. Deslocamento contínuo descendente (now_ms * 0.05f) para fluxo de dados 3D sobre o cockpit 16-bit
    float grid_scroll = static_cast<float>(now_ms) * 0.05f;
    int32_t scroll_offset = static_cast<int32_t>(grid_scroll) % 24;

    uint16_t primary_col = getActiveOpticPrimaryColor();
    for (int32_t gx = 48; gx < SCREEN_WIDTH - 32; gx += 56) {
        for (int32_t gy = 32 + scroll_offset; gy < SCREEN_HEIGHT - 72; gy += 36) {
            canvas.drawPixel(gx, gy, primary_col);
        }
    }

    // 3. Feixes atmosféricos de chuva ao fundo quando estiver chovendo lá fora (ou simulação [W] ativa)
    renderVisorRainDrops(canvas, now_ms, false);
}

void PetController::renderCombatOpticEye(
    M5Canvas& canvas,
    int32_t center_x,
    int32_t center_y,
    int32_t pupil_dx,
    int32_t pupil_dy,
    bool is_left_eye,
    uint32_t now_ms
) {
    float scale_factor = current_pose.eye_radius_current / FACE_EYE_DEFAULT_R;
    int32_t rx = static_cast<int32_t>(FACE_EYE_RX_DEFAULT * scale_factor); // ~26px
    int32_t ry = static_cast<int32_t>(FACE_EYE_RY_DEFAULT * scale_factor); // ~23px

    uint8_t color_idx = getActiveOpticColorIndex();
    uint16_t optic_primary = getActiveOpticPrimaryColor();
    uint16_t optic_core = getActiveOpticCoreColor();

    // 1. Medidores Orbitais Segmentados girando suavemente em torno das órbitas oculares em paleta harmônica
    int32_t orb_r = rx + 6;
    canvas.drawCircle(center_x, center_y, orb_r, COLOR_CYBER_GRID_BLUE);

    float base_spin = static_cast<float>(now_ms) * (is_left_eye ? 0.0020f : -0.0020f);
    for (int seg = 0; seg < 4; ++seg) {
        float a0 = base_spin + static_cast<float>(seg) * 1.570796f;
        for (int step = 0; step < 6; ++step) {
            float a = a0 + static_cast<float>(step) * 0.09f;
            int32_t ox = center_x + static_cast<int32_t>(std::round(std::cos(a) * static_cast<float>(orb_r)));
            int32_t oy = center_y + static_cast<int32_t>(std::round(std::sin(a) * static_cast<float>(orb_r)));
            uint16_t seg_col = (seg % 2 == 0) ? optic_primary : COLOR_CYBER_ICE_LIGHT;
            canvas.fillRect(ox - 1, oy - 1, 2, 2, seg_col);
        }
    }

    // 2. Renderiza a Textura 16-bit 64x64 HD da Lente Óptica do Cartão SD (com malha favo de mel + reflexo de vidro 3D)
    const uint16_t* lens_tex = eye_lens_textures_rgb565[color_idx % EYE_LENS_COLOR_COUNT];
    if (std::fabs(scale_factor - 1.0f) < 0.06f) {
        canvas.pushImage(
            center_x - (EYE_LENS_TEX_SIZE / 2),
            center_y - (EYE_LENS_TEX_SIZE / 2),
            EYE_LENS_TEX_SIZE,
            EYE_LENS_TEX_SIZE,
            lens_tex,
            COLOR_KEY_TRANSPARENT
        );
    } else {
        // Amostragem proporcional da textura 64x64 quando o olho está escalonado (ex.: PIP ativo ou susto)
        int32_t half_draw = static_cast<int32_t>(std::round(32.0f * scale_factor));
        if (half_draw < 8) half_draw = 8;
        for (int32_t dy = -half_draw; dy < half_draw; ++dy) {
            int32_t sy = ((dy + half_draw) * EYE_LENS_TEX_SIZE) / (half_draw * 2);
            if (sy < 0) sy = 0;
            if (sy >= EYE_LENS_TEX_SIZE) sy = EYE_LENS_TEX_SIZE - 1;
            for (int32_t dx = -half_draw; dx < half_draw; ++dx) {
                int32_t sx = ((dx + half_draw) * EYE_LENS_TEX_SIZE) / (half_draw * 2);
                if (sx < 0) sx = 0;
                if (sx >= EYE_LENS_TEX_SIZE) sx = EYE_LENS_TEX_SIZE - 1;
                uint16_t px_col = lens_tex[sy * EYE_LENS_TEX_SIZE + sx];
                if (px_col != COLOR_KEY_TRANSPARENT) {
                    canvas.drawPixel(center_x + dx, center_y + dy, px_col);
                }
            }
        }
    }

    // 3. Pupilas com Padrão de Dados (Retículo de Mira / Target Lock) reagindo a current_eye_x e current_eye_y
    int32_t reticle_x = center_x + pupil_dx;
    int32_t reticle_y = center_y + pupil_dy;
    int32_t pup_r = static_cast<int32_t>(current_pose.pupil_radius_current);

    // Câmara escura interna do sensor óptico e anéis concêntricos na cor emocional ativa
    canvas.fillCircle(reticle_x, reticle_y, pup_r + 3, optic_core);
    canvas.drawCircle(reticle_x, reticle_y, pup_r + 3, optic_primary);
    canvas.drawCircle(reticle_x, reticle_y, pup_r, TFT_WHITE);

    // Ticks cruzados do retículo da pupila (Crosshair) em azul-gelo harmônico
    canvas.drawFastHLine(reticle_x - pup_r - 5, reticle_y, 4, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastHLine(reticle_x + pup_r + 2, reticle_y, 4, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(reticle_x, reticle_y - pup_r - 5, 4, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(reticle_x, reticle_y + pup_r + 2, 4, COLOR_CYBER_ICE_LIGHT);

    // Núcleo laser brilhante + segundo ponto de brilho especular na pupila (dá vida e simpatia ao olhar!)
    canvas.fillRect(reticle_x - 1, reticle_y - 1, 3, 3, TFT_WHITE);
    canvas.fillCircle(reticle_x - (pup_r / 2) - 1, reticle_y - (pup_r / 2) - 1, 2, TFT_WHITE);
    canvas.drawPixel(reticle_x, reticle_y, optic_primary);

    // 4. Obturador Mecânico Superior (apenas durante piscada, sono ou quando realmente ranzinza)
    float closure = current_pose.upper_eyelid_closure;
    if (closure > 0.03f) {
        int32_t lid_cut_y = (center_y - ry) + static_cast<int32_t>((ry * 1.85f) * closure);
        if (lid_cut_y > center_y - ry) {
            for (int32_t y = center_y - ry; y <= lid_cut_y; ++y) {
                int32_t abs_dy = std::abs(y - center_y);
                int32_t cur_w = rx;
                if (abs_dy > (ry - 9)) {
                    cur_w = rx - (abs_dy - (ry - 9));
                }
                if (cur_w > 0) {
                    uint16_t shutter_col = (y >= lid_cut_y - 2) ? optic_primary : COLOR_CYBER_BG_NAVY;
                    canvas.drawFastHLine(center_x - cur_w, y, cur_w * 2 + 1, shutter_col);
                }
            }
        }
    }

    // Obturador mecânico inferior em arco sorridente quando feliz ou bem-humorado (olhos sorridentes estilo Cozmo/Vector)
    if (current_pose.lower_eyelid_closure > 0.05f) {
        int32_t base_lower_h = static_cast<int32_t>(ry * current_pose.lower_eyelid_closure);
        for (int32_t dx = -rx; dx <= rx; ++dx) {
            float norm_x = static_cast<float>(dx) / static_cast<float>(rx > 0 ? rx : 1);
            int32_t arch_boost = static_cast<int32_t>(std::round((1.0f - norm_x * norm_x) * 4.0f));
            int32_t col_top_y = (center_y + ry) - base_lower_h - arch_boost;
            for (int32_t y = col_top_y; y <= center_y + ry; ++y) {
                int32_t abs_dy = std::abs(y - center_y);
                int32_t max_w = rx - ((abs_dy > (ry - 9)) ? (abs_dy - (ry - 9)) : 0);
                if (std::abs(dx) <= max_w) {
                    uint16_t shutter_col = (y <= col_top_y + 1) ? optic_primary : COLOR_CYBER_BG_NAVY;
                    canvas.drawPixel(center_x + dx, y, shutter_col);
                }
            }
        }
    }

    // Colchetes angulares de viseira tecnológica nos 4 cantos externos da órbita
    int32_t bx0 = center_x - rx - 5;
    int32_t bx1 = center_x + rx + 5;
    int32_t by0 = center_y - ry - 3;
    int32_t by1 = center_y + ry + 3;
    canvas.drawFastHLine(bx0, by0, 6, optic_primary);
    canvas.drawFastVLine(bx0, by0, 6, optic_primary);
    canvas.drawFastHLine(bx1 - 5, by0, 6, optic_primary);
    canvas.drawFastVLine(bx1, by0, 6, optic_primary);
    canvas.drawFastHLine(bx0, by1, 6, optic_primary);
    canvas.drawFastVLine(bx0, by1 - 5, 6, optic_primary);
    canvas.drawFastHLine(bx1 - 5, by1, 6, optic_primary);
    canvas.drawFastVLine(bx1, by1 - 5, 6, optic_primary);
}

void PetController::renderLaserArmorEyebrow(
    M5Canvas& canvas,
    int32_t center_x,
    int32_t center_y,
    float angle_deg,
    bool is_left_brow
) {
    // Sobrancelhas expressivas: levemente erguidas (/ \) por padrão (amigável) e arqueadas com charme irônico em EMOTION_JUDGING
    float effective_angle = is_left_brow ? angle_deg : -angle_deg;
    if (current_emotion == EMOTION_JUDGING) {
        if (!is_left_brow) {
            // Arqueia a sobrancelha direita lá no alto com ângulo curioso/irônico (sem franzir!)
            effective_angle = 8.0f;
            center_y -= 6;
        } else {
            effective_angle = -3.0f;
            center_y -= 1;
        }
    }

    float rad = effective_angle * (3.14159265f / 180.0f);
    float cos_a = std::cos(rad);
    float sin_a = std::sin(rad);

    float brow_scale = current_pose.eye_radius_current / FACE_EYE_DEFAULT_R;
    float half_len = 22.0f * brow_scale;
    constexpr float half_thick = 2.5f;

    auto rot_x = [&](float lx, float ly) -> int32_t {
        return center_x + static_cast<int32_t>(std::round(lx * cos_a - ly * sin_a));
    };
    auto rot_y = [&](float lx, float ly) -> int32_t {
        return center_y + static_cast<int32_t>(std::round(lx * sin_a + ly * cos_a));
    };

    // Moldura metálica escura chanfrada
    int32_t ox0 = rot_x(-half_len - 2.0f, -half_thick - 1.0f);
    int32_t oy0 = rot_y(-half_len - 2.0f, -half_thick - 1.0f);
    int32_t ox1 = rot_x( half_len + 2.0f, -half_thick - 1.0f);
    int32_t oy1 = rot_y( half_len + 2.0f, -half_thick - 1.0f);
    int32_t ox2 = rot_x( half_len + 2.0f,  half_thick + 1.0f);
    int32_t oy2 = rot_y( half_len + 2.0f,  half_thick + 1.0f);
    int32_t ox3 = rot_x(-half_len - 2.0f,  half_thick + 1.0f);
    int32_t oy3 = rot_y(-half_len - 2.0f,  half_thick + 1.0f);

    canvas.fillTriangle(ox0, oy0, ox1, oy1, ox2, oy2, COLOR_ARMOR_VISOR);
    canvas.fillTriangle(ox0, oy0, ox2, oy2, ox3, oy3, COLOR_ARMOR_VISOR);

    // Núcleo luminoso seguindo a cor emocional ativa (Ciano/Esmeralda/Âmbar, e Vermelho apenas quando bravo!)
    uint16_t brow_col = getActiveOpticPrimaryColor();
    int32_t ix0 = rot_x(-half_len, -half_thick);
    int32_t iy0 = rot_y(-half_len, -half_thick);
    int32_t ix1 = rot_x( half_len, -half_thick);
    int32_t iy1 = rot_y( half_len, -half_thick);
    int32_t ix2 = rot_x( half_len,  half_thick);
    int32_t iy2 = rot_y( half_len,  half_thick);
    int32_t ix3 = rot_x(-half_len,  half_thick);
    int32_t iy3 = rot_y(-half_len,  half_thick);

    canvas.fillTriangle(ix0, iy0, ix1, iy1, ix2, iy2, brow_col);
    canvas.fillTriangle(ix0, iy0, ix2, iy2, ix3, iy3, brow_col);

    // Feixe de alta energia na borda inferior da placa
    int32_t lx0 = rot_x(-half_len + 2.0f, half_thick);
    int32_t ly0 = rot_y(-half_len + 2.0f, half_thick);
    int32_t lx1 = rot_x( half_len - 2.0f, half_thick);
    int32_t ly1 = rot_y( half_len - 2.0f, half_thick);
    canvas.drawLine(lx0, ly0, lx1, ly1, TFT_WHITE);
}

void PetController::renderTacticalCallouts(
    M5Canvas& canvas,
    int32_t left_eye_x,
    int32_t right_eye_x,
    int32_t eye_y,
    uint32_t now_ms
) {
    bool pip_active = debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms);
    uint16_t primary_col = getActiveOpticPrimaryColor();

    // 1. Indicador central de alerta entre os sensores ópticos (exibido apenas em susto/sobrecarga/alerta real)
    if (current_emotion == EMOTION_STARTLED || current_emotion == EMOTION_GRUMPY || current_emotion == EMOTION_DIZZY) {
        int32_t warn_x = (left_eye_x + right_eye_x) / 2;
        int32_t warn_y = eye_y - 22;
        bool alert_blink = ((now_ms / 260U) % 2U) == 0U;
        uint16_t warn_col = alert_blink ? COLOR_OPTIC_NEON_RED : COLOR_CYBER_ORANGE;

        canvas.drawTriangle(warn_x, warn_y - 5, warn_x - 5, warn_y + 4, warn_x + 5, warn_y + 4, warn_col);
        canvas.drawFastVLine(warn_x, warn_y - 2, 3, COLOR_CYBER_ICE_LIGHT);
        canvas.drawPixel(warn_x, warn_y + 2, COLOR_CYBER_ICE_LIGHT);
    }

    // 2. Faixas de Telemetria Tática posicionadas abaixo das órbitas (Y = 117) nas laterais, longe da boca central
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);

    constexpr int32_t call_band_y = 117;

    // Callout Esquerdo (X = 18..102): Nível RPG / Clima / CPU & RAM / Tema SD
    canvas.drawLine(left_eye_x - 16, eye_y + 25, 28, call_band_y - 2, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastHLine(18, call_band_y - 2, 76, primary_col);
    canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(18, call_band_y + 1);
    const ExternalWorldContext& world_ctx = world_context_service.getWorldContext();
    const UserHabitMatrix& habits = oracle_brain.getHabitMatrix();
    uint32_t cycle_slot = (now_ms / 3200U) % 5U;
    if (rain_simulation_override) {
        canvas.print("CLIMA:CHUVA[W]");
    } else if (cached_audio_data.is_typing_cadence) {
        canvas.print("DSP:DIGITANDO");
    } else if (cached_audio_data.music_beat_active) {
        canvas.print("DSP:RITMO/BPM");
    } else if (cycle_slot == 0U) {
        canvas.printf("LV.%02u XP:%u",
                      static_cast<unsigned int>(habits.cyber_level),
                      static_cast<unsigned int>(habits.xp_points));
    } else if (cycle_slot == 1U && world_ctx.has_valid_sync) {
        canvas.printf("EXT %.0fC %s", world_ctx.outdoor_temp_celsius, isRainingOnVisor() ? "CHUVA" : "SECO");
    } else if (cycle_slot == 2U && (cached_env_data.cpu_usage_percent > 0U || cached_env_data.ram_usage_percent > 0U)) {
        canvas.printf("CPU:%u%% RAM:%u%%",
                      static_cast<unsigned int>(cached_env_data.cpu_usage_percent),
                      static_cast<unsigned int>(cached_env_data.ram_usage_percent));
    } else if (cycle_slot == 3U) {
        canvas.print(!cached_audio_data.vad_enabled ? "VAD:OFF [V]"
                     : (cached_audio_data.vad_keyword_mode ? "VAD:BICHO [V]" : "VAD:LIVRE [V]"));
    } else if (ambient_focus_enabled) {
        canvas.print("AMB:FOCO 24K");
    } else {
        canvas.printf("TEMA SD:%u/4", static_cast<unsigned int>(active_theme_index + 1U));
    }

    // Callout Direito (X = 224..302): Alertas (Postura/Celular/Procrastinação) / Git / Dólar / Mira
    bool posture_alert = (bad_posture_since_ms != 0 && (now_ms - bad_posture_since_ms > 2000U));
    bool phone_alert   = (phone_look_down_since_ms != 0 && (now_ms - phone_look_down_since_ms > 1500U));
    bool procr_alert   = (procrastination_since_ms != 0 && (now_ms - procrastination_since_ms > 2500U));
    uint16_t right_line_col = (posture_alert || phone_alert || procr_alert || world_ctx.is_github_down)
                                  ? COLOR_OPTIC_NEON_RED
                                  : primary_col;
    if (!pip_active) {
        canvas.drawLine(right_eye_x + 16, eye_y + 25, 290, call_band_y - 2, COLOR_CYBER_GRID_BLUE);
    }
    canvas.drawFastHLine(224, call_band_y - 2, 78, right_line_col);
    canvas.setTextColor((posture_alert || phone_alert || procr_alert || world_ctx.is_github_down)
                            ? COLOR_OPTIC_NEON_RED
                            : COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(224, call_band_y + 1);
    uint32_t right_slot = (now_ms / 3500U) % 3U;
    if (posture_alert) {
        canvas.print("POSTURA:ALERTA");
    } else if (phone_alert) {
        canvas.print("CELULAR:DETECT");
    } else if (procr_alert) {
        canvas.print("FOCO:DISTRACAO");
    } else if (right_slot == 1U && cached_env_data.git_modified_files > 0U) {
        canvas.printf("GIT:%u MODIF", static_cast<unsigned int>(cached_env_data.git_modified_files));
    } else if (right_slot == 2U && world_ctx.has_valid_sync) {
        if (world_ctx.is_github_down) {
            canvas.print("GH:OFFLINE!");
        } else {
            canvas.printf("USD R$%.2f", world_ctx.usd_brl_rate);
        }
    } else {
        canvas.printf("SYS LOCK %+02d", static_cast<int>(current_pose.current_eye_x));
    }

    // Mira central discreta de telemetria entre as órbitas
    int32_t mid_x = (left_eye_x + right_eye_x) / 2;
    canvas.drawFastHLine(mid_x - 4, eye_y, 9, COLOR_CYBER_GRID_BLUE);
    canvas.drawPixel(mid_x, eye_y, primary_col);
}

void PetController::renderSynthesisReticleMouth(M5Canvas& canvas, int32_t mouth_x, int32_t mouth_y, uint32_t now_ms) {
    // Boca de Retículo / Interface de Síntese compacta (largura 88px: X = 116..204) com curvatura emocional amigável
    constexpr int32_t synth_total_w = 88;
    constexpr int32_t num_bars = 11;
    constexpr int32_t bar_w = 5;
    constexpr int32_t bar_step = 8;

    int32_t left_x = mouth_x - (synth_total_w / 2);
    float open_norm = (current_pose.mouth_open_height - MOUTH_CLOSED_HEIGHT) /
                      (MOUTH_OPEN_MAX_HEIGHT - MOUTH_CLOSED_HEIGHT);
    if (open_norm < 0.0f) open_norm = 0.0f;
    if (open_norm > 1.0f) open_norm = 1.0f;

    uint16_t primary_col = getActiveOpticPrimaryColor();

    // Colchetes laterais de contenção do sintetizador vocal (altura contida ±6px para não tocar na aba do Oráculo)
    canvas.drawFastVLine(left_x - 5, mouth_y - 6, 13, primary_col);
    canvas.drawFastHLine(left_x - 5, mouth_y - 6, 4, primary_col);
    canvas.drawFastHLine(left_x - 5, mouth_y + 6, 4, primary_col);

    int32_t right_bracket_x = left_x + synth_total_w + 3;
    canvas.drawFastVLine(right_bracket_x, mouth_y - 6, 13, primary_col);
    canvas.drawFastHLine(right_bracket_x - 3, mouth_y - 6, 4, primary_col);
    canvas.drawFastHLine(right_bracket_x - 3, mouth_y + 6, 4, primary_col);

    // Barras verticais moduladas pelo áudio (RMS Lip-Sync) e curvadas claramente em sorriso/smirk amigável
    for (int32_t i = 0; i < num_bars; ++i) {
        float norm_pos = (static_cast<float>(i) - 5.0f) / 5.0f; // [-1.0 .. +1.0]
        int32_t dist_center = std::abs(i - (num_bars / 2));
        float envelope = 1.0f - (static_cast<float>(dist_center) / static_cast<float>((num_bars / 2) + 1));

        // Curvatura de sorriso (+mouth_curve_factor abaixa o centro e levanta as pontas laterais em U; em EMOTION_JUDGING faz um smirk maroto!)
        float curve_arch = (0.45f - norm_pos * norm_pos) * current_pose.mouth_curve_factor * 5.2f;
        if (current_emotion == EMOTION_JUDGING) {
            curve_arch -= norm_pos * 2.4f; // Sorriso de canto irônico (lado direito visivelmente mais alto)
        }
        int32_t bar_center_y = mouth_y + static_cast<int32_t>(std::round(curve_arch));

        int32_t bar_half_h = 1;
        if (lip_sync_state.is_speaking || current_pose.mouth_open_height > 4.0f) {
            float wave = std::fabs(std::sin(static_cast<float>(now_ms) * 0.035f + static_cast<float>(i) * 0.85f));
            float active_amp = open_norm * envelope * (0.45f + 0.55f * wave);
            bar_half_h = 1 + static_cast<int32_t>(std::round(active_amp * 7.5f));
        } else if (current_emotion == EMOTION_STARTLED) {
            bar_half_h = 2 + static_cast<int32_t>((now_ms + i * 7U) % 5U);
        } else {
            bar_half_h = (i % 2 == 0) ? 2 : 1;
        }

        int32_t bx = left_x + (i * bar_step);
        uint16_t bar_col = primary_col;
        if (bar_half_h >= 6) {
            bar_col = TFT_WHITE;
        } else if (bar_half_h >= 3) {
            bar_col = COLOR_CYBER_ICE_LIGHT;
        }

        canvas.fillRect(bx, bar_center_y - bar_half_h, bar_w, (bar_half_h * 2) + 1, bar_col);
        if (bar_half_h >= 4) {
            canvas.drawFastHLine(bx, bar_center_y - bar_half_h - 2, bar_w, TFT_WHITE);
            canvas.drawFastHLine(bx, bar_center_y + bar_half_h + 2, bar_w, TFT_WHITE);
        }
    }
}

void PetController::renderEmotionalParticles(
    M5Canvas& canvas,
    int32_t left_eye_x,
    int32_t right_eye_x,
    int32_t eye_y,
    uint32_t now_ms
) {
    uint16_t primary_col = getActiveOpticPrimaryColor();
    int32_t mid_x = (left_eye_x + right_eye_x) / 2;

    // 1. Anel Neural de Processamento quando aguardando resposta da IA (Gemini Flash-Lite)
    if (oracle_brain.isWaitingAi()) {
        int32_t ring_y = eye_y - 18;
        canvas.drawCircle(mid_x, ring_y, 9, COLOR_CYBER_GRID_BLUE);
        for (int k = 0; k < 6; ++k) {
            float ang = (static_cast<float>(now_ms) * 0.008f) + (static_cast<float>(k) * 1.047197f);
            int32_t nx = mid_x + static_cast<int32_t>(std::round(std::cos(ang) * 9.0f));
            int32_t ny = ring_y + static_cast<int32_t>(std::round(std::sin(ang) * 9.0f));
            canvas.fillCircle(nx, ny, (k % 2 == 0) ? 2 : 1, (k % 2 == 0) ? COLOR_CYBER_ORANGE : TFT_WHITE);
        }
        return;
    }

    // 2. Partículas de Hibernação ("Z" flutuantes ascendentes) em EMOTION_SLEEPING / EMOTION_SLEEPY
    if (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) {
        canvas.setFont(&fonts::Font0);
        canvas.setTextSize(1);
        for (int i = 0; i < 3; ++i) {
            uint32_t phase = (now_ms / 45U + static_cast<uint32_t>(i * 22)) % 60U;
            int32_t zx = right_eye_x + 18 + (i * 8) + static_cast<int32_t>(std::sin(static_cast<float>(phase) * 0.15f) * 3.0f);
            int32_t zy = (eye_y - 6) - static_cast<int32_t>(phase / 2U);
            if (zy > 32 && zx < SCREEN_WIDTH - 18) {
                canvas.setTextColor((i == 0) ? TFT_WHITE : COLOR_CYBER_ICE_LIGHT);
                canvas.setCursor(zx, zy);
                canvas.print("z");
            }
        }
        return;
    }

    // 3. Faíscas Luminosas de Bom Humor / Carinho / Sorriso em EMOTION_HAPPY
    if (current_emotion == EMOTION_HAPPY) {
        for (int i = 0; i < 4; ++i) {
            uint32_t t = (now_ms / 35U + static_cast<uint32_t>(i * 19)) % 40U;
            int32_t sx = ((i % 2 == 0) ? (left_eye_x - 32) : (right_eye_x + 30)) + ((i > 1) ? 6 : -4);
            int32_t sy = (eye_y + 10) - static_cast<int32_t>(t);
            if (sy > 34 && sx > 16 && sx < SCREEN_WIDTH - 16) {
                uint16_t sp_col = (t < 20U) ? TFT_WHITE : primary_col;
                canvas.drawFastHLine(sx - 2, sy, 5, sp_col);
                canvas.drawFastVLine(sx, sy - 2, 5, sp_col);
            }
        }
        return;
    }

    // 4. Ondas Acústicas Concêntricas em EMOTION_LISTENING (Escuta Ativa Hands-Free)
    if (current_emotion == EMOTION_LISTENING) {
        int32_t wave_r = 4 + static_cast<int32_t>((now_ms / 60U) % 12U);
        canvas.drawCircle(mid_x, eye_y, wave_r, primary_col);
        canvas.drawCircle(mid_x, eye_y, (wave_r + 6) % 16, COLOR_CYBER_ICE_LIGHT);
    }
}

void PetController::renderPetFace(M5Canvas& canvas) {
    canvas.setSwapBytes(true);
    uint32_t now_ms = M5.millis();

    // 1. Cockpit Sci-Fi 16-bit carregado do SD (ou Grid Cibernético 3D procedural como fallback)
    renderCyberGridBackground(canvas, now_ms);

    int32_t shake_x = static_cast<int32_t>(current_pose.shake_offset_x);
    int32_t shake_y = static_cast<int32_t>(current_pose.shake_offset_y);

    // Quando o visor PIP estiver aberto no quadrante superior direito, desloca suavemente as órbitas para a esquerda
    // para que o olho direito jamais fique coberto pela janela da câmera/timelapse!
    bool pip_active = debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms);
    int32_t base_left_x  = pip_active ? 68  : FACE_EYE_LEFT_X;
    int32_t base_right_x = pip_active ? 168 : FACE_EYE_RIGHT_X;

    // Posição base dos sensores ópticos acompanhando o olhar + inclinação lateral da cabeça (head roll)
    int32_t face_shift_x = static_cast<int32_t>(current_pose.current_eye_x * 0.40f) + shake_x;
    int32_t face_shift_y = static_cast<int32_t>(current_pose.current_eye_y * 0.30f) + shake_y;
    int32_t tilt_shift_y = static_cast<int32_t>(current_pose.head_tilt_deg * 0.25f);

    int32_t left_eye_x  = base_left_x + face_shift_x;
    int32_t right_eye_x = base_right_x + face_shift_x;
    int32_t eye_y       = FACE_EYE_BASE_Y + face_shift_y;
    int32_t left_eye_y  = eye_y - tilt_shift_y;
    int32_t right_eye_y = eye_y + tilt_shift_y;

    // Deslocamento do retículo da pupila (Target Lock)
    int32_t pupil_dx = static_cast<int32_t>(current_pose.current_eye_x * 0.55f);
    int32_t pupil_dy = static_cast<int32_t>(current_pose.current_eye_y * 0.55f);

    // Reduz levemente a escala das órbitas quando o visor PIP 64x48 está ativo ao lado
    float saved_eye_r = current_pose.eye_radius_current;
    if (pip_active) {
        current_pose.eye_radius_current = saved_eye_r * 0.84f;
    }

    // 2. Linhas de chamada táticas e telemetria lateral
    renderTacticalCallouts(canvas, left_eye_x, right_eye_x, eye_y, now_ms);

    // 2.5. Partículas Emocionais Dinâmicas (Anel Neural de IA, Faíscas Felizes, Ondas de Escuta ou "Z" de Sono)
    renderEmotionalParticles(canvas, left_eye_x, right_eye_x, eye_y, now_ms);

    // 3. Olhos de Lente Óptica HD (texturizados via SD 64x64 RGB565 com colméia hexagonal e Lens Flare)
    renderCombatOpticEye(canvas, left_eye_x, left_eye_y, pupil_dx, pupil_dy, true, now_ms);
    renderCombatOpticEye(canvas, right_eye_x, right_eye_y, pupil_dx, pupil_dy, false, now_ms);

    // 4. Sobrancelhas expressivas com feixe luminoso na cor do humor atual
    int32_t brow_dist = pip_active ? 22 : 25;
    int32_t left_brow_y  = (left_eye_y - brow_dist) + static_cast<int32_t>(current_pose.eyebrow_offset_y);
    int32_t right_brow_y = (right_eye_y - brow_dist) + static_cast<int32_t>(current_pose.eyebrow_offset_y);
    if (left_brow_y < 36) left_brow_y = 36;   // Garante que nunca invada o HUD superior (Y <= 29)
    if (right_brow_y < 36) right_brow_y = 36;
    float tilt_brow_mod  = current_pose.head_tilt_deg * 0.30f;
    renderLaserArmorEyebrow(canvas, left_eye_x + 2, left_brow_y, current_pose.eyebrow_angle_deg + tilt_brow_mod, true);
    renderLaserArmorEyebrow(canvas, right_eye_x - 2, right_brow_y, current_pose.eyebrow_angle_deg - tilt_brow_mod, false);

    current_pose.eye_radius_current = saved_eye_r;

    // 5. Boca de Retículo / Interface de Síntese Vocal centralizada com curvatura emocional
    int32_t mouth_x = (SCREEN_WIDTH / 2) + static_cast<int32_t>(current_pose.current_eye_x * 0.30f) + shake_x;
    int32_t mouth_y = FACE_MOUTH_BASE_Y + static_cast<int32_t>(current_pose.current_eye_y * 0.20f) + shake_y;
    renderSynthesisReticleMouth(canvas, mouth_x, mouth_y, now_ms);
}

void PetController::renderTopCyberSystemsHud(M5Canvas& canvas, const GazeTargetData& gaze_data, uint32_t now_ms) {
    // Painel superior tático dividido em 5 células sem qualquer sobreposição (Y = 0..27)
    uint16_t primary_col = getActiveOpticPrimaryColor();
    canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_HUD_PANEL_BG);
    canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastHLine(0, 29, 88, primary_col);
    canvas.drawFastHLine(SCREEN_WIDTH - 88, 29, 88, primary_col);

    // Divisores verticais sutis entre os módulos do HUD superior
    canvas.drawFastVLine(88,  4, 20, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastVLine(142, 4, 20, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastVLine(196, 4, 20, COLOR_CYBER_GRID_BLUE);

    // 1. Célula 1 (X = 4..84): Badge de Estado Dinâmico (Azul-aço escuro em operação, Vermelho só em alerta)
    constexpr int32_t bx = 4;
    constexpr int32_t by = 4;
    constexpr int32_t bw = 80;
    constexpr int32_t bh = 20;
    constexpr int32_t cut = 4;

    bool is_alert = (current_emotion == EMOTION_GRUMPY || current_emotion == EMOTION_STARTLED || current_emotion == EMOTION_DIZZY);
    uint16_t badge_fill = is_alert ? COLOR_WARRIOR_RED : COLOR_HUD_BADGE_BG;
    uint16_t badge_border = is_alert ? COLOR_CYBER_ORANGE : primary_col;

    for (int32_t dy = 0; dy < bh; ++dy) {
        int32_t x_start = bx + ((dy < cut) ? (cut - dy) : 0);
        int32_t x_end   = (bx + bw - 1) - ((dy >= bh - cut) ? (dy - (bh - cut) + 1) : 0);
        canvas.drawFastHLine(x_start, by + dy, x_end - x_start + 1, badge_fill);
    }
    canvas.drawLine(bx + cut, by, bx + bw - 1, by, badge_border);
    canvas.drawLine(bx, by + cut, bx + cut, by, badge_border);
    canvas.drawLine(bx, by + cut, bx, by + bh - 1, badge_border);
    canvas.drawLine(bx, by + bh - 1, bx + bw - cut - 1, by + bh - 1, badge_border);
    canvas.drawLine(bx + bw - cut - 1, by + bh - 1, bx + bw - 1, by + bh - cut - 1, badge_border);
    canvas.drawLine(bx + bw - 1, by, bx + bw - 1, by + bh - cut - 1, badge_border);

    const char* emotion_label = getEmotionLabel();
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    int32_t label_w = static_cast<int32_t>(std::strlen(emotion_label)) * 6;
    int32_t label_x = bx + ((bw - label_w) / 2);
    if (label_x < bx + 3) label_x = bx + 3;

    canvas.setTextColor(COLOR_TEXT_SHADOW);
    canvas.setCursor(label_x + 1, by + 7);
    canvas.print(emotion_label);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(label_x, by + 6);
    canvas.print(emotion_label);

    // 2. Célula 2 (X = 91..139): Ícone 16-bit CPU Core (SD) + Indicador de Energia (5 blocos)
    constexpr int32_t cpu_x = 92;
    constexpr int32_t cpu_y = 4;
    if (sd_gfx_ready) {
        canvas.pushImage(cpu_x, cpu_y - 2, HUD_ICON_SIZE, HUD_ICON_SIZE, hud_icons_rgb565[0], COLOR_KEY_TRANSPARENT);
        canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
        canvas.setCursor(cpu_x + 14, cpu_y);
        canvas.print("CORE");
    } else {
        canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
        canvas.setCursor(cpu_x, cpu_y);
        canvas.print("CPU CORE");
    }

    int32_t energy_clamped = clampStatValue(internal_stats.energy_level, 0, 100);
    int32_t lit_segments = (energy_clamped * 5 + 50) / 100;
    for (int32_t seg = 0; seg < 5; ++seg) {
        int32_t sx = cpu_x + (seg * 9);
        int32_t sy = cpu_y + 11;
        uint16_t seg_col = COLOR_HUD_BADGE_BG;
        if (seg < lit_segments) {
            seg_col = (lit_segments >= 2) ? primary_col : COLOR_OPTIC_NEON_RED;
        }
        canvas.fillRect(sx, sy, 7, 6, seg_col);
        canvas.drawRect(sx, sy, 7, 6, COLOR_CYBER_GRID_BLUE);
    }

    // 3. Célula 3 (X = 146..193): Ícone 16-bit de Mira/Foco (SD) + Tempo de Foco / Pomodoro
    constexpr int32_t foc_x = 154;
    constexpr int32_t foc_y = 14;
    if (sd_gfx_ready && !pomodoro_active) {
        canvas.pushImage(foc_x - 6, foc_y - 6, HUD_ICON_SIZE, HUD_ICON_SIZE, hud_icons_rgb565[1], COLOR_KEY_TRANSPARENT);
    } else {
        uint16_t foc_col = pomodoro_active ? COLOR_CYBER_ORANGE : primary_col;
        canvas.drawCircle(foc_x, foc_y, 5, foc_col);
        canvas.drawFastHLine(foc_x - 6, foc_y, 13, foc_col);
        canvas.drawFastVLine(foc_x, foc_y - 6, 13, foc_col);
        canvas.drawPixel(foc_x, foc_y, TFT_WHITE);
    }

    canvas.setTextColor(pomodoro_active ? COLOR_CYBER_ORANGE : COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(foc_x + 8, foc_y - 4);
    if (pomodoro_active && pomodoro_end_ms > now_ms) {
        uint32_t rem_sec = (pomodoro_end_ms - now_ms) / 1000U;
        uint32_t mm = rem_sec / 60U;
        uint32_t ss = rem_sec % 60U;
        canvas.printf("%02u:%02u", static_cast<unsigned int>(mm), static_cast<unsigned int>(ss));
    } else {
        canvas.printf("%um", static_cast<unsigned int>(internal_stats.focus_minutes));
    }

    // 4. Célula 4 (X = 200..288): Telemetria de Alvo & Humor (formatada para terminar antes de X = 290)
    canvas.setTextColor(gaze_data.face_detected ? COLOR_CYBER_ICE_LIGHT : COLOR_CYBER_ORANGE);
    canvas.setCursor(200, 5);
    canvas.print(gaze_data.face_detected ? "ALVO: TRAVADO" : "ALVO: BUSCA..");
    canvas.setTextColor(primary_col);
    canvas.setCursor(200, 15);
    canvas.printf("RST:%s HMR:%d%%", gaze_data.face_detected ? "S" : "N", static_cast<int>(internal_stats.mood_score));

    // 5. Célula 5 (X = 294..314): Radar "Sonar" Ativo no canto superior direito com folga limpa
    constexpr int32_t rad_x = 305;
    constexpr int32_t rad_y = 14;
    constexpr int32_t rad_r = 10;

    canvas.fillCircle(rad_x, rad_y, rad_r, COLOR_HUD_INNER_BG);
    canvas.drawCircle(rad_x, rad_y, rad_r, primary_col);
    canvas.drawCircle(rad_x, rad_y, rad_r / 2, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastHLine(rad_x - rad_r, rad_y, rad_r * 2 + 1, COLOR_CYBER_GRID_BLUE);
    canvas.drawFastVLine(rad_x, rad_y - rad_r, rad_r * 2 + 1, COLOR_CYBER_GRID_BLUE);

    float sweep_ang = static_cast<float>(now_ms) * 0.0045f;
    int32_t sw_x = rad_x + static_cast<int32_t>(std::round(std::cos(sweep_ang) * static_cast<float>(rad_r - 1)));
    int32_t sw_y = rad_y + static_cast<int32_t>(std::round(std::sin(sweep_ang) * static_cast<float>(rad_r - 1)));
    canvas.drawLine(rad_x, rad_y, sw_x, sw_y, COLOR_CYBER_ICE_LIGHT);

    if (gaze_data.face_detected) {
        int32_t blip_x = rad_x + static_cast<int32_t>(gaze_data.target_x * 6.0f);
        int32_t blip_y = rad_y + static_cast<int32_t>(gaze_data.target_y * 6.0f);
        canvas.fillRect(blip_x - 1, blip_y - 1, 3, 3, TFT_WHITE);
    }
}

void PetController::renderSideAudioVuMeters(M5Canvas& canvas, const AudioSensorData& audio_data, uint32_t now_ms) {
    // VU Meters de Áudio Laterais: colunas de 11 blocos de LEDs virtuais empilhados nas margens esquerda e direita
    constexpr int32_t num_leds = 11;
    constexpr int32_t led_w = 7;
    constexpr int32_t led_h = 6;
    constexpr int32_t led_pitch = 9;
    constexpr int32_t bottom_y = 136;
    constexpr int32_t left_x = 4;
    constexpr int32_t right_x = SCREEN_WIDTH - 11;

    uint16_t primary_col = getActiveOpticPrimaryColor();

    // Normaliza nível de dB do microfone (em silêncio ~30dB acende apenas os 2 LEDs da base)
    float db_norm = (audio_data.noise_level_db - 28.0f) / 62.0f;
    if (db_norm < 0.0f) db_norm = 0.0f;
    if (db_norm > 1.0f) db_norm = 1.0f;

    // Se o bicho estiver falando, combina com a energia RMS da própria voz para movimentar as barras
    if (lip_sync_state.is_speaking) {
        float voice_norm = (current_pose.mouth_open_height - MOUTH_CLOSED_HEIGHT) /
                           (MOUTH_OPEN_MAX_HEIGHT - MOUTH_CLOSED_HEIGHT);
        if (voice_norm > db_norm) {
            db_norm = voice_norm;
        }
    }

    int32_t lit_count = 2 + static_cast<int32_t>(std::round(db_norm * static_cast<float>(num_leds - 2)));
    if (lit_count > num_leds) lit_count = num_leds;

    // Moldura vertical das torres de VU Meter
    int32_t top_y = bottom_y - ((num_leds - 1) * led_pitch) - 3;
    canvas.drawRect(left_x - 2, top_y, led_w + 4, (num_leds * led_pitch) + 4, COLOR_CYBER_GRID_BLUE);
    canvas.drawRect(right_x - 2, top_y, led_w + 4, (num_leds * led_pitch) + 4, COLOR_CYBER_GRID_BLUE);

    for (int32_t i = 0; i < num_leds; ++i) {
        int32_t ly = bottom_y - (i * led_pitch);
        uint16_t active_col = primary_col;        // Base (0..5): Cor primária do tema
        if (i >= 9) {
            active_col = COLOR_OPTIC_NEON_RED;    // Topo (9..10): Vermelho de pico acústico
        } else if (i >= 6) {
            active_col = COLOR_CYBER_ICE_LIGHT;   // Meio-alto (6..8): Azul-gelo brilhante
        }

        // Leve variação entre canal esquerdo e direito em picos altos
        int32_t right_lit = lit_count;
        if (lit_count > 3 && ((now_ms / 90U) % 2U == 1U)) {
            right_lit = (lit_count > 2) ? (lit_count - 1) : lit_count;
        }

        uint16_t col_l = (i < lit_count) ? active_col : COLOR_HUD_BADGE_BG;
        uint16_t col_r = (i < right_lit) ? active_col : COLOR_HUD_BADGE_BG;

        canvas.fillRect(left_x, ly, led_w, led_h, col_l);
        canvas.fillRect(right_x, ly, led_w, led_h, col_r);
    }
}

// =============================================================================
// Auxiliares Internos de Tipografia UTF-8 com Acentuação em Português (12px)
// =============================================================================
namespace {

enum PortugueseDiacritic : uint8_t {
    DIACRITIC_NONE = 0,
    DIACRITIC_ACUTE,
    DIACRITIC_GRAVE,
    DIACRITIC_CIRCUMFLEX,
    DIACRITIC_TILDE,
    DIACRITIC_CEDILLA,
    DIACRITIC_DEGREE
};

struct DecodedPtGlyph {
    char base_ascii;
    PortugueseDiacritic diacritic;
    bool is_uppercase;
    size_t byte_len;
};

static DecodedPtGlyph mapLatin1CodepointToGlyph(uint8_t b1, size_t byte_len) {
    switch (b1) {
        // Símbolos de grau e ordinais (0xA0..0xBF em Latin-1 / 0xC2 0xXX em UTF-8)
        case 0xA1U: return {'!', DIACRITIC_NONE,       false, byte_len}; // ¡
        case 0xAAU: return {'a', DIACRITIC_NONE,       false, byte_len}; // ª
        case 0xB0U: return {' ', DIACRITIC_DEGREE,     true,  byte_len}; // °
        case 0xBAU: return {' ', DIACRITIC_DEGREE,     true,  byte_len}; // º
        case 0xBFU: return {'?', DIACRITIC_NONE,       false, byte_len}; // ¿
        // Maiúsculas acentuadas (0xC0..0xDF em Latin-1)
        case 0xC0U: return {'A', DIACRITIC_GRAVE,      true,  byte_len}; // À
        case 0xC1U: return {'A', DIACRITIC_ACUTE,      true,  byte_len}; // Á
        case 0xC2U: return {'A', DIACRITIC_CIRCUMFLEX, true,  byte_len}; // Â
        case 0xC3U: return {'A', DIACRITIC_TILDE,      true,  byte_len}; // Ã
        case 0xC7U: return {'C', DIACRITIC_CEDILLA,    true,  byte_len}; // Ç
        case 0xC8U: return {'E', DIACRITIC_GRAVE,      true,  byte_len}; // È
        case 0xC9U: return {'E', DIACRITIC_ACUTE,      true,  byte_len}; // É
        case 0xCAU: return {'E', DIACRITIC_CIRCUMFLEX, true,  byte_len}; // Ê
        case 0xCCU: return {'I', DIACRITIC_GRAVE,      true,  byte_len}; // Ì
        case 0xCDU: return {'I', DIACRITIC_ACUTE,      true,  byte_len}; // Í
        case 0xCEU: return {'I', DIACRITIC_CIRCUMFLEX, true,  byte_len}; // Î
        case 0xD1U: return {'N', DIACRITIC_TILDE,      true,  byte_len}; // Ñ
        case 0xD2U: return {'O', DIACRITIC_GRAVE,      true,  byte_len}; // Ò
        case 0xD3U: return {'O', DIACRITIC_ACUTE,      true,  byte_len}; // Ó
        case 0xD4U: return {'O', DIACRITIC_CIRCUMFLEX, true,  byte_len}; // Ô
        case 0xD5U: return {'O', DIACRITIC_TILDE,      true,  byte_len}; // Õ
        case 0xD9U: return {'U', DIACRITIC_GRAVE,      true,  byte_len}; // Ù
        case 0xDAU: return {'U', DIACRITIC_ACUTE,      true,  byte_len}; // Ú
        case 0xDBU: return {'U', DIACRITIC_CIRCUMFLEX, true,  byte_len}; // Û
        case 0xDCU: return {'U', DIACRITIC_NONE,       true,  byte_len}; // Ü
        // Minúsculas acentuadas (0xE0..0xFF em Latin-1)
        case 0xE0U: return {'a', DIACRITIC_GRAVE,      false, byte_len}; // à
        case 0xE1U: return {'a', DIACRITIC_ACUTE,      false, byte_len}; // á
        case 0xE2U: return {'a', DIACRITIC_CIRCUMFLEX, false, byte_len}; // â
        case 0xE3U: return {'a', DIACRITIC_TILDE,      false, byte_len}; // ã
        case 0xE7U: return {'c', DIACRITIC_CEDILLA,    false, byte_len}; // ç
        case 0xE8U: return {'e', DIACRITIC_GRAVE,      false, byte_len}; // è
        case 0xE9U: return {'e', DIACRITIC_ACUTE,      false, byte_len}; // é
        case 0xEAU: return {'e', DIACRITIC_CIRCUMFLEX, false, byte_len}; // ê
        case 0xECU: return {'i', DIACRITIC_GRAVE,      false, byte_len}; // ì
        case 0xEDU: return {'i', DIACRITIC_ACUTE,      false, byte_len}; // í
        case 0xEEU: return {'i', DIACRITIC_CIRCUMFLEX, false, byte_len}; // î
        case 0xF1U: return {'n', DIACRITIC_TILDE,      false, byte_len}; // ñ
        case 0xF2U: return {'o', DIACRITIC_GRAVE,      false, byte_len}; // ò
        case 0xF3U: return {'o', DIACRITIC_ACUTE,      false, byte_len}; // ó
        case 0xF4U: return {'o', DIACRITIC_CIRCUMFLEX, false, byte_len}; // ô
        case 0xF5U: return {'o', DIACRITIC_TILDE,      false, byte_len}; // õ
        case 0xF9U: return {'u', DIACRITIC_GRAVE,      false, byte_len}; // ù
        case 0xFAU: return {'u', DIACRITIC_ACUTE,      false, byte_len}; // ú
        case 0xFBU: return {'u', DIACRITIC_CIRCUMFLEX, false, byte_len}; // û
        case 0xFCU: return {'u', DIACRITIC_NONE,       false, byte_len}; // ü
        default:    return {' ', DIACRITIC_NONE,       false, byte_len};
    }
}

static DecodedPtGlyph decodeNextPtUtf8Glyph(const char* text_ptr) {
    DecodedPtGlyph glyph = {'?', DIACRITIC_NONE, false, 1};
    uint8_t b0 = static_cast<uint8_t>(text_ptr[0]);

    // Fallback de segurança caso algum escape literal \uXXXX chegue na string
    if (b0 == '\\' && text_ptr[1] == 'u') {
        uint32_t cp = 0;
        bool valid_hex = true;
        for (int k = 0; k < 4; ++k) {
            char hc = text_ptr[2 + k];
            cp <<= 4;
            if (hc >= '0' && hc <= '9') cp |= static_cast<uint32_t>(hc - '0');
            else if (hc >= 'a' && hc <= 'f') cp |= static_cast<uint32_t>(10 + (hc - 'a'));
            else if (hc >= 'A' && hc <= 'F') cp |= static_cast<uint32_t>(10 + (hc - 'A'));
            else { valid_hex = false; break; }
        }
        if (valid_hex) {
            if (cp <= 0xFFU) {
                return mapLatin1CodepointToGlyph(static_cast<uint8_t>(cp), 6);
            }
            return {'-', DIACRITIC_NONE, false, 6};
        }
    }

    if (b0 < 0x80U) {
        glyph.base_ascii = static_cast<char>(b0);
        glyph.is_uppercase = (b0 >= 'A' && b0 <= 'Z');
        glyph.byte_len = 1;
        return glyph;
    }

    if ((b0 & 0xE0U) == 0xC0U && text_ptr[1] != '\0') {
        uint8_t b1 = static_cast<uint8_t>(text_ptr[1]);
        if (b0 == 0xC2U) {
            return mapLatin1CodepointToGlyph(b1, 2);
        }
        if (b0 == 0xC3U) {
            return mapLatin1CodepointToGlyph(static_cast<uint8_t>(b1 + 0x40U), 2);
        }
        glyph.base_ascii = ' ';
        glyph.byte_len = 2;
        return glyph;
    }

    if ((b0 & 0xF0U) == 0xE0U) {
        // Pontuação tipográfica UTF-8 de 3 bytes (0xE2 0x80 0xXX: travessão, aspas curvas, reticências)
        if (b0 == 0xE2U && static_cast<uint8_t>(text_ptr[1]) == 0x80U && text_ptr[2] != '\0') {
            uint8_t b2 = static_cast<uint8_t>(text_ptr[2]);
            if (b2 == 0x93U || b2 == 0x94U) return {'-',  DIACRITIC_NONE, false, 3}; // – —
            if (b2 == 0x98U || b2 == 0x99U) return {'\'', DIACRITIC_NONE, false, 3}; // ‘ ’
            if (b2 == 0x9CU || b2 == 0x9DU) return {'\"', DIACRITIC_NONE, false, 3}; // “ ”
            if (b2 == 0xA6U)                return {'.',  DIACRITIC_NONE, false, 3}; // …
        }
        glyph.base_ascii = ' ';
        glyph.byte_len = 3;
    } else if ((b0 & 0xF8U) == 0xF0U) {
        glyph.base_ascii = ' ';
        glyph.byte_len = 4;
    }
    return glyph;
}

static int32_t measurePtUtf8WordWidth(const char* word_utf8) {
    int32_t char_count = 0;
    const char* p = word_utf8;
    while (*p != '\0') {
        DecodedPtGlyph g = decodeNextPtUtf8Glyph(p);
        ++char_count;
        p += g.byte_len;
    }
    return char_count * 6; // Cada glifo 6x12 px possui avanço exato de 6px
}

static void drawPtDiacriticPixels(M5Canvas& canvas, int32_t gx, int32_t gy, const DecodedPtGlyph& g, uint16_t col) {
    if (g.diacritic == DIACRITIC_NONE) {
        return;
    }

    int32_t top_y = g.is_uppercase ? (gy - 1) : (gy + 1);
    switch (g.diacritic) {
        case DIACRITIC_ACUTE:
            // Acento agudo (´)
            canvas.drawPixel(gx + 3, top_y,     col);
            canvas.drawPixel(gx + 4, top_y,     col);
            canvas.drawPixel(gx + 2, top_y + 1, col);
            break;

        case DIACRITIC_GRAVE:
            // Crase (`)
            canvas.drawPixel(gx + 1, top_y,     col);
            canvas.drawPixel(gx + 2, top_y,     col);
            canvas.drawPixel(gx + 3, top_y + 1, col);
            break;

        case DIACRITIC_CIRCUMFLEX:
            // Acento circunflexo (^)
            canvas.drawPixel(gx + 2, top_y,     col);
            canvas.drawPixel(gx + 3, top_y,     col);
            canvas.drawPixel(gx + 1, top_y + 1, col);
            canvas.drawPixel(gx + 4, top_y + 1, col);
            break;

        case DIACRITIC_TILDE:
            // Til (~) nítido de 4px
            canvas.drawPixel(gx + 1, top_y + 1, col);
            canvas.drawPixel(gx + 2, top_y,     col);
            canvas.drawPixel(gx + 3, top_y + 1, col);
            canvas.drawPixel(gx + 4, top_y,     col);
            break;

        case DIACRITIC_CEDILLA:
            // Cedilha (¸) abaixo da base do 'c'/'C'
            canvas.drawPixel(gx + 2, gy + 10, col);
            canvas.drawPixel(gx + 3, gy + 11, col);
            canvas.drawPixel(gx + 2, gy + 11, col);
            break;

        case DIACRITIC_DEGREE:
            // Símbolo de grau / ordinal (º / °) desenhado como anel 3x3 no topo da célula
            canvas.drawPixel(gx + 2, gy + 1, col);
            canvas.drawPixel(gx + 1, gy + 2, col);
            canvas.drawPixel(gx + 3, gy + 2, col);
            canvas.drawPixel(gx + 2, gy + 3, col);
            break;

        default:
            break;
    }
}

static void drawPtUtf8WordPass(M5Canvas& canvas, const char* word_utf8, int32_t start_x, int32_t start_y, uint16_t color) {
    canvas.setTextColor(color);
    int32_t cx = start_x;
    const char* p = word_utf8;
    char single_ascii[2] = {'\0', '\0'};

    while (*p != '\0') {
        DecodedPtGlyph g = decodeNextPtUtf8Glyph(p);
        // Para 'í' minúsculo, desenha haste do 'i' sem o pingo original para o acento agudo ficar limpo
        if (g.base_ascii == 'i' && g.diacritic == DIACRITIC_ACUTE) {
            canvas.drawFastHLine(cx + 1, start_y + 4, 2, color);
            canvas.drawFastVLine(cx + 2, start_y + 4, 6, color);
            canvas.drawFastHLine(cx + 1, start_y + 9, 3, color);
        } else if (g.base_ascii != ' ') {
            single_ascii[0] = g.base_ascii;
            canvas.setCursor(cx, start_y);
            canvas.print(single_ascii);
        }

        drawPtDiacriticPixels(canvas, cx, start_y, g, color);
        cx += 6;
        p += g.byte_len;
    }
}

static void drawPtUtf8WordWithShadow(
    M5Canvas& canvas,
    const char* word_utf8,
    int32_t x,
    int32_t y,
    uint16_t text_color,
    uint16_t shadow_color
) {
    drawPtUtf8WordPass(canvas, word_utf8, x + 1, y + 1, shadow_color);
    drawPtUtf8WordPass(canvas, word_utf8, x,     y,     text_color);
}

} // namespace

void PetController::renderWrappedShadowText(
    M5Canvas& canvas,
    const char* text_utf8,
    int32_t box_x,
    int32_t box_y,
    int32_t max_width,
    int32_t line_height,
    uint16_t text_color,
    uint16_t shadow_color,
    int32_t* out_end_x,
    int32_t* out_end_y
) {
    if (out_end_x != nullptr) *out_end_x = box_x;
    if (out_end_y != nullptr) *out_end_y = box_y;

    if (text_utf8 == nullptr || text_utf8[0] == '\0') {
        return;
    }

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextWrap(false, false);

    int32_t cursor_x = box_x;
    int32_t cursor_y = box_y;
    int32_t last_drawn_end_x = box_x;
    int32_t last_drawn_end_y = box_y;
    constexpr int32_t space_w = 6;
    const char* ptr = text_utf8;

    while (*ptr != '\0') {
        while (*ptr == ' ') {
            ++ptr;
        }
        if (*ptr == '\0') {
            break;
        }
        if (*ptr == '\n') {
            cursor_x = box_x;
            cursor_y += line_height;
            ++ptr;
            continue;
        }

        // Extrai a próxima palavra completa para nunca quebrar palavras no meio
        char word_buf[96];
        size_t w_idx = 0;
        while (ptr[w_idx] != '\0' && ptr[w_idx] != ' ' && ptr[w_idx] != '\n' && w_idx < sizeof(word_buf) - 1) {
            word_buf[w_idx] = ptr[w_idx];
            ++w_idx;
        }
        word_buf[w_idx] = '\0';
        ptr += w_idx;

        int32_t word_w = measurePtUtf8WordWidth(word_buf);

        // Se a palavra ultrapassar a largura útil da linha, salta antes de desenhar
        if (cursor_x > box_x && (cursor_x + word_w > box_x + max_width)) {
            cursor_x = box_x;
            cursor_y += line_height;
        }

        // Renderiza sombra projetada de 1px em preto e texto principal por cima com suporte total a acentos PT-BR
        drawPtUtf8WordWithShadow(canvas, word_buf, cursor_x, cursor_y, text_color, shadow_color);

        last_drawn_end_x = cursor_x + word_w;
        last_drawn_end_y = cursor_y;
        cursor_x += word_w + space_w;
    }

    if (out_end_x != nullptr) *out_end_x = last_drawn_end_x;
    if (out_end_y != nullptr) *out_end_y = last_drawn_end_y;
}

void PetController::renderTacticalDialogueOverlay(M5Canvas& canvas, const char* subtitle_text, uint32_t now_ms) {
    // Caixa de Diálogo de Sobreposição Tática na parte inferior (Y: 168 a 236, X: 6 a 314)
    constexpr int32_t box_x = 6;
    constexpr int32_t box_y = 168;
    constexpr int32_t box_w = 308;
    constexpr int32_t box_h = 68;
    constexpr int32_t pad_inner = 10; // Padding interno estrito de 10px em todas as margens da caixa

    uint16_t primary_col = getActiveOpticPrimaryColor();

    // 1. Fundo escuro tático azul-noite profundo (zero verde!) e bordas duplas harmônicas
    canvas.fillRect(box_x, box_y, box_w, box_h, COLOR_HUD_PANEL_BG);
    canvas.fillRect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, COLOR_HUD_INNER_BG);

    canvas.drawRect(box_x, box_y, box_w, box_h, primary_col);
    canvas.drawRect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, COLOR_CYBER_GRID_BLUE);

    // 2. Colchetes de canto em azul-gelo harmônico (em vez de amarelo conflitante)
    canvas.drawFastHLine(box_x - 2, box_y - 2, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(box_x - 2, box_y - 2, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastHLine(box_x + box_w - 10, box_y - 2, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(box_x + box_w + 1, box_y - 2, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastHLine(box_x - 2, box_y + box_h + 1, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(box_x - 2, box_y + box_h - 10, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastHLine(box_x + box_w - 10, box_y + box_h + 1, 12, COLOR_CYBER_ICE_LIGHT);
    canvas.drawFastVLine(box_x + box_w + 1, box_y + box_h - 10, 12, COLOR_CYBER_ICE_LIGHT);

    // Marcadores de mira nos pontos médios laterais da caixa
    int32_t mid_y = box_y + (box_h / 2);
    canvas.drawFastHLine(box_x - 1, mid_y, 6, primary_col);
    canvas.drawFastHLine(box_x + box_w - 5, mid_y, 6, primary_col);

    // 3. Aba de Identificação no topo esquerdo: "SISTEMA ORÁCULO:" ancorada na borda superior
    constexpr int32_t tab_x = box_x + pad_inner;
    constexpr int32_t tab_y = box_y - 9;
    constexpr int32_t tab_w = 114;
    constexpr int32_t tab_h = 15;
    bool tab_pulse = lip_sync_state.is_speaking && (((now_ms / 180U) % 2U) == 0U);
    uint16_t tab_bg = tab_pulse ? COLOR_CYBER_BLUE_DARK : COLOR_HUD_BADGE_BG;

    canvas.fillRect(tab_x, tab_y, tab_w, tab_h, tab_bg);
    canvas.drawRect(tab_x, tab_y, tab_w, tab_h, primary_col);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextWrap(false, false);
    drawPtUtf8WordWithShadow(canvas, "SISTEMA ORÁCULO:", tab_x + 5, tab_y + 1, TFT_WHITE, COLOR_TEXT_SHADOW);

    // 3.5. Aba de Canal de Transmissão no topo direito com ícone SD/Áudio 16-bit
    constexpr int32_t rtab_w = 118;
    constexpr int32_t rtab_h = 12;
    constexpr int32_t rtab_x = box_x + box_w - pad_inner - rtab_w;
    constexpr int32_t rtab_y = box_y - 7;
    bool waiting_ai = oracle_brain.isWaitingAi();
    bool is_muted_now = is_muted_indefinitely;
    canvas.fillRect(rtab_x, rtab_y, rtab_w, rtab_h, COLOR_HUD_BADGE_BG);
    canvas.drawRect(rtab_x, rtab_y, rtab_w, rtab_h, (waiting_ai || is_muted_now) ? COLOR_CYBER_ORANGE : primary_col);

    if (sd_gfx_ready) {
        uint8_t icon_idx = lip_sync_state.is_speaking ? 3U : 2U;
        canvas.pushImage(rtab_x + 2, rtab_y, HUD_ICON_SIZE, HUD_ICON_SIZE, hud_icons_rgb565[icon_idx], COLOR_KEY_TRANSPARENT);
    }

    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor((waiting_ai || is_muted_now) ? COLOR_CYBER_ORANGE : COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(rtab_x + (sd_gfx_ready ? 15 : 3), rtab_y + 2);
    if (waiting_ai) {
        canvas.print("[IA://GEMINI_2.5]");
    } else if (is_muted_now) {
        canvas.print("[MUDO://SHHH_ON]");
    } else if (lip_sync_state.is_speaking) {
        canvas.print("[TX://AUDIO_24K]");
    } else if (cached_audio_data.vad_enabled && cached_audio_data.vad_keyword_mode) {
        canvas.print("[VAD://BICHO_ON]");
    } else {
        canvas.print("[SD://CACHE_64G]");
    }

    // 4. Word-Wrap & Layout com área interna 100% livre para até 3 linhas de legenda
    constexpr int32_t text_x = box_x + pad_inner;
    constexpr int32_t text_y = box_y + 9;
    constexpr int32_t max_text_width = box_w - (pad_inner * 2); // 288px de largura útil

    int32_t end_x = text_x;
    int32_t end_y = text_y;
    renderWrappedShadowText(
        canvas,
        subtitle_text,
        text_x,
        text_y,
        max_text_width,
        13,
        TFT_WHITE,
        COLOR_TEXT_SHADOW,
        &end_x,
        &end_y
    );

    // 5. Cursor de dados quadrado piscando no final da frase atual (█)
    bool show_cursor = lip_sync_state.is_speaking || (((now_ms / 320U) % 2U) == 0U);
    if (show_cursor) {
        int32_t cur_x = end_x + 2;
        int32_t cur_y = end_y + 1;
        if (cur_x + 6 > box_x + box_w - pad_inner) {
            cur_x = text_x;
            cur_y += 13;
        }
        if (cur_y + 10 <= box_y + box_h - 4) {
            uint16_t cursor_col = lip_sync_state.is_speaking ? COLOR_CYBER_ICE_LIGHT : primary_col;
            canvas.fillRect(cur_x, cur_y, 6, 10, cursor_col);
        }
    }
}

void PetController::applyVideoGlitchEffect(M5Canvas& canvas, uint32_t now_ms) {
    if (glitch_frames_remaining == 0 || active_glitch_band_count == 0) {
        return;
    }
    --glitch_frames_remaining;

    // 1. Desloca 1 a 3 faixas horizontais de pixel para a esquerda ou direita usando readRect / pushImage
    uint16_t line_buf[SCREEN_WIDTH];
    for (uint8_t b = 0; b < active_glitch_band_count; ++b) {
        int32_t by = glitch_bands[b].band_y;
        int32_t bh = glitch_bands[b].band_h;
        int32_t shift_x = glitch_bands[b].shift_x;

        for (int32_t row = 0; row < bh; ++row) {
            int32_t y = by + row;
            if (y < 0 || y >= SCREEN_HEIGHT) continue;

            canvas.readRect(0, y, SCREEN_WIDTH, 1, line_buf);
            if (shift_x > 0 && shift_x < SCREEN_WIDTH) {
                canvas.pushImage(shift_x, y, SCREEN_WIDTH - shift_x, 1, line_buf);
                canvas.drawFastHLine(0, y, shift_x, COLOR_OPTIC_NEON_RED);
            } else if (shift_x < 0 && (-shift_x) < SCREEN_WIDTH) {
                int32_t abs_s = -shift_x;
                canvas.pushImage(0, y, SCREEN_WIDTH - abs_s, 1, line_buf + abs_s);
                canvas.drawFastHLine(SCREEN_WIDTH - abs_s, y, abs_s, COLOR_CYBER_BLUE_NEON);
            }
        }
    }

    // 2. Desenha ranhuras e chuviscos rápidos (linhas de 1px com canais RGB desalinhados)
    for (uint32_t s = 0; s < 7U; ++s) {
        uint32_t r_seed = now_ms * 31U + (s * 97U);
        int32_t sy = static_cast<int32_t>(r_seed % SCREEN_HEIGHT);
        int32_t sx = static_cast<int32_t>((r_seed / 7U) % (SCREEN_WIDTH - 40));
        int32_t slen = 18 + static_cast<int32_t>((r_seed / 13U) % 64);

        // Desalinhamento cromático RGB (vermelho puro + ciano puro + branco)
        canvas.drawFastHLine(sx, sy, slen, 0xF800);
        if (sy + 1 < SCREEN_HEIGHT) {
            canvas.drawFastHLine(sx + 3, sy + 1, slen, 0x07FF);
        }
        if ((s & 1U) == 0U) {
            canvas.drawFastHLine(sx + 6, sy, slen / 3, TFT_WHITE);
        }
    }
}

void PetController::renderCyberPipVisor(M5Canvas& canvas, const GazeTargetData& gaze_data, uint32_t now_ms) {
    // Suporta 2 tamanhos:
    // - Modo Doca Padrão (64x48 no quadrante superior direito: X = 234, Y = 44)
    // - Modo Zoom Óptico HD [Z] (160x120 centralizado: X = 80, Y = 40)
    // E 3 Modos Ópticos [X] (ou toque no cabeçalho do PIP):
    // - 0: Ciano Tático (Scanline)
    // - 1: Cor Real 16-bit RGB565
    // - 2: Termográfico Infravermelho
    int32_t pip_w = pip_hd_zoom_active ? PIP_HD_FRAME_WIDTH  : PIP_FRAME_WIDTH;
    int32_t pip_h = pip_hd_zoom_active ? PIP_HD_FRAME_HEIGHT : PIP_FRAME_HEIGHT;
    int32_t pip_x = pip_hd_zoom_active ? 80  : 234;
    int32_t pip_y = pip_hd_zoom_active ? 40  : 44;

    uint16_t primary_col = getActiveOpticPrimaryColor();
    bool has_intruder = (gaze_data.face_count >= 2U);
    bool has_obj_close = gaze_data.object_held_close;
    bool has_hand_gesture = (gaze_data.gesture_shhh || gaze_data.gesture_thumbs_up ||
                             gaze_data.gesture_victory || gaze_data.gesture_open_palm);

    // Moldura tática do visor Picture-in-Picture (PIP / HD Zoom)
    uint16_t border_col = has_hand_gesture ? COLOR_SONAR_GREEN : ((has_intruder || has_obj_close) ? COLOR_CYBER_ORANGE : primary_col);
    canvas.fillRect(pip_x - 2, pip_y - 10, pip_w + 4, pip_h + 12, COLOR_HUD_PANEL_BG);
    canvas.drawRect(pip_x - 2, pip_y - 10, pip_w + 4, pip_h + 12, border_col);
    uint16_t hdr_bg = timelapse_active ? COLOR_CYBER_ORANGE : (has_intruder ? COLOR_WARRIOR_RED : COLOR_HUD_BADGE_BG);
    canvas.fillRect(pip_x - 1, pip_y - 9, pip_w + 2, 8, hdr_bg);

    static const char* const OPTIC_TAGS[3] = {"CIANO", "RGB16", "TERMO"};
    const char* mode_tag = OPTIC_TAGS[pip_optic_mode % 3U];

    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(pip_x + 2, pip_y - 8);
    if (gaze_data.gesture_shhh) {
        canvas.print("SHHH:MUDO!");
    } else if (gaze_data.gesture_thumbs_up) {
        canvas.print("GESTO:JOINHA");
    } else if (gaze_data.gesture_victory) {
        canvas.print("SELFIE:PAZ!");
    } else if (gaze_data.gesture_open_palm) {
        canvas.print("GESTO:PALMA");
    } else if (pip_hd_zoom_active) {
        canvas.printf("HD 160x120 [%s][X]", mode_tag);
    } else if (timelapse_active) {
        canvas.print("SD//TIMELAP");
    } else if (has_obj_close) {
        canvas.print("SCAN:OBJETO");
    } else if (has_intruder) {
        canvas.printf("2 ROSTOS! #%u", static_cast<unsigned int>(gaze_data.face_count));
    } else if (gaze_data.is_yawning) {
        canvas.print("FADIGA/BOCJ");
    } else if (gaze_data.is_slouching || gaze_data.is_too_close) {
        canvas.print("PIP:POSTURA");
    } else {
        canvas.printf("CAM//%s", mode_tag);
    }

    bool has_pip_data = timelapse_active ? (!timelapse_files.empty()) : cached_pip_valid;

    if (has_pip_data) {
        uint16_t row_rgb565[PIP_HD_FRAME_WIDTH];
        for (int32_t py = 0; py < pip_h; ++py) {
            bool is_scanline = ((py & 1) == 1);
            for (int32_t px = 0; px < pip_w; ++px) {
                // Modo 1 (Cor Real 16-bit RGB565) quando não está em timelapse
                if (pip_optic_mode == 1U && !timelapse_active) {
                    uint16_t raw_col = pip_hd_zoom_active
                        ? cached_pip_hd_rgb565_buffer[py * PIP_HD_FRAME_WIDTH + px]
                        : cached_pip_rgb565_buffer[py * PIP_FRAME_WIDTH + px];
                    if (raw_col != 0U) {
                        if (is_scanline) {
                            // Atenua levemente 12% nos scanlines ímpares mantendo fidelidade de cor
                            uint16_t r5 = (raw_col >> 11) & 0x1FU;
                            uint16_t g6 = (raw_col >> 5)  & 0x3FU;
                            uint16_t b5 = raw_col & 0x1FU;
                            r5 = (r5 * 7U) / 8U;
                            g6 = (g6 * 7U) / 8U;
                            b5 = (b5 * 7U) / 8U;
                            raw_col = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
                        }
                        row_rgb565[px] = raw_col;
                        continue;
                    }
                }

                uint8_t lum = 0;
                if (pip_hd_zoom_active && !timelapse_active) {
                    lum = cached_pip_hd_buffer[py * PIP_HD_FRAME_WIDTH + px];
                } else if (pip_hd_zoom_active && timelapse_active) {
                    // Interpolação bilinear de 64x48 -> 160x120 para o Timelapse SD
                    float gx = (static_cast<float>(px) * static_cast<float>(PIP_FRAME_WIDTH - 1)) /
                               static_cast<float>(PIP_HD_FRAME_WIDTH - 1);
                    float gy = (static_cast<float>(py) * static_cast<float>(PIP_FRAME_HEIGHT - 1)) /
                               static_cast<float>(PIP_HD_FRAME_HEIGHT - 1);
                    int32_t x0 = static_cast<int32_t>(gx);
                    int32_t y0 = static_cast<int32_t>(gy);
                    int32_t x1 = (x0 + 1 < PIP_FRAME_WIDTH)  ? (x0 + 1) : x0;
                    int32_t y1 = (y0 + 1 < PIP_FRAME_HEIGHT) ? (y0 + 1) : y0;
                    float fx = gx - static_cast<float>(x0);
                    float fy = gy - static_cast<float>(y0);
                    float v00 = static_cast<float>(timelapse_pip_buffer[y0 * PIP_FRAME_WIDTH + x0]);
                    float v10 = static_cast<float>(timelapse_pip_buffer[y0 * PIP_FRAME_WIDTH + x1]);
                    float v01 = static_cast<float>(timelapse_pip_buffer[y1 * PIP_FRAME_WIDTH + x0]);
                    float v11 = static_cast<float>(timelapse_pip_buffer[y1 * PIP_FRAME_WIDTH + x1]);
                    float top = v00 + fx * (v10 - v00);
                    float bot = v01 + fx * (v11 - v01);
                    lum = static_cast<uint8_t>(std::round(top + fy * (bot - top)));
                } else {
                    const uint8_t* src_64 = timelapse_active ? timelapse_pip_buffer : cached_pip_buffer;
                    lum = src_64[py * PIP_FRAME_WIDTH + px];
                }

                if (is_scanline) {
                    lum = static_cast<uint8_t>((static_cast<uint16_t>(lum) * 7U) / 8U);
                }

                uint16_t r5 = 0;
                uint16_t g6 = 0;
                uint16_t b5 = 0;

                if (pip_optic_mode == 2U) {
                    // Modo 2: Termográfico Infravermelho (Azul Marinho -> Roxo -> Vermelho -> Laranja -> Amarelo -> Branco)
                    if (lum < 64U) {
                        r5 = static_cast<uint16_t>((lum * 12U) / 64U);
                        g6 = 0U;
                        b5 = static_cast<uint16_t>(6U + (lum * 22U) / 64U);
                    } else if (lum < 128U) {
                        uint8_t m = lum - 64U;
                        r5 = static_cast<uint16_t>(12U + (m * 19U) / 64U);
                        g6 = static_cast<uint16_t>((m * 14U) / 64U);
                        b5 = static_cast<uint16_t>(28U - (m * 24U) / 64U);
                    } else if (lum < 192U) {
                        uint8_t m = lum - 128U;
                        r5 = 31U;
                        g6 = static_cast<uint16_t>(14U + (m * 42U) / 64U);
                        b5 = static_cast<uint16_t>((m * 6U) / 64U);
                    } else {
                        uint8_t m = lum - 192U;
                        r5 = 31U;
                        g6 = static_cast<uint16_t>(56U + (m * 7U) / 63U);
                        b5 = static_cast<uint16_t>(6U + (m * 25U) / 63U);
                    }
                } else {
                    // Modo 0 (ou fallback): Ciano Tático (Azul profundo -> Ciano Óptico -> Branco Gelo)
                    if (lum < 96U) {
                        r5 = static_cast<uint16_t>((lum * 3U) / 96U);
                        g6 = static_cast<uint16_t>((lum * 34U) / 96U);
                        b5 = static_cast<uint16_t>(5U + (lum * 25U) / 96U);
                    } else if (lum < 190U) {
                        uint8_t mid = lum - 96U;
                        r5 = static_cast<uint16_t>(3U + (mid * 16U) / 94U);
                        g6 = static_cast<uint16_t>(34U + (mid * 26U) / 94U);
                        b5 = 30U;
                    } else {
                        uint8_t hi = lum - 190U;
                        r5 = static_cast<uint16_t>(19U + (hi * 12U) / 65U);
                        g6 = 63U;
                        b5 = 31U;
                    }
                }
                row_rgb565[px] = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
            }
            canvas.pushImage(pip_x, pip_y + py, pip_w, 1, row_rgb565);
        }
    } else {
        // Fallback sintético caso o buffer raw ainda não esteja disponível
        canvas.fillRect(pip_x, pip_y, pip_w, pip_h, COLOR_HUD_INNER_BG);
        for (int32_t gx = 8; gx < pip_w; gx += 16) {
            canvas.drawFastVLine(pip_x + gx, pip_y, pip_h, COLOR_CYBER_GRID_BLUE);
        }
        for (int32_t gy = 8; gy < pip_h; gy += 12) {
            canvas.drawFastHLine(pip_x, pip_y + gy, pip_w, COLOR_CYBER_GRID_BLUE);
        }
    }

    // Retículo Primário de travamento facial (Target Lock #1) sobreposto à imagem PIP / HD
    if (gaze_data.face_detected) {
        int32_t tx = pip_x + static_cast<int32_t>((gaze_data.target_x + 1.0f) * 0.5f * static_cast<float>(pip_w - 1));
        int32_t ty = pip_y + static_cast<int32_t>((gaze_data.target_y + 1.0f) * 0.5f * static_cast<float>(pip_h - 1));
        int32_t ret_r = pip_hd_zoom_active ? 12 : 6;
        if (tx < pip_x + ret_r) tx = pip_x + ret_r;
        if (tx > pip_x + pip_w - ret_r - 1) tx = pip_x + pip_w - ret_r - 1;
        if (ty < pip_y + ret_r) ty = pip_y + ret_r;
        if (ty > pip_y + pip_h - ret_r - 1) ty = pip_y + pip_h - ret_r - 1;

        uint16_t lock_col = (gaze_data.is_slouching || gaze_data.is_too_close) ? COLOR_OPTIC_NEON_RED : COLOR_CYBER_ICE_LIGHT;
        canvas.drawRect(tx - ret_r, ty - ret_r, ret_r * 2 + 1, ret_r * 2 + 1, lock_col);
        canvas.drawFastHLine(tx - ret_r - 3, ty, 4, primary_col);
        canvas.drawFastHLine(tx + ret_r,     ty, 4, primary_col);
        canvas.drawFastVLine(tx, ty - ret_r - 3, 4, primary_col);
        canvas.drawFastVLine(tx, ty + ret_r,     4, primary_col);
    }

    // Retículo Secundário (ALVO #2 - Intruso / Pair Programming) piscando em laranja/vermelho!
    if (gaze_data.face_count >= 2U) {
        int32_t sx = pip_x + static_cast<int32_t>((gaze_data.secondary_face_x + 1.0f) * 0.5f * static_cast<float>(pip_w - 1));
        int32_t sy = pip_y + static_cast<int32_t>((gaze_data.secondary_face_y + 1.0f) * 0.5f * static_cast<float>(pip_h - 1));
        int32_t s_r = pip_hd_zoom_active ? 9 : 5;
        if (sx < pip_x + s_r) sx = pip_x + s_r;
        if (sx > pip_x + pip_w - s_r - 1) sx = pip_x + pip_w - s_r - 1;
        if (sy < pip_y + s_r) sy = pip_y + s_r;
        if (sy > pip_y + pip_h - s_r - 1) sy = pip_y + pip_h - s_r - 1;

        uint16_t sec_col = (((now_ms / 180U) % 2U) == 0U) ? COLOR_OPTIC_NEON_RED : COLOR_CYBER_ORANGE;
        canvas.drawRect(sx - s_r, sy - s_r, s_r * 2 + 1, s_r * 2 + 1, sec_col);
        if (pip_hd_zoom_active) {
            canvas.setTextColor(sec_col);
            canvas.setCursor(sx - 10, sy - s_r - 8);
            canvas.print("ALVO#2");
        }
    }

    // Rodapé compacto dentro do PIP HD com telemetria óptica (LUX, YAW e Calibração [C])
    if (pip_hd_zoom_active) {
        canvas.fillRect(pip_x, pip_y + pip_h - 9, pip_w, 9, COLOR_HUD_PANEL_BG);
        canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
        canvas.setCursor(pip_x + 3, pip_y + pip_h - 8);
        canvas.printf("YAW:%+.0f LUX:%u %s",
                      gaze_data.head_yaw_deg,
                      static_cast<unsigned int>(cached_env_data.ambient_lux),
                      gaze_data.posture_calibrated ? "CAL:OK" : "[C]CAL");
    }

    // Linha de varredura laser descendente no PIP
    int32_t laser_y = pip_y + static_cast<int32_t>((now_ms / 35U) % static_cast<uint32_t>(pip_h));
    canvas.drawFastHLine(pip_x, laser_y, pip_w, primary_col);
}

void PetController::renderRpgAndHabitsModal(M5Canvas& canvas, uint32_t now_ms) {
    (void)now_ms;
    uint16_t primary_col = getActiveOpticPrimaryColor();
    const UserHabitMatrix& hm = oracle_brain.getHabitMatrix();

    const int32_t mx = 16;
    const int32_t my = 30;
    const int32_t mw = SCREEN_WIDTH - 32; // 288 px
    const int32_t mh = 132;               // Y = 30..162

    // Painel tático sci-fi com fundo profundo e cantos chanfrados
    canvas.fillRect(mx, my, mw, mh, COLOR_HUD_INNER_BG);
    canvas.drawRect(mx, my, mw, mh, primary_col);
    canvas.drawRect(mx + 2, my + 2, mw - 4, mh - 4, COLOR_CYBER_GRID_BLUE);

    // Cabeçalho do Modal
    canvas.fillRect(mx + 3, my + 3, mw - 6, 15, COLOR_HUD_BADGE_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(mx + 8, my + 7);
    canvas.printf("DOSSIE NEURAL RPG // LV.%02u %s",
                  static_cast<unsigned int>(hm.cyber_level),
                  oracle_brain.getCyberRankTitle());

    canvas.setTextColor(COLOR_CYBER_ORANGE);
    canvas.setCursor(mx + mw - 54, my + 7);
    canvas.print("[TAB/X]");

    // Barra de Progresso de XP
    uint32_t xp_in_level = hm.xp_points % 250U;
    int32_t bar_x = mx + 8;
    int32_t bar_y = my + 22;
    int32_t bar_w = mw - 16;
    canvas.drawRect(bar_x, bar_y, bar_w, 8, COLOR_CYBER_GRID_BLUE);
    int32_t fill_w = static_cast<int32_t>((xp_in_level * static_cast<uint32_t>(bar_w - 2)) / 250U);
    if (fill_w > 0) {
        canvas.fillRect(bar_x + 1, bar_y + 1, fill_w, 6, COLOR_SONAR_GREEN);
    }
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(bar_x + 4, bar_y + 11);
    canvas.printf("XP TOTAL: %u pts (Prox Nivel: %u/250) | POMO: %u | LANCHES: %u",
                  static_cast<unsigned int>(hm.xp_points),
                  static_cast<unsigned int>(xp_in_level),
                  static_cast<unsigned int>(hm.total_pomodoros_completed),
                  static_cast<unsigned int>(hm.total_visual_feedings));

    // Gráfico de Barras 24h da Matriz de Hábitos (Foco por Hora do Dia)
    int32_t chart_x = mx + 10;
    int32_t chart_y = my + 46;
    int32_t chart_w = mw - 20;
    int32_t chart_h = 48;
    canvas.drawRect(chart_x, chart_y, chart_w, chart_h, COLOR_CYBER_GRID_BLUE);

    uint32_t max_min = 10U;
    for (int h = 0; h < 24; ++h) {
        if (hm.hourly_focus_minutes[h] > max_min) {
            max_min = hm.hourly_focus_minutes[h];
        }
    }

    int32_t col_step = (chart_w - 8) / 24; // ~11px por hora
    for (int h = 0; h < 24; ++h) {
        int32_t bx = chart_x + 4 + h * col_step;
        uint32_t val = hm.hourly_focus_minutes[h];
        int32_t bh = static_cast<int32_t>((val * static_cast<uint32_t>(chart_h - 14)) / max_min);
        if (val > 0 && bh < 2) bh = 2;
        if (bh > 0) {
            uint16_t bcol = (val >= 20U) ? COLOR_SONAR_GREEN : primary_col;
            canvas.fillRect(bx, chart_y + chart_h - 10 - bh, col_step - 2, bh, bcol);
        }
        if (h % 4 == 0) {
            canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
            canvas.setCursor(bx, chart_y + chart_h - 8);
            canvas.printf("%02d", h);
        }
    }

    // Rodapé com Resumo de Memória SD + Mundo Externo + Git
    const ExternalWorldContext& wctx = world_context_service.getWorldContext();
    canvas.drawFastHLine(mx + 8, my + 98, mw - 16, COLOR_CYBER_GRID_BLUE);
    canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(mx + 8, my + 103);
    canvas.printf("SD-MEM: %u episodios | %u fatos | Git Dirty: %u arq.",
                  static_cast<unsigned int>(oracle_brain.getTotalEpisodicRecords()),
                  static_cast<unsigned int>(oracle_brain.getPermanentProfileFacts().size()),
                  static_cast<unsigned int>(cached_env_data.git_modified_files));

    canvas.setTextColor(primary_col);
    canvas.setCursor(mx + 8, my + 115);
    if (wctx.has_valid_sync) {
        canvas.printf("MUNDO: %.1fC | USD R$%.2f | BTC %+.1f%% | GH:%s",
                      wctx.outdoor_temp_celsius,
                      wctx.usd_brl_rate,
                      wctx.btc_variation_pct,
                      !wctx.is_github_down ? "OK" : "WARN");
    } else {
        canvas.print("MUNDO: Sincronizando telemetria externa...");
    }
}

void PetController::renderDebugOverlay(M5Canvas& canvas, float measured_fps, const GazeTargetData& gaze_data, const AudioSensorData& audio_data) {
    // Faixa compacta de telemetria técnica no rodapé (Y = 225..238) quando debug_mode == true (Tecla [D])
    // sem cobrir os olhos nem o visor PIP!
    canvas.fillRect(8, 224, SCREEN_WIDTH - 16, 12, 0x0000);
    canvas.drawRect(8, 224, SCREEN_WIDTH - 16, 12, COLOR_SONAR_GREEN);

    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_SONAR_GREEN);
    canvas.setCursor(12, 226);
    canvas.printf("FPS:%.0f FC:%u YAW:%+.0f dB:%.0f/%.0f ZCR:%.2f",
                  measured_fps,
                  static_cast<unsigned int>(gaze_data.face_count),
                  gaze_data.head_yaw_deg,
                  audio_data.noise_level_db,
                  audio_data.adaptive_noise_floor_db,
                  audio_data.spectral_zcr);
}

void PetController::renderDialogueAndHud(M5Canvas& canvas, float measured_fps, const GazeTargetData& gaze_data, const AudioSensorData& audio_data) {
    canvas.setSwapBytes(true);
    uint32_t now_ms = M5.millis();

    // 1. HUD Cibernético Superior: Badge Poligonal + Ícones 16-bit SD + Mira de Foco/Pomodoro + Radar Sonar Rotativo
    renderTopCyberSystemsHud(canvas, gaze_data, now_ms);

    // 2. VU Meters de Áudio Laterais empilhados nas margens esquerda e direita
    renderSideAudioVuMeters(canvas, audio_data, now_ms);

    // 2.5. Visor Tático Picture-in-Picture (64x48 ou Zoom Óptico HD 160x120 [Z])
    if (!rpg_panel_open && (pip_hd_zoom_active || debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms))) {
        renderCyberPipVisor(canvas, gaze_data, now_ms);
    }

    // 2.8. Modal Interativo de RPG & Matriz de Hábitos 24h (Tecla [TAB] ou clique no Badge de Nível)
    if (rpg_panel_open) {
        renderRpgAndHabitsModal(canvas, now_ms);
    }

    // 3. Caixa de Diálogo de Sobreposição Tática com padding de 10px, Word-Wrap e cursor piscante
    const char* active_dialogue = (lip_sync_state.visible_text[0] != '\0')
                                      ? lip_sync_state.visible_text
                                      : SARCASTIC_ORACLE_BANK[0].reply_text;

    char scanning_buf[96];
    if (oracle_brain.isWaitingAi()) {
        uint32_t dots = (now_ms / 220U) % 4U;
        const char* dot_str = (dots == 0U) ? "" : ((dots == 1U) ? "." : ((dots == 2U) ? ".." : "..."));
        std::snprintf(scanning_buf, sizeof(scanning_buf),
                      "PROCESSANDO / ANALISANDO ALVO%s [SD-MEM:%u | PERFIL:%u]",
                      dot_str,
                      static_cast<unsigned int>(oracle_brain.getTotalEpisodicRecords()),
                      static_cast<unsigned int>(oracle_brain.getPermanentProfileFacts().size()));
        active_dialogue = scanning_buf;
    }

    renderTacticalDialogueOverlay(canvas, active_dialogue, now_ms);

    // 3.5. Gotas refrativas de chuva escorrendo pelo vidro frontal do visor e respingando na borda do Oráculo
    renderVisorRainDrops(canvas, now_ms, true);

    // 4. Efeito de Interferência Eletromagnética / Glitch de Vídeo no susto ou chacoalhão do IMU
    applyVideoGlitchEffect(canvas, now_ms);

    // 4.5. Flash Óptico de Selfie Tática (Gesto ✌️ Paz & Amor / Vitória [K])
    if (now_ms < selfie_flash_until_ms) {
        canvas.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_WHITE);
        canvas.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, TFT_WHITE);
        canvas.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, COLOR_CYBER_ICE_LIGHT);
        if ((selfie_flash_until_ms - now_ms) > 220U) {
            canvas.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_WHITE);
        }
    }

    // 5. Informações de depuração ocultas por padrão; exibidas apenas se debug_mode == true
    if (debug_mode) {
        renderDebugOverlay(canvas, measured_fps, gaze_data, audio_data);
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

