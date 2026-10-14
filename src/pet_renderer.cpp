#include "pet_controller_internal.h"

// PetController: renderização do rosto, HUD, visor PIP, overlays e efeitos

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
    float base_scale = current_pose.eye_radius_current / FACE_EYE_DEFAULT_R;
    float scale_x = base_scale * current_pose.eye_scale_x;
    float scale_y = base_scale * current_pose.eye_scale_y;
    int32_t rx = static_cast<int32_t>(std::round(FACE_EYE_RX_DEFAULT * scale_x)); // ~26px modulável
    int32_t ry = static_cast<int32_t>(std::round(FACE_EYE_RY_DEFAULT * scale_y)); // ~23px modulável
    if (rx < 8) rx = 8;
    if (ry < 6) ry = 6;

    uint8_t color_idx = getActiveOpticColorIndex();
    uint16_t optic_primary = getActiveOpticPrimaryColor();
    uint16_t optic_core = getActiveOpticCoreColor();

    // 1. Medidores Orbitais Segmentados girando suavemente em torno das órbitas oculares em paleta harmônica
    int32_t orb_r = ((rx > ry) ? rx : ry) + 6;
    canvas.drawCircle(center_x, center_y, orb_r, COLOR_CYBER_GRID_BLUE);

    float spin_speed = (current_emotion == EMOTION_STARTLED || current_emotion == EMOTION_DIZZY) ? 0.0065f : 0.0020f;
    if (cached_audio_data.is_typing_active) {
        spin_speed += cached_audio_data.typing_click_rate * 0.0008f;
    }
    float base_spin = static_cast<float>(now_ms) * (is_left_eye ? spin_speed : -spin_speed);
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

    // 2. Renderiza a Textura 16-bit 64x64 HD da Lente Óptica do Cartão SD com suporte a Squash & Stretch (scale_x / scale_y)
    const uint16_t* lens_tex = eye_lens_textures_rgb565[color_idx % EYE_LENS_COLOR_COUNT];
    if (std::fabs(scale_x - 1.0f) < 0.05f && std::fabs(scale_y - 1.0f) < 0.05f) {
        canvas.pushImage(
            center_x - (EYE_LENS_TEX_SIZE / 2),
            center_y - (EYE_LENS_TEX_SIZE / 2),
            EYE_LENS_TEX_SIZE,
            EYE_LENS_TEX_SIZE,
            lens_tex,
            COLOR_KEY_TRANSPARENT
        );
    } else {
        int32_t half_w = static_cast<int32_t>(std::round(32.0f * scale_x));
        int32_t half_h = static_cast<int32_t>(std::round(32.0f * scale_y));
        if (half_w < 8) half_w = 8;
        if (half_h < 6) half_h = 6;
        for (int32_t dy = -half_h; dy < half_h; ++dy) {
            int32_t sy = ((dy + half_h) * EYE_LENS_TEX_SIZE) / (half_h * 2);
            if (sy < 0) sy = 0;
            if (sy >= EYE_LENS_TEX_SIZE) sy = EYE_LENS_TEX_SIZE - 1;
            for (int32_t dx = -half_w; dx < half_w; ++dx) {
                int32_t sx = ((dx + half_w) * EYE_LENS_TEX_SIZE) / (half_w * 2);
                if (sx < 0) sx = 0;
                if (sx >= EYE_LENS_TEX_SIZE) sx = EYE_LENS_TEX_SIZE - 1;
                uint16_t px_col = lens_tex[sy * EYE_LENS_TEX_SIZE + sx];
                if (px_col != COLOR_KEY_TRANSPARENT) {
                    canvas.drawPixel(center_x + dx, center_y + dy, px_col);
                }
            }
        }
    }

    // 3. Pupilas Expressivas: Retículo de Mira padrão ou símbolos especiais (Arco Feliz ^ ^, Espiral @ @, Exclamação !)
    int32_t reticle_x = center_x + pupil_dx;
    int32_t reticle_y = center_y + pupil_dy;
    int32_t pup_r = static_cast<int32_t>(current_pose.pupil_radius_current);
    bool is_winking_this_eye = (!is_left_eye && (now_ms < wink_until_ms) && !is_blinking);
    bool happy_arch_mode = (now_ms < head_pat_until_ms) || is_winking_this_eye ||
                           (current_emotion == EMOTION_HAPPY && current_pose.lower_eyelid_closure > 0.36f);

    if (current_emotion == EMOTION_DIZZY) {
        // Pupila em espiral giratória (@ @) durante sobrecarga ou tontura
        canvas.fillCircle(reticle_x, reticle_y, pup_r + 3, optic_core);
        float sp_base = static_cast<float>(now_ms) * (is_left_eye ? 0.015f : -0.015f);
        int32_t prev_sx = reticle_x;
        int32_t prev_sy = reticle_y;
        for (int step = 1; step <= 20; ++step) {
            float t = static_cast<float>(step) * 0.45f;
            float r_sp = (static_cast<float>(step) / 20.0f) * static_cast<float>(pup_r + 3);
            int32_t sx = reticle_x + static_cast<int32_t>(std::round(std::cos(sp_base + t) * r_sp));
            int32_t sy = reticle_y + static_cast<int32_t>(std::round(std::sin(sp_base + t) * r_sp));
            canvas.drawLine(prev_sx, prev_sy, sx, sy, (step % 2 == 0) ? TFT_WHITE : optic_primary);
            prev_sx = sx;
            prev_sy = sy;
        }
    } else if (happy_arch_mode) {
        // Brilho interno de arco feliz luminoso (^ ^) estilo Cozmo/Vector quando recebe cafuné, pisca um olho ou está radiante!
        canvas.fillCircle(reticle_x, reticle_y, pup_r + 2, optic_core);
        for (int32_t dx = -pup_r - 2; dx <= pup_r + 2; ++dx) {
            float nx = static_cast<float>(dx) / static_cast<float>(pup_r + 2);
            int32_t arch_y = reticle_y - static_cast<int32_t>(std::round((1.0f - nx * nx) * 5.0f)) + 2;
            canvas.fillRect(reticle_x + dx, arch_y - 1, 1, 3, TFT_WHITE);
            canvas.drawPixel(reticle_x + dx, arch_y - 2, optic_primary);
        }
        // Brilho estrelado de alegria no canto superior da lente
        int32_t gl_x = reticle_x - pup_r;
        int32_t gl_y = reticle_y - pup_r;
        canvas.drawFastHLine(gl_x - 2, gl_y, 5, TFT_WHITE);
        canvas.drawFastVLine(gl_x, gl_y - 2, 5, TFT_WHITE);
    } else {
        // Câmara escura interna do sensor óptico e anéis concêntricos na cor emocional ativa
        int32_t flow_pulse = 0;
        if (cached_audio_data.is_typing_active) {
            float rate = (cached_audio_data.typing_click_rate > 1.0f) ? cached_audio_data.typing_click_rate : 3.0f;
            flow_pulse = static_cast<int32_t>(std::round(std::sin(static_cast<float>(now_ms) * 0.008f * rate) * 1.5f));
        }
        int32_t eff_pup_r = pup_r + flow_pulse;
        if (eff_pup_r < 4) eff_pup_r = 4;

        canvas.fillCircle(reticle_x, reticle_y, eff_pup_r + 3, optic_core);
        canvas.drawCircle(reticle_x, reticle_y, eff_pup_r + 3, optic_primary);
        canvas.drawCircle(reticle_x, reticle_y, eff_pup_r, TFT_WHITE);

        // Ticks cruzados do retículo da pupila (Crosshair) em azul-gelo harmônico (gira proporcionalmente ao Coder Flow!)
        if (cached_audio_data.is_typing_active) {
            float rot_speed = 0.003f + cached_audio_data.typing_click_rate * 0.0012f;
            float ret_ang = static_cast<float>(now_ms) * (is_left_eye ? rot_speed : -rot_speed);
            for (int k = 0; k < 4; ++k) {
                float a = ret_ang + static_cast<float>(k) * 1.570796f;
                int32_t x0 = reticle_x + static_cast<int32_t>(std::round(std::cos(a) * static_cast<float>(eff_pup_r + 2)));
                int32_t y0 = reticle_y + static_cast<int32_t>(std::round(std::sin(a) * static_cast<float>(eff_pup_r + 2)));
                int32_t x1 = reticle_x + static_cast<int32_t>(std::round(std::cos(a) * static_cast<float>(eff_pup_r + 5)));
                int32_t y1 = reticle_y + static_cast<int32_t>(std::round(std::sin(a) * static_cast<float>(eff_pup_r + 5)));
                canvas.drawLine(x0, y0, x1, y1, COLOR_OPTIC_EMERALD);
            }
        } else {
            canvas.drawFastHLine(reticle_x - eff_pup_r - 5, reticle_y, 4, COLOR_CYBER_ICE_LIGHT);
            canvas.drawFastHLine(reticle_x + eff_pup_r + 2, reticle_y, 4, COLOR_CYBER_ICE_LIGHT);
            canvas.drawFastVLine(reticle_x, reticle_y - eff_pup_r - 5, 4, COLOR_CYBER_ICE_LIGHT);
            canvas.drawFastVLine(reticle_x, reticle_y + eff_pup_r + 2, 4, COLOR_CYBER_ICE_LIGHT);
        }

        // Núcleo laser brilhante + segundo ponto de brilho especular dinâmico na pupila (dá vida e simpatia ao olhar!)
        canvas.fillRect(reticle_x - 1, reticle_y - 1, 3, 3, TFT_WHITE);
        int32_t catchlight_x = reticle_x - (eff_pup_r / 2) - 1;
        int32_t catchlight_y = reticle_y - (eff_pup_r / 2) - 1;
        canvas.fillCircle(catchlight_x, catchlight_y, 2, TFT_WHITE);
        canvas.drawPixel(reticle_x + (eff_pup_r / 2), reticle_y + (eff_pup_r / 2), COLOR_CYBER_ICE_LIGHT);
        canvas.drawPixel(reticle_x, reticle_y, optic_primary);
    }

    // 4. Obturador Mecânico Superior Independente por Olho (permite piscadinha de um olho só e olhar analítico!)
    float upper_closure = is_left_eye ? current_pose.left_upper_lid : current_pose.right_upper_lid;
    if (upper_closure > 0.03f) {
        int32_t lid_cut_y = (center_y - ry) + static_cast<int32_t>((ry * 1.85f) * upper_closure);
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

    // 5. Obturador Mecânico Inferior em Arco Sorridente Independente por Olho (olhos sorridentes estilo Cozmo/Vector)
    float lower_closure = is_left_eye ? current_pose.left_lower_lid : current_pose.right_lower_lid;
    if (lower_closure > 0.05f) {
        int32_t base_lower_h = static_cast<int32_t>(ry * lower_closure);
        for (int32_t dx = -rx; dx <= rx; ++dx) {
            float norm_x = static_cast<float>(dx) / static_cast<float>(rx > 0 ? rx : 1);
            int32_t arch_boost = static_cast<int32_t>(std::round((1.0f - norm_x * norm_x) * 5.0f));
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
    // O ângulo já vem calculado individualmente (left_brow_angle_deg / right_brow_angle_deg) permitindo expressões assimétricas ricas!
    (void)is_left_brow;
    float effective_angle = angle_deg;

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

void PetController::renderCyberCheekBlush(
    M5Canvas& canvas,
    int32_t left_eye_x,
    int32_t right_eye_x,
    int32_t left_eye_y,
    int32_t right_eye_y,
    uint32_t now_ms
) {
    if (current_pose.cheek_glow_intensity < 0.12f) {
        return;
    }

    // Bochechas cibernéticas luminosas (traços diagonais neon /// abaixo das órbitas quando feliz, sorrindo ou recebendo cafuné)
    bool pulse_bright = ((now_ms / 240U) % 2U) == 0U;
    uint16_t blush_col = (current_pose.cheek_glow_intensity > 0.65f && pulse_bright)
                             ? COLOR_CYBER_ORANGE
                             : COLOR_OPTIC_EMERALD;

    int32_t lx_base = left_eye_x - 22;
    int32_t ly_base = left_eye_y + 24;
    int32_t rx_base = right_eye_x + 12;
    int32_t ry_base = right_eye_y + 24;

    for (int i = 0; i < 3; ++i) {
        int32_t offset = i * 5;
        // Bochecha esquerda (///)
        canvas.drawLine(lx_base + offset, ly_base + 5, lx_base + offset + 3, ly_base, blush_col);
        canvas.drawLine(lx_base + offset + 1, ly_base + 5, lx_base + offset + 4, ly_base, blush_col);
        // Bochecha direita (///)
        canvas.drawLine(rx_base + offset, ry_base + 5, rx_base + offset + 3, ry_base, blush_col);
        canvas.drawLine(rx_base + offset + 1, ry_base + 5, rx_base + offset + 4, ry_base, blush_col);
    }
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
    if (now_ms < head_pat_until_ms) {
        canvas.print("AFETO:CAFUNE+");
    } else if (rain_simulation_override) {
        canvas.print("CLIMA:CHUVA[W]");
    } else if (cached_audio_data.is_typing_cadence) {
        canvas.print("DSP:LENDO COD");
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
    // Boca de Retículo / Interface de Síntese expressiva (largura 88px: X = 116..204) com contorno labial luminoso e curvas emocionais
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

    // Colchetes laterais de contenção do sintetizador vocal (inclinam sutilmente junto com o sorriso!)
    int32_t corner_lift = static_cast<int32_t>(std::round(current_pose.mouth_curve_factor * 2.5f));
    canvas.drawFastVLine(left_x - 5, mouth_y - 6 - corner_lift, 13, primary_col);
    canvas.drawFastHLine(left_x - 5, mouth_y - 6 - corner_lift, 4, primary_col);
    canvas.drawFastHLine(left_x - 5, mouth_y + 6 - corner_lift, 4, primary_col);

    int32_t right_lift = corner_lift + ((current_emotion == EMOTION_JUDGING) ? 2 : 0);
    int32_t right_bracket_x = left_x + synth_total_w + 3;
    canvas.drawFastVLine(right_bracket_x, mouth_y - 6 - right_lift, 13, primary_col);
    canvas.drawFastHLine(right_bracket_x - 3, mouth_y - 6 - right_lift, 4, primary_col);
    canvas.drawFastHLine(right_bracket_x - 3, mouth_y + 6 - right_lift, 4, primary_col);

    // Modo especial: Boca em 'O' de espanto (EMOTION_STARTLED) ou bocejo profundo (EMOTION_SLEEPY sem falar)
    if (!lip_sync_state.is_speaking &&
        (current_emotion == EMOTION_STARTLED || (current_emotion == EMOTION_SLEEPY && current_pose.mouth_open_height > 7.5f))) {
        int32_t o_rx = (current_emotion == EMOTION_STARTLED) ? 14 : 11;
        int32_t o_ry = (current_emotion == EMOTION_STARTLED) ? 7  : 6;
        canvas.fillEllipse(mouth_x, mouth_y, o_rx, o_ry, COLOR_HUD_INNER_BG);
        canvas.drawEllipse(mouth_x, mouth_y, o_rx, o_ry, primary_col);
        canvas.drawEllipse(mouth_x, mouth_y, o_rx - 2, o_ry - 2, COLOR_CYBER_ICE_LIGHT);
        canvas.drawFastHLine(mouth_x - 5, mouth_y, 11, TFT_WHITE);
        return;
    }

    int32_t prev_cx = 0;
    int32_t prev_top_y = 0;
    int32_t prev_bot_y = 0;

    // Barras verticais moduladas pelo áudio (RMS Lip-Sync) + contorno labial superior/inferior conectado
    for (int32_t i = 0; i < num_bars; ++i) {
        float norm_pos = (static_cast<float>(i) - 5.0f) / 5.0f; // [-1.0 .. +1.0]
        int32_t dist_center = std::abs(i - (num_bars / 2));
        float envelope = 1.0f - (static_cast<float>(dist_center) / static_cast<float>((num_bars / 2) + 1));

        // Curvatura emocional amplificada (+mouth_curve_factor abaixa o centro e levanta as pontas em U; em EMOTION_JUDGING faz smirk!)
        float curve_arch = (0.48f - norm_pos * norm_pos) * current_pose.mouth_curve_factor * 7.2f;
        if (current_emotion == EMOTION_JUDGING) {
            curve_arch -= norm_pos * 3.4f; // Sorriso de canto irônico (lado direito visivelmente mais alto)
        } else if (current_emotion == EMOTION_DIZZY) {
            curve_arch = std::sin(norm_pos * 4.2f + static_cast<float>(now_ms) * 0.015f) * 3.5f; // Boca ondulada em curto-circuito!
        }
        int32_t bar_center_y = mouth_y + static_cast<int32_t>(std::round(curve_arch));

        int32_t bar_half_h = 1;
        if (lip_sync_state.is_speaking || current_pose.mouth_open_height > 4.0f) {
            float wave = std::fabs(std::sin(static_cast<float>(now_ms) * 0.035f + static_cast<float>(i) * 0.85f));
            float active_amp = open_norm * envelope * (0.45f + 0.55f * wave);
            bar_half_h = 1 + static_cast<int32_t>(std::round(active_amp * 7.5f));
        } else if (now_ms < head_pat_until_ms || (current_emotion == EMOTION_HAPPY && current_pose.mouth_curve_factor > 0.85f)) {
            // Sorriso aberto radiante no centro quando muito feliz ou recebendo carinho
            bar_half_h = (dist_center <= 2) ? 3 : ((dist_center <= 4) ? 2 : 1);
        } else {
            bar_half_h = (i % 2 == 0) ? 2 : 1;
        }

        int32_t bx = left_x + (i * bar_step);
        int32_t cx = bx + (bar_w / 2);
        int32_t top_y = bar_center_y - bar_half_h;
        int32_t bot_y = bar_center_y + bar_half_h;

        uint16_t bar_col = primary_col;
        if (bar_half_h >= 6) {
            bar_col = TFT_WHITE;
        } else if (bar_half_h >= 3) {
            bar_col = COLOR_CYBER_ICE_LIGHT;
        }

        canvas.fillRect(bx, top_y, bar_w, (bar_half_h * 2) + 1, bar_col);
        if (bar_half_h >= 4) {
            canvas.drawFastHLine(bx, top_y - 2, bar_w, TFT_WHITE);
            canvas.drawFastHLine(bx, bot_y + 2, bar_w, TFT_WHITE);
        }

        // Conecta as barras com um feixe labial contínuo que deixa a expressão da boca imediatamente legível
        if (i > 0) {
            canvas.drawLine(prev_cx, prev_top_y, cx, top_y, COLOR_CYBER_ICE_LIGHT);
            canvas.drawLine(prev_cx, prev_bot_y, cx, bot_y, primary_col);
        }
        prev_cx = cx;
        prev_top_y = top_y;
        prev_bot_y = bot_y;
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

    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);

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

    // 2. Corações pixelados e faíscas durante Cafuné / Carinho na cabeça
    if (now_ms < head_pat_until_ms) {
        for (int i = 0; i < 3; ++i) {
            uint32_t phase = (now_ms / 28U + static_cast<uint32_t>(i * 17)) % 36U;
            int32_t hx = mid_x - 28 + (i * 28) + static_cast<int32_t>(std::sin(static_cast<float>(phase) * 0.22f) * 4.0f);
            int32_t hy = (eye_y - 12) - static_cast<int32_t>(phase / 2U);
            if (hy > 34) {
                uint16_t h_col = (i % 2 == 0) ? COLOR_CYBER_ORANGE : TFT_WHITE;
                // Desenha mini-coração pixel art 7x6
                canvas.fillRect(hx - 2, hy, 2, 2, h_col);
                canvas.fillRect(hx + 1, hy, 2, 2, h_col);
                canvas.fillRect(hx - 3, hy + 1, 7, 2, h_col);
                canvas.fillRect(hx - 2, hy + 3, 5, 1, h_col);
                canvas.fillRect(hx - 1, hy + 4, 3, 1, h_col);
                canvas.drawPixel(hx, hy + 5, h_col);
            }
        }
        return;
    }

    // 3. Partículas de Código e Bits discretos de 4px ('0' e '1') subindo nas laterais dos olhos (*Coder Flow*)
    if (cached_audio_data.is_typing_active && current_emotion != EMOTION_SLEEPING) {
        for (int i = 0; i < 6; ++i) {
            uint32_t speed_div = (cached_audio_data.typing_click_rate > 4.0f) ? 24U : 34U;
            uint32_t phase = (now_ms / speed_div + static_cast<uint32_t>(i * 11)) % 44U;
            int32_t side_x = (i % 2 == 0)
                                 ? (left_eye_x - 34 - ((i / 2) * 5))
                                 : (right_eye_x + 31 + ((i / 2) * 5));
            int32_t bit_y = (eye_y + 14) - static_cast<int32_t>(phase);
            if (bit_y > 34 && side_x > 14 && side_x < SCREEN_WIDTH - 18) {
                uint16_t bit_col = (i % 2 == 0) ? COLOR_OPTIC_EMERALD : COLOR_CYBER_ICE_LIGHT;
                bool is_one = (((now_ms / 180U) + static_cast<uint32_t>(i)) % 2U) == 0U;
                if (is_one) {
                    // Glifo discreto '1' de 4px de altura
                    canvas.drawFastVLine(side_x + 1, bit_y, 4, bit_col);
                    canvas.drawPixel(side_x, bit_y + 1, bit_col);
                } else {
                    // Glifo discreto '0' de 3x4px
                    canvas.drawRect(side_x, bit_y, 3, 4, bit_col);
                }
            }
        }
    }

    // 4. Notas Musicais Rítmicas quando há batida de música ativa no ambiente
    if (cached_audio_data.music_beat_active && current_emotion != EMOTION_SLEEPING) {
        for (int i = 0; i < 2; ++i) {
            uint32_t phase = (now_ms / 32U + static_cast<uint32_t>(i * 23)) % 42U;
            int32_t nx = (i == 0) ? (left_eye_x - 32) : (right_eye_x + 28);
            int32_t ny = (eye_y + 4) - static_cast<int32_t>(phase / 2U);
            if (ny > 34 && nx > 16 && nx < SCREEN_WIDTH - 18) {
                uint16_t ncol = (i == 0) ? COLOR_CYBER_ORANGE : TFT_WHITE;
                canvas.fillCircle(nx, ny + 4, 2, ncol);
                canvas.drawFastVLine(nx + 2, ny - 2, 6, ncol);
                canvas.drawFastHLine(nx + 2, ny - 2, 3, ncol);
            }
        }
    }

    // 5. Partículas de Hibernação ("Z" flutuantes ascendentes) em EMOTION_SLEEPING / EMOTION_SLEEPY
    if (current_emotion == EMOTION_SLEEPING || current_emotion == EMOTION_SLEEPY) {
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

    // 6. Emote de Interrogação / Curiosidade ("?") ao lado da sobrancelha arqueada em EMOTION_JUDGING
    if (current_emotion == EMOTION_JUDGING) {
        int32_t qx = right_eye_x + 28;
        int32_t qy = eye_y - 24 + static_cast<int32_t>(std::sin(static_cast<float>(now_ms) * 0.008f) * 2.5f);
        if (qx < SCREEN_WIDTH - 18 && qy > 32) {
            canvas.setTextColor(COLOR_CYBER_ORANGE);
            canvas.setCursor(qx + 1, qy + 1);
            canvas.print("?");
            canvas.setTextColor(TFT_WHITE);
            canvas.setCursor(qx, qy);
            canvas.print("?");
        }
        return;
    }

    // 7. Faíscas Luminosas de Bom Humor / Sorriso em EMOTION_HAPPY
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

    // 8. Ondas Acústicas Concêntricas em EMOTION_LISTENING (Escuta Ativa Hands-Free)
    if (current_emotion == EMOTION_LISTENING) {
        int32_t wave_r = 4 + static_cast<int32_t>((now_ms / 60U) % 12U);
        canvas.drawCircle(mid_x, eye_y, wave_r, primary_col);
        canvas.drawCircle(mid_x, eye_y, (wave_r + 6) % 16, COLOR_CYBER_ICE_LIGHT);
    }
}

void PetController::renderFloatingHudPopups(M5Canvas& canvas, uint32_t now_ms) {
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);

    for (size_t i = 0; i < MAX_HUD_POPUPS; ++i) {
        FloatingHudPopup& p = hud_popups[i];
        if (!p.active) {
            continue;
        }
        if (now_ms >= p.expire_ms || now_ms < p.spawn_ms) {
            p.active = false;
            continue;
        }

        float duration = static_cast<float>(p.expire_ms - p.spawn_ms);
        float progress = (duration > 1.0f) ? (static_cast<float>(now_ms - p.spawn_ms) / duration) : 1.0f;
        int32_t cur_y = static_cast<int32_t>(std::round(p.y - progress * 22.0f));
        if (cur_y < 30) {
            cur_y = 30;
        }

        int32_t text_w = static_cast<int32_t>(std::strlen(p.text)) * 6;
        int32_t draw_x = static_cast<int32_t>(p.x) - (text_w / 2);
        if (draw_x < 14) draw_x = 14;
        if (draw_x + text_w > SCREEN_WIDTH - 14) draw_x = SCREEN_WIDTH - 14 - text_w;

        // Pill tático semi-opaco com borda luminosa para leitura instantânea
        canvas.fillRect(draw_x - 3, cur_y - 2, text_w + 6, 11, COLOR_HUD_PANEL_BG);
        canvas.drawRect(draw_x - 3, cur_y - 2, text_w + 6, 11, p.color);
        canvas.setTextColor(COLOR_TEXT_SHADOW);
        canvas.setCursor(draw_x + 1, cur_y + 1);
        canvas.print(p.text);
        canvas.setTextColor(TFT_WHITE);
        canvas.setCursor(draw_x, cur_y);
        canvas.print(p.text);
    }
}

void PetController::renderPetFace(M5Canvas& canvas) {
    canvas.setSwapBytes(true);
    uint32_t now_ms = M5.millis();

    // 1. Cockpit Sci-Fi 16-bit carregado do SD (ou Grid Cibernético 3D procedural como fallback)
    renderCyberGridBackground(canvas, now_ms);

    int32_t shake_x  = static_cast<int32_t>(current_pose.shake_offset_x);
    int32_t shake_y  = static_cast<int32_t>(current_pose.shake_offset_y);
    int32_t breath_y = static_cast<int32_t>(std::round(current_pose.breathing_offset_y));
    int32_t slouch_y = static_cast<int32_t>(std::round(slouch_drop_y));

    // Quando o visor PIP estiver aberto no quadrante superior direito, desloca suavemente as órbitas para a esquerda
    // para que o olho direito jamais fique coberto pela janela da câmera/timelapse!
    bool pip_active = debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms);
    int32_t base_left_x  = pip_active ? 68  : FACE_EYE_LEFT_X;
    int32_t base_right_x = pip_active ? 168 : FACE_EYE_RIGHT_X;

    // Posição base dos sensores ópticos acompanhando o olhar + respiração orgânica + inclinação lateral da cabeça + espelho de postura
    int32_t face_shift_x = static_cast<int32_t>(current_pose.current_eye_x * 0.40f) + shake_x;
    int32_t face_shift_y = static_cast<int32_t>(current_pose.current_eye_y * 0.30f) + shake_y + breath_y + slouch_y;
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

    // 2.5. Partículas Emocionais Dinâmicas (Anel Neural, Corações de Cafuné, Código Digitando, Notas Musicais, "?" ou "Z")
    renderEmotionalParticles(canvas, left_eye_x, right_eye_x, eye_y, now_ms);

    // 3. Olhos de Lente Óptica HD com Squash & Stretch e Pálpebras Independentes
    renderCombatOpticEye(canvas, left_eye_x, left_eye_y, pupil_dx, pupil_dy, true, now_ms);
    renderCombatOpticEye(canvas, right_eye_x, right_eye_y, pupil_dx, pupil_dy, false, now_ms);

    // 3.5. Bochechas Cibernéticas Luminosas (Blush ///) quando feliz, sorrindo ou recebendo cafuné
    renderCyberCheekBlush(canvas, left_eye_x, right_eye_x, left_eye_y, right_eye_y, now_ms);

    // 4. Sobrancelhas independentes com feixe luminoso na cor do humor atual
    int32_t brow_dist = pip_active ? 22 : 25;
    int32_t left_brow_y  = (left_eye_y - brow_dist) + static_cast<int32_t>(current_pose.left_brow_offset_y);
    int32_t right_brow_y = (right_eye_y - brow_dist) + static_cast<int32_t>(current_pose.right_brow_offset_y);
    if (left_brow_y < 35) left_brow_y = 35;   // Garante que nunca invada o HUD superior (Y <= 29)
    if (right_brow_y < 35) right_brow_y = 35;
    float tilt_brow_mod  = current_pose.head_tilt_deg * 0.30f;
    renderLaserArmorEyebrow(canvas, left_eye_x + 2, left_brow_y, current_pose.left_brow_angle_deg + tilt_brow_mod, true);
    renderLaserArmorEyebrow(canvas, right_eye_x - 2, right_brow_y, current_pose.right_brow_angle_deg - tilt_brow_mod, false);

    current_pose.eye_radius_current = saved_eye_r;

    // 5. Boca de Retículo / Interface de Síntese Vocal centralizada com curvatura emocional, respiração sutil e espelho de postura
    int32_t mouth_x = (SCREEN_WIDTH / 2) + static_cast<int32_t>(current_pose.current_eye_x * 0.30f) + shake_x;
    int32_t mouth_y = FACE_MOUTH_BASE_Y + static_cast<int32_t>(current_pose.current_eye_y * 0.20f) + shake_y + (breath_y / 2) + (slouch_y / 2);
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

    // 2. Célula 2 (X = 91..139): Ícone 16-bit CPU Core (SD) + Indicador de Energia (5 blocos, pulsa no BPM se música ativa)
    constexpr int32_t cpu_x = 92;
    constexpr int32_t cpu_y = 4;
    int32_t hud_bounce_y = cached_audio_data.is_music_active
                               ? static_cast<int32_t>(std::round(bounce_offset_y * 0.5f))
                               : 0;
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
        int32_t sy = cpu_y + 11 + hud_bounce_y;
        uint16_t seg_col = COLOR_HUD_BADGE_BG;
        if (seg < lit_segments) {
            seg_col = (lit_segments >= 2) ? primary_col : COLOR_OPTIC_NEON_RED;
        }
        canvas.fillRect(sx, sy, 7, 6, seg_col);
        canvas.drawRect(sx, sy, 7, 6, COLOR_CYBER_GRID_BLUE);
    }

    // 3. Célula 3 (X = 146..193): Ícone 16-bit de Mira/Foco (SD) + Tempo de Foco / Pomodoro
    constexpr int32_t foc_x = 154;
    int32_t foc_y = 14 + hud_bounce_y;
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
    // VU Meters de Áudio Laterais: colunas de 11 blocos de LEDs virtuais empilhados nas margens esquerda e direita + Peak-Hold
    constexpr int32_t num_leds = 11;
    constexpr int32_t led_w = 7;
    constexpr int32_t led_h = 6;
    constexpr int32_t led_pitch = 9;
    int32_t vu_bounce_y = audio_data.is_music_active
                              ? static_cast<int32_t>(std::round(bounce_offset_y))
                              : 0;
    int32_t bottom_y = 136 + vu_bounce_y;
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

    // Leve variação entre canal esquerdo e direito em picos altos
    int32_t right_lit = lit_count;
    if (lit_count > 3 && ((now_ms / 90U) % 2U == 1U)) {
        right_lit = (lit_count > 2) ? (lit_count - 1) : lit_count;
    }

    // Atualiza Peak-Hold com queda suave por gravidade a cada 95ms
    if (static_cast<float>(lit_count - 1) > vu_peak_left) {
        vu_peak_left = static_cast<float>(lit_count - 1);
    }
    if (static_cast<float>(right_lit - 1) > vu_peak_right) {
        vu_peak_right = static_cast<float>(right_lit - 1);
    }
    if (now_ms - last_vu_decay_ms >= 95U) {
        last_vu_decay_ms = now_ms;
        if (vu_peak_left > 1.0f)  vu_peak_left  -= 0.35f;
        if (vu_peak_right > 1.0f) vu_peak_right -= 0.35f;
    }
    int32_t peak_idx_l = static_cast<int32_t>(std::round(vu_peak_left));
    int32_t peak_idx_r = static_cast<int32_t>(std::round(vu_peak_right));

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

        uint16_t col_l = (i < lit_count) ? active_col : ((i == peak_idx_l && peak_idx_l >= 2) ? COLOR_CYBER_ORANGE : COLOR_HUD_BADGE_BG);
        uint16_t col_r = (i < right_lit) ? active_col : ((i == peak_idx_r && peak_idx_r >= 2) ? COLOR_CYBER_ORANGE : COLOR_HUD_BADGE_BG);

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

void PetController::renderShiftDebriefOverlay(M5Canvas& canvas, uint32_t now_ms) {
    if (now_ms >= debrief_overlay_until_ms) {
        return;
    }

    constexpr int32_t mx = 18;
    constexpr int32_t my = 32;
    constexpr int32_t mw = SCREEN_WIDTH - 36;
    constexpr int32_t mh = 112;

    uint16_t primary_col = getActiveOpticPrimaryColor();
    const UserHabitMatrix& hm = oracle_brain.getHabitMatrix();

    canvas.fillRect(mx, my, mw, mh, COLOR_HUD_PANEL_BG);
    canvas.drawRect(mx, my, mw, mh, COLOR_CYBER_ORANGE);
    canvas.drawRect(mx + 2, my + 2, mw - 4, mh - 4, primary_col);

    // Faixa de título superior do Debriefing
    canvas.fillRect(mx + 3, my + 3, mw - 6, 16, COLOR_HUD_BADGE_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_CYBER_ORANGE);
    canvas.setCursor(mx + 10, my + 7);
    canvas.print("RESUMO DA MISSAO DIARIA [ENCERRAMENTO]");

    uint32_t rem_sec = (debrief_overlay_until_ms - now_ms + 999U) / 1000U;
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(mx + mw - 28, my + 7);
    canvas.printf("%us", static_cast<unsigned int>(rem_sec));

    int32_t row_x = mx + 10;
    int32_t row_y = my + 24;
    constexpr int32_t row_step = 16;

    // 1. Nível de RPG e XP atual
    canvas.setTextColor(COLOR_SONAR_GREEN);
    canvas.setCursor(row_x, row_y);
    canvas.printf("> NIVEL RPG: LV.%02u  |  XP ACUMULADO: %u pts",
                  static_cast<unsigned int>(hm.cyber_level),
                  static_cast<unsigned int>(hm.xp_points));

    // 2. Minutos de Foco acumulados
    canvas.setTextColor(COLOR_CYBER_ICE_LIGHT);
    canvas.setCursor(row_x, row_y + row_step);
    canvas.printf("> TEMPO DE FOCO ACUMULADO: %u min",
                  static_cast<unsigned int>(internal_stats.focus_minutes));

    // 3. Cafés / Águas registrados (Hidratação & Lanches Visuais)
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(row_x, row_y + row_step * 2);
    canvas.printf("> CAFES / AGUAS REGISTRADOS: %u recargas",
                  static_cast<unsigned int>(hm.total_visual_feedings));

    // 4. Alertas de Postura "Camarão"
    canvas.setTextColor(hm.total_posture_warnings > 0U ? COLOR_CYBER_ORANGE : COLOR_SONAR_GREEN);
    canvas.setCursor(row_x, row_y + row_step * 3);
    canvas.printf("> ALERTAS POSTURA 'CAMARAO': %u ocorrencias",
                  static_cast<unsigned int>(hm.total_posture_warnings));

    // 5. Commits / Pomodoros concluídos
    canvas.setTextColor(primary_col);
    canvas.setCursor(row_x, row_y + row_step * 4);
    canvas.printf("> COMMITS HOJE: %u  |  POMODOROS CONCLUIDOS: %u",
                  static_cast<unsigned int>(git_commits_today),
                  static_cast<unsigned int>(hm.total_pomodoros_completed));
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
    if (!rpg_panel_open && (now_ms >= debrief_overlay_until_ms) &&
        (pip_hd_zoom_active || debug_mode || oracle_brain.isWaitingAi() || timelapse_active || (now_ms < pip_display_until_ms))) {
        renderCyberPipVisor(canvas, gaze_data, now_ms);
    }

    // 2.8. Modal Interativo de RPG & Matriz de Hábitos 24h (Tecla [TAB] ou clique no Badge de Nível)
    if (rpg_panel_open) {
        renderRpgAndHabitsModal(canvas, now_ms);
    } else if (now_ms < debrief_overlay_until_ms) {
        // 2.9. Overlay temporário (8s) de Ritual de Encerramento de Expediente ("RESUMO DA MISSAO DIARIA")
        renderShiftDebriefOverlay(canvas, now_ms);
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

    // 3.8. Popups flutuantes de feedback instantâneo (+XP, +HUMOR, CAFUNÉ, FOCO)
    renderFloatingHudPopups(canvas, now_ms);

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
