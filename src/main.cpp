#include <M5Unified.h>
#include <cmath>
#include "hal_sensors.h"
#include "pet_state.h"
#include "storage.h"

// =============================================================================
// Constantes Visuais e de Animação (Padrão UPPER_CASE)
// =============================================================================
constexpr uint32_t SLEEP_MODE_FRAME_DELAY_MS = 50; // Reduz ritmo de atualização quando dormindo (~20 FPS)
constexpr uint32_t PERCEPTION_TASK_STACK_BYTES = 8192; // DSP de áudio + câmera + I2C (4 KB era insuficiente)

// =============================================================================
// Variáveis de Estado Global (Padrão snake_case)
// =============================================================================
static M5Canvas screen_canvas(&M5.Display);
static HalSensors hal_sensors;
static PetController pet_controller;
static float measured_fps = 60.0f;

// =============================================================================
// Funções de Inicialização e Wrapper de Ciclo de Vida (Padrão camelCase)
// =============================================================================

#if defined(ARDUINO)
// Task dedicada no Core 0 do ESP32-S3 para leitura assíncrona de sensores / IA sem travar a renderização a 60 FPS
static void perceptionBackgroundTask(void* task_params) {
    (void)task_params;
    for (;;) {
        hal_sensors.updateSensors();
        M5.delay(16);
    }
}
#endif

void initDisplayCanvas() {
    M5.Display.setRotation(1);
    M5.Display.setBrightness(200);

    // Configura Double Buffering em PSRAM (no CoreS3) ou RAM (no PC SDL2)
    screen_canvas.setColorDepth(16);
    screen_canvas.setPsram(true);
    screen_canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    screen_canvas.setSwapBytes(true);

    // Configura fonte UTF-8 embutida com suporte a acentuação em português brasileiro (ã, ç, é, ô, í, ú)
    screen_canvas.setFont(&fonts::efontCN_12);
    screen_canvas.setTextWrap(true, false);
}

void setup() {
    auto cfg = M5.config();
#if defined(ARDUINO)
    cfg.serial_baudrate = 115200;
#endif
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
    cfg.internal_mic = true;
    cfg.internal_spk = true; // Habilita subsistema de áudio para M5.Speaker.tone() no Lip-Sync
    M5.begin(cfg);

    // Monta o cartão microSD (CoreS3) / pasta sd_card (PC) antes de qualquer leitura de config.json
    if (!storageBegin()) {
        M5_LOGW("[STORAGE] Cartao microSD indisponivel: usando configuracao de secrets.h e memoria NVS.");
    }

    M5.Speaker.setVolume(255);

    initDisplayCanvas();
    hal_sensors.initHardware();
    pet_controller.initController();

    // Carrega o Save State persistido (Preferences NVS no CoreS3 ou pet_save.json no PC SDL2)
    pet_controller.restorePersistentState(hal_sensors);

#if defined(ARDUINO)
    // No ESP32-S3 Dual-Core, isola a task de percepção no Core 0 via FreeRTOS
    xTaskCreatePinnedToCore(
        perceptionBackgroundTask,
        "PerceptionTask",
        PERCEPTION_TASK_STACK_BYTES,
        nullptr,
        1,
        nullptr,
        0
    );
#endif

    // Saudação inicial sorteada (evita repetir as falas recentes do histórico)
    pet_controller.speakStartupGreeting(hal_sensors);
}

void loop() {
    uint32_t frame_start_ms = M5.millis();

    // Atualiza eventos do M5Unified (botões, toque e janela SDL2 no PC)
    M5.update();

#if !defined(ARDUINO)
    // 1. No modo PC (native_pc), atualiza a percepção de forma cooperativa no loop principal
    hal_sensors.updateSensors();
#endif
    // Publica um snapshot consistente dos sensores para todo o frame (evita corrida entre Core 0 e Core 1)
    hal_sensors.latchFrameSnapshot();

    // 2. Atualiza Máquina de Estados Híbrida (Camada 1 + Camada 2 + Lip-Sync)
    pet_controller.updateController(hal_sensors, frame_start_ms);

    // 3. Renderiza Rosto Vetorial e Caixa de Diálogo em Double Buffering (M5Canvas)
    pet_controller.renderPetFace(screen_canvas);
    pet_controller.renderDialogueAndHud(
        screen_canvas,
        measured_fps,
        hal_sensors.getGazeData(),
        hal_sensors.getAudioData()
    );
    screen_canvas.pushSprite(0, 0);

    // 4. Controle de cadência: 60 FPS ativo, ou ritmo reduzido em EMOTION_SLEEPING
    uint32_t target_frame_ms = (pet_controller.getCurrentEmotion() == EMOTION_SLEEPING)
                                   ? SLEEP_MODE_FRAME_DELAY_MS
                                   : FRAME_TIME_MS;

    uint32_t frame_elapsed_ms = M5.millis() - frame_start_ms;
    if (frame_elapsed_ms < target_frame_ms) {
        M5.delay(target_frame_ms - frame_elapsed_ms);
    }

    uint32_t total_frame_ms = M5.millis() - frame_start_ms;
    if (total_frame_ms > 0) {
        float instant_fps = 1000.0f / static_cast<float>(total_frame_ms);
        measured_fps = (measured_fps * 0.9f) + (instant_fps * 0.1f);
    }
}

#if !defined(ARDUINO)
// Função de execução em thread dedicada gerenciada pelo Panel_sdl do M5GFX
static int sdlUserAppLoop(bool* is_running) {
    setup();
    do {
        loop();
    } while (*is_running);
    return 0;
}

// Ponto de entrada nativo (Desktop PC / SDL2)
int (main)(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return lgfx::Panel_sdl::main(sdlUserAppLoop);
}
#endif
