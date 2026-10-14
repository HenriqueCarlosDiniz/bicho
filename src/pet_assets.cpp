#include "pet_controller_internal.h"

// PetController: geração e carga de assets de áudio e gráficos no Cartão SD

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
