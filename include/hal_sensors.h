#ifndef HAL_SENSORS_H
#define HAL_SENSORS_H

#include <M5Unified.h>
#include <stdint.h>
#include <string>
#include <vector>

#if !defined(ARDUINO)
// Inclusão do SDL2 para captura de eventos de teclado e áudio real no emulador nativo de PC
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#endif
#include <mutex>
#endif

// =============================================================================
// Constantes Globais de Hardware e Simulação (Padrão UPPER_CASE)
// =============================================================================
constexpr int32_t SCREEN_WIDTH = 320;
constexpr int32_t SCREEN_HEIGHT = 240;
constexpr uint32_t TARGET_FPS = 60;
constexpr uint32_t FRAME_TIME_MS = 1000 / TARGET_FPS;

// Limiares e constantes de áudio simulado/real
constexpr float NOISE_FLOOR_DB = 30.0f;
constexpr float SPEECH_LEVEL_DB = 58.0f;
constexpr float LOUD_PEAK_DB = 88.0f;
constexpr float MAX_SPL_DB = 100.0f;

// Parâmetros de Captura de Microfone Real via SDL2 (16kHz, 16-bit PCM, Mono)
constexpr uint32_t MIC_CAPTURE_SAMPLE_RATE = 16000;
constexpr uint8_t MIC_CAPTURE_CHANNELS = 1;
constexpr uint16_t MIC_CAPTURE_BUFFER_FRAMES = 512;
constexpr uint32_t MIC_RECORD_DURATION_SEC = 4;
constexpr size_t MIC_RING_BUFFER_SAMPLES = MIC_CAPTURE_SAMPLE_RATE * MIC_RECORD_DURATION_SEC; // 64.000 amostras (4s)
constexpr size_t MAX_VOICE_NOTES_FILES = 20; // Limite máximo de notas de voz arquivadas em sd_card/voice_notes/

// Caminhos de arquivos temporários para Webcam e Persistência
constexpr const char* PET_SAVE_FILE_PATH = "pet_save.json";
constexpr const char* PET_NVS_NAMESPACE = "bicho_nvs";
constexpr const char* FACE_TRACK_FILE_PATH = "face_track.txt";
constexpr const char* CAM_SNAP_FILE_PATH = "cam_snap.jpg";
constexpr const char* CAM_PIP_FILE_PATH = "cam_pip.raw";
constexpr const char* CAM_PIP_HD_FILE_PATH = "cam_pip_hd.raw";
constexpr const char* CAM_PIP_RGB565_FILE_PATH = "cam_pip_rgb565.raw";
constexpr const char* CAM_PIP_HD_RGB565_FILE_PATH = "cam_pip_hd_rgb565.raw";
constexpr const char* SD_CONFIG_FILE_PATH = "sd_card/config.json";

// Dimensões do Visor Tático Picture-in-Picture (PIP 64x48 padrão e 160x120 Zoom HD)
constexpr int32_t PIP_FRAME_WIDTH = 64;
constexpr int32_t PIP_FRAME_HEIGHT = 48;
constexpr size_t PIP_FRAME_BYTES = static_cast<size_t>(PIP_FRAME_WIDTH * PIP_FRAME_HEIGHT);

constexpr int32_t PIP_HD_FRAME_WIDTH = 160;
constexpr int32_t PIP_HD_FRAME_HEIGHT = 120;
constexpr size_t PIP_HD_FRAME_BYTES = static_cast<size_t>(PIP_HD_FRAME_WIDTH * PIP_HD_FRAME_HEIGHT);

// =============================================================================
// Enumerações de Estado e Eventos (Padrão UPPER_CASE)
// =============================================================================
enum AudioModeState {
    AUDIO_MODE_MIC = 0,
    AUDIO_MODE_SPEAKER
};

enum AudioEventTrigger {
    AUDIO_EVENT_NONE = 0,
    AUDIO_EVENT_LOUD_NOISE,       // Pico de ruído alto (susto)
    AUDIO_EVENT_CONTINUOUS_SPEECH,// Fala contínua (modo reunião/escuta)
    AUDIO_EVENT_INTERACTION,      // Gatilho direto de interação com o pet
    AUDIO_EVENT_VOICE_COMMAND     // Frase curta detectada via VAD Hands-Free
};

enum PetEmotionState {
    EMOTION_NEUTRAL = 0,
    EMOTION_IDLE,
    EMOTION_HAPPY,
    EMOTION_STARTLED,
    EMOTION_LISTENING,
    EMOTION_SLEEPY,
    EMOTION_FOCUSED,
    EMOTION_JUDGING,
    EMOTION_GRUMPY,
    EMOTION_SLEEPING,
    EMOTION_DIZZY
};

// =============================================================================
// Estruturas de Dados da HAL (Atributos em snake_case)
// =============================================================================
struct GazeTargetData {
    float target_x;             // Coordenada X normalizada (-1.0f a +1.0f)
    float target_y;             // Coordenada Y normalizada (-1.0f a +1.0f)
    int32_t raw_screen_x;       // Coordenada X em pixels (0 a SCREEN_WIDTH)
    int32_t raw_screen_y;       // Coordenada Y em pixels (0 a SCREEN_HEIGHT)
    bool face_detected;         // Indica se há um rosto/foco ativo sendo rastreado
    bool touch_active;          // Indica toque na tela (CoreS3) ou clique/arraste (PC)
    float head_tilt_deg;        // Inclinação lateral da cabeça (Roll) em graus (-35.0 a +35.0)
    float head_yaw_deg;         // Rotação horizontal da cabeça (Yaw) em graus (-45.0 a +45.0)
    int32_t face_width_px;      // Largura aparente do rosto em pixels (escala 320x240)
    uint8_t face_count;         // Quantidade de rostos no quadro (0, 1, 2+ para alerta de intruso)
    int32_t secondary_face_x;   // Coordenada X do 2º rosto detectado (0 a SCREEN_WIDTH)
    int32_t secondary_face_y;   // Coordenada Y do 2º rosto detectado (0 a SCREEN_HEIGHT)
    bool is_slouching;          // Postura curvada ("camarão") avaliada em relação ao baseline calibrado
    bool is_too_close;          // Rosto próximo demais avaliado em relação ao baseline calibrado
    bool is_smiling;            // Sorriso detectado pelas landmarks da boca
    bool is_looking_down_phone; // Cabeça inclinada para baixo (olhando celular / doomscrolling)
    bool hand_wave_detected;    // Gesto de aceno de mão no campo periférico superior
    bool is_yawning;            // Bocejo prolongado / fadiga visual detectada
    bool head_nod_yes;          // Gesto temporal de cabeça "SIM" (oscilação vertical rápida)
    bool head_shake_no;         // Gesto temporal de cabeça "NÃO" (oscilação horizontal rápida)
    bool object_held_close;     // Objeto (caneca, garrafa, lanche, placa) exibido perto da lente/sensor
    bool posture_calibrated;    // Indica se o baseline postural pessoal já foi calibrado nesta sessão
    uint8_t scene_change_pct;   // Percentual de mudança visual na cena (0 a 100%)
    bool gesture_shhh;          // Gesto de dedo indicador vertical sobre a boca ("Shhh!" -> silêncio/mudo)
    bool gesture_thumbs_up;     // Gesto de Joinha ("Thumbs Up" -> aprovação / +XP)
    bool gesture_victory;       // Gesto de Paz e Amor / Vitória ("V-Sign" -> dispara selfie tática no SD)
    bool gesture_open_palm;     // Gesto de Palma da Mão Aberta ("Stop / Pause" -> pausa fala ou Pomodoro)
};

struct AudioSensorData {
    float noise_level_db;           // Nível de pressão sonora estimado em dB (30 a 100 dB)
    float adaptive_noise_floor_db;  // Piso de ruído ambiente adaptativo aprendido em tempo real (dB)
    float spectral_zcr;             // Taxa de Cruzamento por Zero (Zero-Crossing Rate: 0.0 a 1.0)
    bool is_loud_peak;              // Flag de pico repentino de barulho alto
    bool is_continuous_speech;      // Flag de fala contínua detectada (reunião)
    bool interaction_triggered;     // Gatilho de interação disparado
    bool vad_speech_active;         // Indica que o usuário está falando uma frase no momento (VAD)
    bool vad_phrase_completed;      // Indica que o usuário terminou uma frase curta -> aciona Oráculo Hands-Free
    bool vad_enabled;               // Indica se o modo Hands-Free por voz está ativo (Tecla [V])
    bool vad_keyword_mode;          // Exige a palavra-chave ("Bicho" / "Ei Bicho" / "Oráculo") para responder por voz
    bool wake_word_detected;        // Indica se a assinatura acústica da palavra-chave foi detectada na frase
    bool is_typing_cadence;         // Indica cadência de digitação mecânica (evita falso disparo de voz e premia foco)
    bool double_clap_detected;      // Gatilho de Duas Palmas ("Clap-Clap" 👏👏 -> acorda do modo silêncio / chama atenção)
    bool whistle_detected;          // Gatilho de Assobio Humano (tom senoidal 950Hz-2800Hz -> reação alegre imediata)
    bool music_beat_active;         // Indica batida musical rítmica ativa no ambiente
    float music_beat_phase;         // Fase rítmica (0.0 a 1.0) para balanço sutil dos olhos/sobrancelhas
    bool silence_toggle_kbd_triggered; // Indica que a tecla [Q] foi pressionada para alternar Modo Silêncio
    bool pomodoro_toggle_triggered; // Gatilho para iniciar/encerrar Missão Pomodoro (Tecla [F])
    bool gallery_timelapse_triggered;    // Gatilho para reproduzir Timelapse de Snapshots do SD no PIP (Tecla [G])
    bool ambient_audio_toggle_triggered; // Gatilho para alternar Som Ambiente de Foco do SD (Tecla [B])
    bool voice_note_replay_triggered;    // Gatilho para reproduzir última Nota de Voz da Caixa Preta SD (Tecla [N])
    bool theme_cycle_triggered;          // Gatilho para ciclar Tema de Cockpit Sci-Fi do SD (Tecla [T])
    bool pip_zoom_toggle_triggered;      // Gatilho para alternar Zoom Óptico HD 160x120 (Tecla [Z])
    bool rpg_modal_toggle_triggered;     // Gatilho para abrir/fechar Painel de RPG e Hábitos (Tecla [TAB])
    bool calibrate_posture_triggered;    // Gatilho para recalibrar postura pessoal (Tecla [C])
    bool pip_color_mode_triggered;       // Gatilho para ciclar modo óptico do PIP: Ciano / RGB565 / Térmico (Tecla [X])
    bool object_scan_triggered;          // Gatilho manual para inspecionar objeto na câmera (Tecla [S])
    bool rain_preview_toggle_triggered;  // Gatilho para simular/alternar chuva com gotas escorrendo na tela (Tecla [W])
    AudioEventTrigger last_event;   // Último evento de áudio registrado
};

struct EnvironmentSensorData {
    float accel_x;                  // Aceleração eixo X (g) - BMI270
    float accel_y;                  // Aceleração eixo Y (g) - BMI270
    float accel_z;                  // Aceleração eixo Z (g) - BMI270
    uint16_t ambient_lux;           // Luminosidade ambiente real (Lux) - Câmera / LTR-553ALS
    uint16_t proximity_raw;         // Proximidade bruta - LTR-553ALS / Câmera
    uint8_t battery_percent;        // Nível de bateria (%) - AXP2101
    bool is_charging;               // Estado de carregamento - AXP2101
    uint8_t cpu_usage_percent;      // Uso real de CPU (%) do PC ou carga interna do ESP32-S3
    uint8_t ram_usage_percent;      // Uso real de RAM (%) do PC ou PSRAM/Heap do ESP32-S3
    uint8_t git_modified_files;     // Quantidade de arquivos modificados no repositório Git local
    char active_window_title[64];   // Título da janela ativa no PC (ex: VS Code, Chrome) ou task no CoreS3
};

struct SdSystemConfig {
    char wifi_ssid[64];             // SSID da rede Wi-Fi lido do SD (sd_card/config.json)
    char wifi_password[64];         // Senha Wi-Fi lida do SD
    char gemini_api_key[96];        // Chave da API Gemini opcional no SD
    float latitude;                 // Latitude geográfica para clima Open-Meteo
    float longitude;                // Longitude geográfica para clima Open-Meteo
    float time_scale;               // Multiplicador de velocidade biológica
    uint8_t speaker_volume;         // Volume do alto-falante (0 a 255)
    bool morning_briefing_enabled;  // Dispara resumo matinal automático ao sincronizar mundo externo
};

struct PersistentPetData {
    int32_t hunger_level;           // Nível de fome persistido (0 a 100)
    int32_t mood_score;             // Humor persistido (0 a 100)
    int32_t energy_level;           // Energia persistida (0 a 100)
    uint32_t focus_minutes;         // Minutos acumulados de foco
};

// =============================================================================
// Classe da Camada de Abstração de Hardware (HAL)
// =============================================================================
class HalSensors {
private:
    GazeTargetData gaze_data;
    AudioSensorData audio_data;
    EnvironmentSensorData env_data;
    SdSystemConfig sd_config;
    AudioModeState current_audio_mode;
    uint32_t last_loud_peak_ms;
    uint8_t pip_frame_gray[PIP_FRAME_BYTES];
    bool has_valid_pip_frame;
    uint8_t pip_hd_frame_gray[PIP_HD_FRAME_BYTES];
    bool has_valid_pip_hd_frame;
    uint16_t pip_frame_rgb565[PIP_FRAME_BYTES];
    uint16_t pip_hd_frame_rgb565[PIP_HD_FRAME_BYTES];
    bool has_valid_pip_rgb565;

    // Estado compartilhado multiplataforma (PC + CoreS3) para Auto-Calibração Postural e Gestos de Cabeça
    float calibrated_face_y;
    float calibrated_face_w;
    uint16_t calibration_samples;
    static constexpr size_t GESTURE_HISTORY_LEN = 16;
    int16_t gesture_hist_x[GESTURE_HISTORY_LEN];
    int16_t gesture_hist_y[GESTURE_HISTORY_LEN];
    uint8_t gesture_hist_count;
    uint8_t gesture_hist_head;
    uint32_t last_gesture_sample_ms;
    uint32_t last_head_gesture_ms;
    uint32_t nod_active_until_ms;
    uint32_t shake_active_until_ms;
    uint32_t shhh_active_until_ms;
    uint32_t thumb_active_until_ms;
    uint32_t victory_active_until_ms;
    uint32_t palm_active_until_ms;

    void updateSharedVisionCalibrationAndGestures(uint32_t now_ms, bool raw_slouch_hint, bool raw_close_hint);

    // Estado interno do Ouvido 2.0 (DSP Acústico Multiplataforma PC + CoreS3, VAD, Palavra-Chave, Palmas, Assobio e Teclado)
    uint32_t vad_active_speech_frames;
    uint32_t vad_trailing_silence_frames;
    uint32_t last_vad_trigger_ms;
    uint32_t mic_echo_guard_until_ms;
    float dsp_noise_floor_db;
    float dsp_prev_block_db;
    float dsp_prev_zcr;
    float dsp_latest_zcr;
    float dsp_latest_crest;
    bool dsp_impulse_active;
    uint32_t dsp_impulse_start_ms;
    float dsp_impulse_peak_db;
    float dsp_impulse_max_zcr;
    float dsp_impulse_max_crest;
    uint8_t dsp_clap_stage;
    uint32_t dsp_last_clap_ms;
    bool dsp_double_clap_latched;
    uint32_t dsp_whistle_accum_ms;
    uint32_t dsp_last_whistle_ms;
    bool dsp_whistle_latched;
    uint8_t dsp_typing_click_count;
    uint32_t dsp_typing_window_start_ms;
    uint32_t dsp_typing_active_until_ms;
    uint8_t dsp_kw_phonetic_stage;       // 0=idle, 1=vogal inicial ("Bi"/"Ei"), 2=fricativa/transição ("ch"), 3=vogal final ("o")
    uint16_t dsp_voiced_formant_frames;  // Contagem de blocos com formante vocal humano (ZCR < 0.175) na frase atual
    uint8_t dsp_syllable_peaks;          // Picos de envoltória silábica na frase atual
    bool dsp_in_syllable_peak;
    bool dsp_wake_word_latched;
    uint32_t dsp_last_beat_peak_ms;
    uint32_t dsp_beat_interval_ms;
    uint8_t dsp_beat_streak;
    uint32_t dsp_music_active_until_ms;

    void analyzeSharedPcmBlockDsp(const int16_t* samples, size_t sample_count, float block_db, double rms_raw, uint32_t now_ms);

#if defined(ARDUINO)
    // Métodos e estado exclusivos para o hardware real ESP32-S3 (M5Stack CoreS3: GC0308 + LTR-553ALS)
    bool cores3_camera_initialized;
    bool cores3_ltr553_initialized;
    uint32_t last_cores3_cam_capture_ms;
    uint32_t last_cores3_ltr_read_ms;
    uint8_t prev_cores3_luma_grid[64]; // Grade 8x8 para cálculo de mudança de cena e aceno no CoreS3
    mutable std::vector<int16_t> cores3_mic_ring_buffer;
    mutable size_t cores3_mic_write_pos;
    mutable bool cores3_mic_filled;

    void initCoreS3Peripherals();
    void readCoreS3CameraGaze();
    void readCoreS3Microphone();
    void readCoreS3OnboardSensors();
#else
    // Métodos e estado exclusivos para o ambiente Desktop PC (SDL2 + Microfone Real + Webcam Real)
    bool prev_key_space;
    bool prev_key_m;
    bool prev_key_i;
    bool prev_key_r;
    bool prev_key_p;
    bool prev_key_o;
    bool prev_key_d;
    bool prev_key_f;
    bool prev_key_v;
    bool prev_key_g;
    bool prev_key_b;
    bool prev_key_n;
    bool prev_key_t;
    bool prev_key_z;
    bool prev_key_tab;
    bool prev_key_c;
    bool prev_key_x;
    bool prev_key_s;
    bool prev_key_q;
    bool prev_key_j;
    bool prev_key_k;
    bool prev_key_h;
    bool prev_key_w;
    bool simulated_presence_enabled;
    bool manual_meeting_override;
    uint16_t webcam_ambient_lux;

    // Captura de microfone real via SDL_OpenAudioDevice
    uint32_t mic_device_id;
    bool mic_capture_active;
    mutable std::mutex mic_buffer_mutex;
    mutable std::vector<int16_t> mic_ring_buffer;
    mutable size_t mic_ring_write_pos;
    mutable bool mic_ring_filled;
    float realtime_mic_db;
    uint32_t sustained_voice_frames;
    uint32_t silence_voice_frames;
    uint32_t last_face_track_check_ms;
    uint32_t last_pip_read_ms;
    bool webcam_tracker_active;

    static void sdlAudioCaptureCallback(void* userdata, uint8_t* stream, int len);
    void processCapturedAudioSamples(const int16_t* samples, size_t sample_count);
    bool readWebcamTrackerFile(int32_t& out_face_x, int32_t& out_face_y, bool& out_face_detected);
    void updateWebcamPipBuffer(uint32_t now_ms);

    void readPcSimulatedGaze();
    void readPcSimulatedAudio();
    void readPcSimulatedEnvironment();
#endif
    bool debug_mode_enabled;

    // Utilitário interno para codificar buffers binários (WAV/JPEG) em Base64
    static std::string encodeBase64(const uint8_t* data, size_t length);

public:
    HalSensors();
    ~HalSensors();

    // Inicializa sensores ou subsistemas de captura real (SDL2 Audio / Webcam / GC0308 + LTR-553ALS)
    bool initHardware();

    // Recalibra o baseline de postura ergonômica pessoal usando a posição atual do usuário
    void recalibratePostureBaseline();

    // Carrega ou cria o arquivo de configuração unificada sd_card/config.json
    bool loadOrInitSdConfig();
    const SdSystemConfig& getSdConfig() const;

    // Atualiza todas as leituras da HAL (deve ser chamado a cada frame)
    void updateSensors();
    void updatePerceptionSensors();

    // Grava 4 segundos das amostras PCM capturadas pelo microfone em WAV e codifica em Base64
    std::string recordUserAudioWav() const;

    // Captura 1 frame 320x240 em JPEG da webcam/GC0308 e codifica em Base64 para o Gemini
    std::string captureCameraJpegBase64() const;

    // Alterna de forma segura o barramento de áudio entre Mic e Speaker
    void switchAudioBusMode(AudioModeState desired_mode);

    // Define manualmente se um rosto está presente no campo de visão (útil para testes no PC)
    void setFacePresence(bool is_present);

    // Controle do modo de depuração visual (overlay de telemetria)
    void setDebugMode(bool is_enabled);
    bool isDebugModeEnabled() const;

    // Persistência Multiplataforma (Save State: NVS Preferences no ESP32 / pet_save.json no PC)
    bool savePetState(const PersistentPetData& save_data);
    bool loadPetState(PersistentPetData& out_save_data);

    // Cliente HTTP / IA Abstraído (WiFiClientSecure + HTTPClient + ArduinoJson no ESP32 / Mock ou Script no PC)
    bool requestOracleInference(const char* state_json,
                                char* out_reply_text,
                                size_t reply_buffer_size,
                                PetEmotionState& out_target_emotion,
                                int32_t& out_mood_delta,
                                int32_t& out_energy_delta,
                                int32_t* out_hunger_delta = nullptr,
                                std::string* out_new_memory = nullptr,
                                std::string* out_permanent_fact = nullptr,
                                std::string* out_memory_tags = nullptr);

    // Getters de leitura rápida (camelCase)
    const GazeTargetData& getGazeData() const;
    const AudioSensorData& getAudioData() const;
    const EnvironmentSensorData& getEnvironmentData() const;
    AudioModeState getAudioBusMode() const;
    const uint8_t* getPipFrameBuffer() const;
    bool hasValidPipFrame() const;
    const uint8_t* getPipHdFrameBuffer() const;
    bool hasValidPipHdFrame() const;
    const uint16_t* getPipFrameRgb565Buffer() const;
    const uint16_t* getPipHdFrameRgb565Buffer() const;
    bool hasValidPipRgb565Frame() const;
};

#endif // HAL_SENSORS_H
