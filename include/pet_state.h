#ifndef PET_STATE_H
#define PET_STATE_H

#include <M5Unified.h>
#include <stdint.h>
#include <vector>
#include "hal_sensors.h"
#include "oracle_brain.h"
#include "world_context.h"
#include "voice_synth_worker.h"

// =============================================================================
// Constantes de Tempo e Limiares da Máquina de Estados (Padrão UPPER_CASE)
// =============================================================================
constexpr uint32_t TIME_SCALE_SIMULATION = 1;           // Escala de tempo 1:1 em tempo real (60s reais = 1 minuto)
constexpr uint32_t FOCUS_LIMIT_MINUTES = 50;            // Intervalo de lembrete de pausa a cada 50 min de foco contínuo
constexpr uint32_t ABSENCE_SLEEP_MINUTES = 3;           // Minutos sem rosto para entrar em EMOTION_SLEEPING
constexpr uint32_t MEETING_FATIGUE_MINUTES = 4;         // Minutos de fala contínua para cansaço de reunião
constexpr uint32_t STARTLE_DURATION_MS = 1800;          // Duração do tremor e susto após barulho alto
constexpr uint32_t ORACLE_COOLDOWN_MS = 3500;           // Tempo mínimo entre consultas ao Oráculo
constexpr uint32_t VOICE_PENDING_MAX_WAIT_MS = 9000;    // Quanto uma frase de voz espera o Oráculo ficar livre

// Constantes do Motor de Voz WAV PCM 24kHz HD e Lip-Sync Procedural (UPPER_CASE)
constexpr uint32_t VOICE_WAV_SAMPLE_RATE_HZ = 24000;    // Taxa de amostragem 24kHz HD Mono PCM 16-bit
constexpr uint32_t VOICE_WAV_BYTES_PER_SAMPLE = 2;      // 16 bits = 2 bytes por amostra mono
constexpr uint32_t VOICE_WAV_HEADER_OFFSET = 44;        // Offset padrão do cabeçalho RIFF/WAV
constexpr uint32_t VOICE_RMS_WINDOW_SAMPLES = 384;      // Janela de amostras (~16ms a 24kHz) para cálculo RMS
constexpr uint32_t PROCEDURAL_SYLLABLE_PAUSE_MS = 15;   // Pausa de 15ms entre sílabas no fallback procedural
constexpr uint32_t LIP_SYNC_CHAR_DELAY_MS = 48;         // Velocidade de digitação/pronúncia por caractere
constexpr uint32_t LIP_SYNC_PUNCTUATION_MS = 190;       // Pausa extra em vírgulas e pontos finais
constexpr float MOUTH_OPEN_MAX_HEIGHT = 18.0f;          // Abertura máxima da boca durante vogais/sílabas tônicas
constexpr float MOUTH_CLOSED_HEIGHT = 3.0f;             // Altura da boca fechada em repouso
constexpr uint16_t VOICE_BASE_FREQ_HZ = 440;            // Frequência base da voz procedural
constexpr int32_t DIALOGUE_MAX_LEN = 160;               // Tamanho máximo do texto na caixa de diálogo
constexpr int32_t JSON_PAYLOAD_MAX_LEN = 256;           // Tamanho máximo do buffer JSON de telemetria

// Constantes do Motor Gráfico HD de 16 bits (RGB565) no Cartão MicroSD (UPPER_CASE)
constexpr uint8_t SD_THEME_COUNT = 4;                   // 4 temas Sci-Fi 320x240 em sd_card/gfx/themes/
constexpr uint8_t EYE_LENS_COLOR_COUNT = 4;             // 4 paletas de lentes 64x64 (Ciano, Esmeralda, Âmbar, Vermelho)
constexpr int32_t EYE_LENS_TEX_SIZE = 64;               // Textura esférica 64x64 das lentes ópticas (sd_card/gfx/eyes/)
constexpr size_t EYE_LENS_TEX_PIXELS = static_cast<size_t>(EYE_LENS_TEX_SIZE * EYE_LENS_TEX_SIZE);
constexpr uint8_t HUD_ICON_COUNT = 4;                   // 4 ícones táticos 12x12 em sd_card/gfx/icons/
constexpr int32_t HUD_ICON_SIZE = 12;                   // Ícones táticos anti-aliased 12x12 (sd_card/gfx/icons/)
constexpr size_t HUD_ICON_PIXELS = static_cast<size_t>(HUD_ICON_SIZE * HUD_ICON_SIZE);
constexpr uint16_t COLOR_KEY_TRANSPARENT = 0xF81F;      // Magenta puro (RGB565) para transparência de sprites

// =============================================================================
// Estruturas de Estado Interno, Resposta Multimodal e Geometria (snake_case)
// =============================================================================

struct PetInternalStats {
    int32_t hunger_level;           // 0 a 100 (nível de "fome de dados/café")
    int32_t mood_score;             // 0 a 100 (humor atual do bicho)
    int32_t energy_level;           // 0 a 100 (energia vital)
    uint32_t focus_minutes;         // Minutos de foco silencioso acumulados na sessão
    uint32_t absence_minutes;       // Minutos sem detectar o rosto do dono
    uint32_t meeting_minutes;       // Minutos contínuos detectando voz/reunião
};

struct OracleResponsePayload {
    char reply_text[DIALOGUE_MAX_LEN];  // Resposta sarcástica em português brasileiro
    PetEmotionState target_emotion;     // Nova emoção ordenada pelo Oráculo
    int32_t mood_delta;                 // Impacto numérico no mood_score
    int32_t energy_delta;               // Impacto numérico no energy_level
    int32_t hunger_delta;               // Impacto numérico no hunger_level (alimentação visual)
};

struct FacePoseGeometry {
    float current_eye_x;            // Posição X interpolada da pupila/olhar
    float current_eye_y;            // Posição Y interpolada da pupila/olhar
    float eye_offset_x;             // Deslocamento horizontal interpolado dos olhos
    float eye_offset_y;             // Deslocamento vertical interpolado dos olhos
    float eye_radius_current;       // Raio atual do globo ocular
    float pupil_radius_current;     // Raio atual da pupila (dilata no susto, contrai na raiva)
    float upper_eyelid_closure;     // 0.0 (aberto) a 1.0 (fechado/semicerrado no julgamento)
    float lower_eyelid_closure;     // 0.0 (aberto) a 1.0 (sorriso feliz ou cansaço)
    float eyebrow_angle_deg;        // Ângulo das sobrancelhas (-25 a +25 graus)
    float eyebrow_offset_y;         // Altura vertical das sobrancelhas
    float head_tilt_deg;            // Inclinação lateral da cabeça espelhada do usuário
    float mouth_open_height;        // Abertura vertical da boca sincronizada com a voz
    float mouth_width_current;      // Largura atual da boca
    float mouth_curve_factor;       // Curvatura da boca (-1.0 triste/ranzinza, 0 neutro, +1.0 feliz)
    float shake_offset_x;           // Tremor horizontal (usado no susto/raiva)
    float shake_offset_y;           // Tremor vertical (usado no susto/raiva)
    float eye_scale_x;              // Escala horizontal elástica (Squash & Stretch)
    float eye_scale_y;              // Escala vertical elástica (Squash & Stretch)
    float left_upper_lid;           // Fechamento independente da pálpebra superior esquerda (assimetria/wink)
    float right_upper_lid;          // Fechamento independente da pálpebra superior direita (assimetria/wink)
    float left_lower_lid;           // Fechamento independente da pálpebra inferior esquerda
    float right_lower_lid;          // Fechamento independente da pálpebra inferior direita
    float left_brow_angle_deg;      // Ângulo independente da sobrancelha esquerda
    float right_brow_angle_deg;     // Ângulo independente da sobrancelha direita
    float left_brow_offset_y;       // Altura independente da sobrancelha esquerda
    float right_brow_offset_y;      // Altura independente da sobrancelha direita
    float breathing_offset_y;       // Oscilação vertical orgânica de respiração do rosto
    float cheek_glow_intensity;     // Intensidade das bochechas cibernéticas luminosas (0.0 a 1.0)
};

constexpr uint8_t MAX_HUD_POPUPS = 4;

struct FloatingHudPopup {
    char text[28];                  // Texto curto do popup (ex: "+10 XP", "♥ CARINHO!", "+8 HUMOR")
    float x;                        // Posição X na tela
    float y;                        // Posição Y atual (sobe suavemente)
    float vy;                       // Velocidade vertical de subida
    uint16_t color;                 // Cor RGB565 do popup
    uint32_t spawn_ms;              // Instante de criação
    uint32_t expire_ms;             // Instante de expiração
    bool active;                    // Indica se o slot está ativo
};

struct LipSyncState {
    bool is_speaking;                           // Indica se o bicho está falando no momento
    char full_text[DIALOGUE_MAX_LEN];           // Texto completo a ser pronunciado
    char visible_text[DIALOGUE_MAX_LEN];        // Substring visível gradualmente na legenda
    uint16_t current_char_index;                // Índice da letra atual no Lip-Sync
    uint16_t total_text_length;                 // Comprimento total da frase atual
    uint32_t next_char_timestamp_ms;            // Timestamp para pronunciar a próxima letra
    uint32_t subtitle_expire_ms;                // Quando ocultar a legenda após terminar de falar
};

struct GlitchSliceBand {
    int32_t band_y;                 // Linha inicial Y da faixa de distorção
    int32_t band_h;                 // Altura da faixa horizontal (px)
    int32_t shift_x;                // Deslocamento horizontal (+direita / -esquerda)
};

constexpr uint8_t VISOR_RAINDROP_COUNT = 20;

struct VisorRainDrop {
    float x;                        // Posição horizontal X da gota no vidro (0..320)
    float y;                        // Posição vertical Y da gota escorrendo (25..238)
    float trail_start_y;            // Início do rastro úmido de condensação acima da gota
    float base_vy;                  // Velocidade base de escorrimento vertical
    float vy;                       // Velocidade instantânea modulada pela tensão superficial
    float meander_phase;            // Fase individual para sinuosidade no vidro
    uint32_t next_slip_ms;          // Próximo instante de transição aderência/escorrimento
    bool is_sliding;                // True quando escorrendo rápido, false quando retida por tensão superficial
    uint8_t radius;                 // Raio da cabeça da gota (2 a 4 px)
    uint8_t trail_len;              // Comprimento atual do rastro de condensação (px)
    uint8_t splash_frames;          // Frames restantes de respingo (splash) ao colidir na borda inferior/caixa
};

// =============================================================================
// Classe Controladora da Máquina de Estados e Expressões do Bicho
// =============================================================================
class PetController {
private:
    PetInternalStats internal_stats;
    PetEmotionState current_emotion;
    FacePoseGeometry current_pose;
    FacePoseGeometry target_pose;
    LipSyncState lip_sync_state;
    OracleBrain oracle_brain;
    WorldContextService world_context_service;

    std::vector<uint8_t> wav_buffer;
    std::vector<uint8_t> sfx_wav_buffer;
    uint32_t playback_start_ms;
    uint32_t wav_duration_ms;
    size_t wav_pcm_data_offset;
    bool using_wav_voice;

    uint32_t last_minute_tick_ms;
    uint32_t emotion_lock_until_ms;
    uint32_t startle_until_ms;
    uint32_t last_blink_ms;
    uint32_t next_blink_interval_ms;
    uint32_t last_oracle_trigger_ms;
    uint8_t oracle_phrase_cursor;
    bool is_blinking;
    bool was_user_present;
    bool prev_touch_active;
    bool debug_mode;
    bool imu_shake_detected;
    uint8_t glitch_frames_remaining;
    uint8_t active_glitch_band_count;
    GlitchSliceBand glitch_bands[3];
    char last_state_json[JSON_PAYLOAD_MAX_LEN];

    // Estado de Telemetria do Host, Áudio DSP 2.0, Postura, Pomodoro Tático e Visor PIP (64x48 e 160x120 HD + 3 Modos Ópticos)
    EnvironmentSensorData cached_env_data;
    AudioSensorData cached_audio_data;
    const uint8_t* cached_pip_buffer;
    bool cached_pip_valid;
    const uint8_t* cached_pip_hd_buffer;
    bool cached_pip_hd_valid;
    const uint16_t* cached_pip_rgb565_buffer;
    bool cached_pip_rgb565_valid;
    const uint16_t* cached_pip_hd_rgb565_buffer;
    bool cached_pip_hd_rgb565_valid;
    uint8_t pip_optic_mode; // 0 = Ciano Tático, 1 = Cor Real 16-bit RGB565, 2 = Termográfico IR
    bool pip_hd_zoom_active;
    uint32_t pip_display_until_ms;
    uint32_t bad_posture_since_ms;
    uint32_t last_posture_alert_ms;
    bool pomodoro_active;
    uint32_t pomodoro_end_ms;

    // Estado de Áudio Ambiental no SD (Tecla [B]) e Galeria Timelapse de Snapshots no Visor PIP (Tecla [G])
    bool ambient_focus_enabled;
    uint32_t last_ambient_loop_ms;
    bool timelapse_active;
    uint32_t timelapse_until_ms;
    uint32_t timelapse_last_step_ms;
    size_t timelapse_frame_index;
    std::vector<std::string> timelapse_files;
    uint8_t timelapse_pip_buffer[PIP_FRAME_BYTES];

    // Buffers de Texturas Gráficas HD de 16 bits (RGB565) carregadas do Cartão SD (sd_card/gfx/)
    uint8_t active_theme_index;
    bool sd_gfx_ready;
    std::vector<uint16_t> active_theme_bg_rgb565;
    uint16_t eye_lens_textures_rgb565[4][EYE_LENS_TEX_PIXELS];
    uint16_t hud_icons_rgb565[4][HUD_ICON_PIXELS];

    // Estado de Gamificação RPG, Briefing Matinal, Visão 2.0, Ouvido 2.0 e Modo Silêncio Indefinido
    bool rpg_panel_open;
    bool daily_briefing_done;
    uint32_t phone_look_down_since_ms;
    uint32_t last_phone_alert_ms;
    uint32_t procrastination_since_ms;
    uint32_t last_procrastination_alert_ms;
    uint32_t last_hand_wave_ms;
    uint32_t intruder_since_ms;
    uint32_t last_intruder_alert_ms;
    uint32_t yawn_since_ms;
    uint32_t last_yawn_alert_ms;
    uint32_t object_close_since_ms;
    uint32_t last_object_inspect_ms;
    uint32_t dark_room_since_ms;
    uint32_t last_night_owl_alert_ms;
    uint32_t last_head_gesture_ms;
    uint32_t last_hand_gesture_react_ms;
    uint32_t last_acoustic_gesture_ms;
    bool is_muted_indefinitely;
    uint32_t selfie_flash_until_ms;
    float saccade_offset_x;
    float saccade_offset_y;
    uint32_t next_saccade_ms;
    bool rain_simulation_override;
    VisorRainDrop visor_raindrops[VISOR_RAINDROP_COUNT];

    // Estado de Expressividade Avançada, Cafuné (Head Pat), Piscadinha (Wink), Peak-Hold VU e Popups Flutuantes
    uint32_t head_pat_until_ms;
    uint32_t last_head_pat_reward_ms;
    uint32_t wink_until_ms;
    uint32_t next_wink_ms;
    uint32_t blink_bounce_until_ms;
    int32_t last_touch_x;
    int32_t last_touch_y;
    float touch_stroke_accum;
    float vu_peak_left;
    float vu_peak_right;
    uint32_t last_vu_decay_ms;
    FloatingHudPopup hud_popups[MAX_HUD_POPUPS];

    // 1. Micro-Expressões Oculares de "Vida" (Sacadas Rápidas, Dilatação Pupilar por Lux e Side-Eye)
    float micro_saccade_offset_x;
    float micro_saccade_offset_y;
    uint32_t micro_saccade_until_ms;
    uint32_t next_micro_saccade_ms;
    float pupil_lux_radius_offset;
    uint32_t side_monitor_since_ms;
    bool is_side_eye_active;
    int8_t side_eye_dir;

    // 2. Modo DJ Silencioso (Pulso vertical sincronizado com BPM real)
    float bounce_offset_y;

    // 3. Efeito "Espelho Físico" de Postura (slouch_drop_y e detecção de alívio)
    float slouch_drop_y;
    bool was_slouching_pose;

    // 4. Recompensa Instantânea de Git Commit
    uint16_t prev_git_modified_files;
    uint16_t git_commits_today;

    // 5. Ritual de Encerramento de Expediente (Debriefing de Fim de Tarde)
    bool shift_debrief_done_today;
    uint32_t debrief_overlay_until_ms;

    // 6. Frase de voz aguardando o Oráculo ficar livre (áudio capturado no fim da frase, não depois)
    uint32_t pending_voice_until_ms;
    uint32_t sfx_playback_until_ms;     // Fim do efeito sonoro atual: depois dele o barramento volta ao microfone
    std::string pending_voice_wav_b64;

    // 7. Síntese de voz assíncrona (fora do loop de 60 FPS). Declarado após oracle_brain para ser destruído antes dele.
    VoiceSynthWorker voice_worker;
    uint32_t voice_job_id;
    bool awaiting_voice;
    uint32_t voice_wait_deadline_ms;

    // Métodos internos de percepção, interpolação, DSP de voz, gráficos SD e regras de negócio (camelCase)
    void updatePresenceAndFocus(const GazeTargetData& gaze_data, const AudioSensorData& audio_data, uint32_t now_ms);
    void updateAcousticReactions(const AudioSensorData& audio_data, const EnvironmentSensorData& env_data, uint32_t now_ms);
    void computeTargetFacePose(const GazeTargetData& gaze_data, uint32_t now_ms);
    void interpolateFacePose();
    void updateLipSyncStep(HalSensors& hal_sensors, uint32_t now_ms);
    void playProceduralCharTone(char current_char);
    bool loadVoiceWavForPlayback(std::vector<uint8_t>& synthesized_wav);
    void beginLipSyncPlayback(uint32_t now_ms);
    static void applyEmotionalSciFiDspToWav(std::vector<uint8_t>& target_wav, PetEmotionState emotion_state);
    void initOrGenerateSdAudioAssets();
    void initOrGenerateSdGraphicsAssets();
    bool loadSdThemeToBuffer(uint8_t theme_index);
    uint8_t getActiveOpticColorIndex() const;
    uint16_t getActiveOpticPrimaryColor() const;
    uint16_t getActiveOpticCoreColor() const;
    bool playSdSoundEffect(HalSensors& hal_sensors, const char* relative_sd_path);
    void calculateLipSyncMouthHeight(uint32_t now_ms);
    int32_t clampStatValue(int32_t value, int32_t min_val, int32_t max_val) const;
    bool isDistractionWindow(const char* window_title) const;
    void initVisorRainDrops();
    void updateVisorRainDrops(uint32_t now_ms);
    bool isRainingOnVisor() const;
    void spawnHudPopup(const char* text, float x, float y, uint16_t color, uint32_t duration_ms = 1800U);

    // Métodos auxiliares de renderização estilo "Cyber-Guerreiro" / HUD Militar (camelCase)
    void renderCyberGridBackground(M5Canvas& canvas, uint32_t now_ms);
    void renderVisorRainDrops(M5Canvas& canvas, uint32_t now_ms, bool foreground_layer);
    void renderCombatOpticEye(M5Canvas& canvas, int32_t center_x, int32_t center_y, int32_t pupil_dx, int32_t pupil_dy, bool is_left_eye, uint32_t now_ms);
    void renderLaserArmorEyebrow(M5Canvas& canvas, int32_t center_x, int32_t center_y, float angle_deg, bool is_left_brow);
    void renderCyberCheekBlush(M5Canvas& canvas, int32_t left_eye_x, int32_t right_eye_x, int32_t left_eye_y, int32_t right_eye_y, uint32_t now_ms);
    void renderTacticalCallouts(M5Canvas& canvas, int32_t left_eye_x, int32_t right_eye_x, int32_t eye_y, uint32_t now_ms);
    void renderSynthesisReticleMouth(M5Canvas& canvas, int32_t mouth_x, int32_t mouth_y, uint32_t now_ms);
    void renderEmotionalParticles(M5Canvas& canvas, int32_t left_eye_x, int32_t right_eye_x, int32_t eye_y, uint32_t now_ms);
    void renderFloatingHudPopups(M5Canvas& canvas, uint32_t now_ms);
    void renderTopCyberSystemsHud(M5Canvas& canvas, const GazeTargetData& gaze_data, uint32_t now_ms);
    void renderSideAudioVuMeters(M5Canvas& canvas, const AudioSensorData& audio_data, uint32_t now_ms);
    void renderTacticalDialogueOverlay(M5Canvas& canvas, const char* subtitle_text, uint32_t now_ms);
    void renderWrappedShadowText(M5Canvas& canvas, const char* text_utf8, int32_t box_x, int32_t box_y, int32_t max_width, int32_t line_height, uint16_t text_color, uint16_t shadow_color, int32_t* out_end_x = nullptr, int32_t* out_end_y = nullptr);
    void applyVideoGlitchEffect(M5Canvas& canvas, uint32_t now_ms);
    void renderCyberPipVisor(M5Canvas& canvas, const GazeTargetData& gaze_data, uint32_t now_ms);
    void renderRpgAndHabitsModal(M5Canvas& canvas, uint32_t now_ms);
    void renderShiftDebriefOverlay(M5Canvas& canvas, uint32_t now_ms);
    void renderDebugOverlay(M5Canvas& canvas, float measured_fps, const GazeTargetData& gaze_data, const AudioSensorData& audio_data);

public:
    PetController();

    // Inicializa estado interno, geometria facial e saudação inicial
    void initController();

    // Carrega e salva estado persistido através da HAL (NVS no ESP32 / pet_save.json no PC)
    void restorePersistentState(HalSensors& hal_sensors);
    void syncPersistentState(HalSensors& hal_sensors) const;

    // Atualiza as duas camadas de percepção, física facial e lip-sync a 60 FPS
    void updateController(HalSensors& hal_sensors, uint32_t now_ms);

    // Aciona a Camada 2: Agente Multimodal "Oráculo Sarcástico" (Toque ou Evento Crítico)
    void triggerSarcasticOracle(HalSensors& hal_sensors, const char* custom_reason);

    // Aplica um payload estruturado recebido da IA (ou simulador local)
    void applyOracleResponse(HalSensors& hal_sensors, const OracleResponsePayload& response_payload);

    // Inicia a fala procedural letra por letra com Lip-Sync e legenda gradual
    void speakWithLipSync(HalSensors& hal_sensors, const char* reply_text);

    // Sorteia e fala uma apresentação ao ligar, evitando as frases ditas recentemente
    void speakStartupGreeting(HalSensors& hal_sensors);

    // Corta imediatamente qualquer áudio/fala em andamento (ex.: gesto Shhh ou Palma de Stop)
    void stopSpeakingImmediately(HalSensors& hal_sensors, const char* silent_banner_text = nullptr);

    // Serializa o estado interno atual no formato JSON exigido pela Camada 2
    void buildStateJson(char* out_buffer, size_t buffer_size) const;

    // Renderiza o rosto vetorial fluido e a caixa de diálogo/HUD no M5Canvas
    void renderPetFace(M5Canvas& canvas);
    void renderDialogueAndHud(M5Canvas& canvas, float measured_fps, const GazeTargetData& gaze_data, const AudioSensorData& audio_data);

    // Controle da flag de depuração visual (debug_mode)
    void setDebugMode(bool enabled);
    bool isDebugMode() const;

    // Getters de inspeção (camelCase)
    PetEmotionState getCurrentEmotion() const;
    const char* getEmotionLabel() const;
    const PetInternalStats& getInternalStats() const;
    const LipSyncState& getLipSyncState() const;
    const char* getLastStateJson() const;
};

#endif // PET_STATE_H
