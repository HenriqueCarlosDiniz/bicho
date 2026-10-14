#ifndef PET_CONTROLLER_INTERNAL_H
#define PET_CONTROLLER_INTERNAL_H

// =============================================================================
// Cabeçalho interno compartilhado pelos arquivos de implementação do PetController:
//   pet_controller.cpp (estado/regras), pet_assets.cpp (assets SD), pet_voice.cpp (voz/lip-sync)
//   e pet_renderer.cpp (rosto/HUD). Não incluir fora desses arquivos.
// =============================================================================

#include "pet_state.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
constexpr uint16_t COLOR_CYBER_GREEN     = 0x07E8;   // Verde cibernético para popups de XP/afeto
constexpr uint16_t COLOR_OPTIC_EMERALD   = 0x07E8;   // Verde esmeralda óptico para partículas e bochechas
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


#endif // PET_CONTROLLER_INTERNAL_H
