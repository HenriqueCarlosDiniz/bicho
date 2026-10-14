#include "oracle_brain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#if defined(ARDUINO)
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ctime>
#else
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#endif

// =============================================================================
// Banco Contextual de Contingência Offline (getFallbackOfflineResponse)
// =============================================================================
struct OfflineFallbackEntry {
    const char* reply_text;
    PetEmotionState target_emotion;
    int32_t mood_delta;
    int32_t energy_delta;
    const char* new_memory;
};

static const OfflineFallbackEntry OFFLINE_SARCASTIC_BANK[] = {
    {
        "Se essa reunião durar mais dez minutos, até meu coprocessador vai pedir demissão.",
        EMOTION_JUDGING, +4, -4,
        "Sobreviveu a mais uma reunião interminável com bom humor"
    },
    {
        "Você chama isso de código limpo ou é arte abstrata? Beba água.",
        EMOTION_JUDGING, +5, +5,
        "Foi julgado com ironia amigável pelo código"
    },
    {
        "Mais uma xícara de café? Seu coração já está rodando em overclock de 240MHz.",
        EMOTION_HAPPY, +8, +8,
        "Suspeita de excesso de cafeína na mesa"
    },
    {
        "Olha só, um clique! Quase esqueci que você me deixa vigiando sua mesa de graça.",
        EMOTION_HAPPY, +12, +4,
        "Lembrou que o Oráculo existe na mesa"
    },
    {
        "Cinco abas abertas e nenhum commit ainda? Minha RAM torce por você.",
        EMOTION_FOCUSED, +4, +2,
        "Concentrado olhando para a tela antes de commitar"
    },
    {
        "Boa postura! Brincadeira, você parece um camarão curvado na frente do monitor.",
        EMOTION_JUDGING, +6, +2,
        "Postura de camarão detectada pelo sensor óptico"
    }
};
constexpr uint8_t OFFLINE_BANK_COUNT = sizeof(OFFLINE_SARCASTIC_BANK) / sizeof(OFFLINE_SARCASTIC_BANK[0]);

// =============================================================================
// Implementação da Classe OracleBrain
// =============================================================================

OracleBrain::OracleBrain()
    : recent_memories(),
      recent_replies(),
      permanent_profile_facts(),
      daily_summaries(),
      total_episodic_records(0),
      sd_storage_ready(false),
      trigger_reason("TOUCH_POKE"),
      fallback_cursor(0),
      creative_lens_cursor(0),
      is_waiting_ai(false),
      has_pending_response(false)
{
    std::memset(&habit_matrix, 0, sizeof(habit_matrix));
    habit_matrix.cyber_level = 1;
    latest_response = {
        "",
        EMOTION_JUDGING,
        0,
        0,
        0,
        "",
        "",
        "",
        false
    };
}

OracleBrain::~OracleBrain() {
#if !defined(ARDUINO)
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
#endif
}

void OracleBrain::initBrain() {
    sd_storage_ready = initSdCardDirectories();

    // Carrega Perfil Permanente (Nível 1), Matriz de Hábitos (Nível 4) e Histórico de Falas no Cartão SD
    if (sd_storage_ready) {
        if (!loadPermanentProfileFromSd()) {
            permanent_profile_facts.clear();
            permanent_profile_facts.push_back("Criador de sistemas embarcados C++ / M5Stack CoreS3");
            savePermanentProfileToSd();
        }
        if (!loadHabitMatrixFromSd()) {
            std::memset(&habit_matrix, 0, sizeof(habit_matrix));
            habit_matrix.cyber_level = 1;
            saveHabitMatrixToSd();
        }
        loadRecentRepliesFromSpeechLog();
    }

    // Carrega memórias episódicas (Nível 2) ou migra automaticamente de oracle_memories.json
    if (!loadMemoriesFromStorage()) {
        recent_memories.clear();
        addStructuredMemory(
            "Inicializado no cubo M5Stack CoreS3 com memória cognitiva em cartão SD",
            "",
            "sistema,boot,sd",
            "BOOT_INIT",
            75
        );
    }

    // Consolida resumos de dias anteriores (Nível 3: Daily Summaries)
    if (sd_storage_ready) {
        consolidatePreviousDaysLogs();
    }
}

std::string OracleBrain::resolveApiKey() const {
    if (AI_API_KEY != nullptr && std::strlen(AI_API_KEY) > 10 &&
        std::strcmp(AI_API_KEY, "SUA_CHAVE_DE_API_MULTIMODAL_AQUI") != 0) {
        return std::string(AI_API_KEY);
    }
#if !defined(ARDUINO)
    const char* env_key = std::getenv("GEMINI_API_KEY");
    if (env_key != nullptr && std::strlen(env_key) > 10) {
        return std::string(env_key);
    }
    const char* alt_env_key = std::getenv("AI_API_KEY");
    if (alt_env_key != nullptr && std::strlen(alt_env_key) > 10) {
        return std::string(alt_env_key);
    }
#endif
    // Fallback para chave configurada no cartão SD (sd_card/config.json)
    FILE* cfg = std::fopen(SD_CONFIG_FILE_PATH, "r");
    if (cfg != nullptr) {
        char buf[1024] = {0};
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, cfg);
        std::fclose(cfg);
        buf[n] = '\0';
        std::string sd_key;
        if (extractJsonStringField(std::string(buf), "gemini_api_key", sd_key) && sd_key.size() > 10) {
            return sd_key;
        }
    }
    return "";
}

std::string OracleBrain::escapeJsonString(const std::string& raw_str) const {
    std::string escaped;
    escaped.reserve(raw_str.size() + 16);
    for (char c : raw_str) {
        switch (c) {
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:
                if (static_cast<uint8_t>(c) >= 0x20U) {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

const char* OracleBrain::emotionToCodeString(PetEmotionState emotion_state) const {
    switch (emotion_state) {
        case EMOTION_IDLE:      return "EMOTION_IDLE";
        case EMOTION_FOCUSED:   return "EMOTION_FOCUSED";
        case EMOTION_STARTLED:  return "EMOTION_STARTLED";
        case EMOTION_SLEEPING:  return "EMOTION_SLEEPING";
        case EMOTION_SLEEPY:    return "EMOTION_SLEEPING";
        case EMOTION_JUDGING:   return "EMOTION_JUDGING";
        case EMOTION_HAPPY:     return "EMOTION_HAPPY";
        case EMOTION_GRUMPY:    return "EMOTION_GRUMPY";
        case EMOTION_DIZZY:     return "EMOTION_DIZZY";
        case EMOTION_LISTENING: return "EMOTION_FOCUSED";
        case EMOTION_NEUTRAL:
        default:                return "EMOTION_IDLE";
    }
}

PetEmotionState OracleBrain::parseEmotionString(const std::string& emotion_str) const {
    if (emotion_str.find("EMOTION_HAPPY") != std::string::npos)    return EMOTION_HAPPY;
    if (emotion_str.find("EMOTION_GRUMPY") != std::string::npos)   return EMOTION_GRUMPY;
    if (emotion_str.find("EMOTION_JUDGING") != std::string::npos)  return EMOTION_JUDGING;
    if (emotion_str.find("EMOTION_FOCUSED") != std::string::npos)  return EMOTION_FOCUSED;
    if (emotion_str.find("EMOTION_STARTLED") != std::string::npos) return EMOTION_STARTLED;
    if (emotion_str.find("EMOTION_SLEEPING") != std::string::npos) return EMOTION_SLEEPING;
    if (emotion_str.find("EMOTION_DIZZY") != std::string::npos)    return EMOTION_DIZZY;
    if (emotion_str.find("EMOTION_IDLE") != std::string::npos)     return EMOTION_IDLE;
    return EMOTION_JUDGING;
}

std::string OracleBrain::truncateUtf8Reply(const std::string& text_utf8, size_t max_chars) const {
    size_t char_count = 0;
    size_t byte_pos = 0;
    while (byte_pos < text_utf8.size() && char_count < max_chars) {
        uint8_t lead = static_cast<uint8_t>(text_utf8[byte_pos]);
        size_t char_bytes = 1;
        if ((lead & 0xE0U) == 0xC0U) char_bytes = 2;
        else if ((lead & 0xF0U) == 0xE0U) char_bytes = 3;
        else if ((lead & 0xF8U) == 0xF0U) char_bytes = 4;

        if (byte_pos + char_bytes > text_utf8.size()) {
            break;
        }
        byte_pos += char_bytes;
        ++char_count;
    }
    return text_utf8.substr(0, byte_pos);
}

std::string OracleBrain::buildTelemetryJsonString(const OracleContextPacket& context_packet) const {
    char telemetry_buf[560];
    std::snprintf(
        telemetry_buf,
        sizeof(telemetry_buf),
        "{\"current_emotion\":\"%s\",\"mood_score\":%d,\"energy_level\":%d,\"hunger_level\":%d,"
        "\"focus_minutes\":%u,\"noise_level_db\":%.1f,\"is_user_present\":%s,"
        "\"is_in_meeting\":%s,\"is_charging\":%s,\"vad_keyword_mode\":%s,\"trigger_reason\":\"%s\"",
        emotionToCodeString(context_packet.current_emotion),
        static_cast<int>(context_packet.mood_score),
        static_cast<int>(context_packet.energy_level),
        static_cast<int>(context_packet.hunger_level),
        static_cast<unsigned int>(context_packet.focus_minutes),
        context_packet.noise_level_db,
        context_packet.is_user_present ? "true" : "false",
        context_packet.is_in_meeting ? "true" : "false",
        context_packet.is_charging ? "true" : "false",
        context_packet.vad_keyword_mode ? "true" : "false",
        escapeJsonString(context_packet.trigger_reason).c_str()
    );

    std::string telemetry_json(telemetry_buf);
    if (!context_packet.external_world_summary.empty()) {
        telemetry_json += ",\"external_world_summary\":\"" +
                          escapeJsonString(context_packet.external_world_summary) + "\"";
    }
    if (!context_packet.host_and_posture_summary.empty()) {
        telemetry_json += ",\"host_and_posture_summary\":\"" +
                          escapeJsonString(context_packet.host_and_posture_summary) + "\"";
    }

    // Injeção da Matriz Estatística de Hábitos do Cartão SD
    std::string habit_summary = buildHabitMatrixSummary();
    if (!habit_summary.empty()) {
        telemetry_json += ",\"user_habit_profile\":\"" + escapeJsonString(habit_summary) + "\"";
    }

    // Nível 1 do SD: Perfil Permanente do Usuário
    if (!permanent_profile_facts.empty()) {
        telemetry_json += ",\"permanent_user_profile\":[";
        for (size_t i = 0; i < permanent_profile_facts.size(); ++i) {
            if (i > 0) telemetry_json += ",";
            telemetry_json += "\"" + escapeJsonString(permanent_profile_facts[i]) + "\"";
        }
        telemetry_json += "]";
    }

    // Nível 3 do SD: Resumos Diários Consolidados
    if (!daily_summaries.empty()) {
        telemetry_json += ",\"daily_summaries\":[";
        for (size_t i = 0; i < daily_summaries.size(); ++i) {
            if (i > 0) telemetry_json += ",";
            char sum_buf[220];
            std::snprintf(sum_buf, sizeof(sum_buf), "[%s | %u eventos | humor %d%%] %s",
                          daily_summaries[i].date_str.c_str(),
                          static_cast<unsigned int>(daily_summaries[i].total_events),
                          static_cast<int>(daily_summaries[i].avg_mood_score),
                          daily_summaries[i].highlight_fact.c_str());
            telemetry_json += "\"" + escapeJsonString(sum_buf) + "\"";
        }
        telemetry_json += "]";
    }

    // Nível 2 + Micro-RAG no Cartão SD: Busca memórias antigas relevantes para o contexto atual
    std::vector<std::string> rag_matches = retrieveRelevantMemories(context_packet);
    if (!rag_matches.empty()) {
        telemetry_json += ",\"relevant_past_memories_sd\":[";
        for (size_t i = 0; i < rag_matches.size(); ++i) {
            if (i > 0) telemetry_json += ",";
            telemetry_json += "\"" + escapeJsonString(rag_matches[i]) + "\"";
        }
        telemetry_json += "]";
    }

    telemetry_json += ",\"recent_memories\":[";
    for (size_t i = 0; i < recent_memories.size(); ++i) {
        if (i > 0) {
            telemetry_json += ",";
        }
        telemetry_json += "\"" + escapeJsonString(recent_memories[i]) + "\"";
    }
    telemetry_json += "]";

    // Lista de falas recentes proibidas para impedir loops e repetição de piadas/temas
    if (!recent_replies.empty()) {
        telemetry_json += ",\"forbidden_recent_replies_do_not_repeat\":[";
        for (size_t i = 0; i < recent_replies.size(); ++i) {
            if (i > 0) telemetry_json += ",";
            telemetry_json += "\"" + escapeJsonString(recent_replies[i]) + "\"";
        }
        telemetry_json += "]";
    }

    // Lente Criativa Rotativa + Diretriz específica do gatilho para garantir alta variedade temática
    static const char* const CREATIVE_LENSES[] = {
        "Estilo Narrador da Vida Selvagem (National Geographic): descreva o humano na bancada como um espécime raro em seu habitat natural.",
        "Estilo IA de Nave Espacial Sci-Fi (TARS/HAL-9000 bem-humorado): trate o quarto como uma estação orbital e comente telemetria ou clima externo.",
        "Estilo Engenheiro de Corrida de Fórmula 1 no rádio (Box, Box!): trate a sessão de trabalho, postura ou hidratação como estratégia de pista e desgaste de pneus.",
        "Estilo Detetive Noir Cyberpunk: faça uma observação perspicaz sobre a expressão facial na câmera, a iluminação do ambiente ou o mistério na tela.",
        "Estilo Mestre de RPG / Crônicas Épicas: narre o momento atual do humano (nível RPG, foco, clima ou trivia geek) como uma quest lendária.",
        "Estilo Crítico Cultural & Geek Sagaz: conecte a manchete do HackerNews, a trivia geek do dia ou o dólar/Bitcoin com a cara do humano na câmera.",
        "Estilo Sommelier de Cafeína & Ergonomia: avalie o nível de energia visual do humano, se há caneca/copo na mesa e sugira um pit-stop digno.",
        "Estilo Filósofo Existencialista de Silício: reflita com ironia leve sobre viver num cubo de 2 polegadas observando humanos digitarem."
    };
    constexpr uint32_t LENS_COUNT = sizeof(CREATIVE_LENSES) / sizeof(CREATIVE_LENSES[0]);
    const char* chosen_lens = CREATIVE_LENSES[creative_lens_cursor % LENS_COUNT];
    ++creative_lens_cursor;

    std::string trigger_focus = "Comente algo visualmente curioso da câmera ou do mundo externo.";
    if (context_packet.trigger_reason == "DAILY_BRIEFING") {
        trigger_focus = "BRIEFING DO DIA: Comente de forma criativa o clima externo, cotação do Dólar/BTC, a notícia do HackerNews ou a Trivia Geek! NÃO fale de compilador.";
    } else if (context_packet.trigger_reason == "TOUCH_POKE") {
        trigger_focus = "CUTUCÃO NA TELA: O humano clicou em você! Observe a expressão dele na câmera, o ambiente ao fundo ou faça uma piada sobre o toque físico.";
    } else if (context_packet.trigger_reason == "VOICE_HANDS_FREE" || context_packet.trigger_reason == "VOICE_COMMAND") {
        if (context_packet.vad_keyword_mode && context_packet.trigger_reason == "VOICE_HANDS_FREE") {
            trigger_focus = "VOZ HANDS-FREE (MODO PALAVRA-CHAVE 'BICHO'): Se o humano chamou por 'Bicho', 'Ei Bicho', 'Oráculo', 'Cubo' ou falou claramente com você, responda diretamente ao que ele disse com sagacidade! Se for conversa paralela com outra pessoa ou ruído sem falar com você, retorne reply_text='IGNORE'.";
        } else {
            trigger_focus = "VOZ DO HUMANO: Responda diretamente ao que o humano acabou de dizer no áudio (ou à expressão dele na câmera) com sagacidade inédita!";
        }
    } else if (context_packet.trigger_reason == "OVERWORK") {
        trigger_focus = "EXCESSO DE FOCO: Convide o humano para esticar as pernas ou beber água usando uma metáfora totalmente nova.";
    } else if (context_packet.trigger_reason == "PHONE_DOOMSCROLL") {
        trigger_focus = "OLHANDO CELULAR: Provoque com bom humor o humano hipnotizado pela telinha vertical.";
    } else if (context_packet.trigger_reason == "BAD_POSTURE") {
        trigger_focus = "POSTURA: Brinque de forma criativa com a geometria da coluna do humano na câmera.";
    } else if (context_packet.trigger_reason == "OBJECT_INSPECTION") {
        trigger_focus = "INSPEÇÃO DE OBJETO NA LENTE: O humano aproximou um objeto da câmera (caneca de café, copo d'água, lanche, placa de hardware ou celular)! Identifique visualmente o que ele está mostrando na foto e reaja com entusiasmo geek.";
    } else if (context_packet.trigger_reason == "INTRUDER_DETECTED") {
        trigger_focus = "INTRUSO / SEGUNDO ROSTO NA CÂMERA: Há 2 ou mais pessoas no enquadramento! Comente sobre o visitante/espião atrás do humano ou pergunte se é sessão de pair programming.";
    } else if (context_packet.trigger_reason == "YAWN_FATIGUE") {
        trigger_focus = "BOCEJO / FADIGA VISUAL: O humano acabou de bocejar na câmera! Sugira café expresso, água gelada ou um cochilo tático antes que ele durma no teclado.";
    } else if (context_packet.trigger_reason == "NIGHT_OWL_DARK") {
        trigger_focus = "MODO CORUJÃO (QUARTO ESCURO): A iluminação caiu (< 25 lux)! Comente sobre programar nas sombras como um hacker de filme ou sugira ligar uma luz indireta.";
    }

    telemetry_json += ",\"creative_lens_directive\":\"" +
                      escapeJsonString(std::string(chosen_lens) + " | FOCO DO GATILHO: " + trigger_focus) + "\"}";
    return telemetry_json;
}

std::string OracleBrain::buildGeminiRequestPayload(const OracleContextPacket& context_packet) const {
    std::string telemetry_context = buildTelemetryJsonString(context_packet);
    std::string escaped_system = escapeJsonString(ORACLE_SYSTEM_PROMPT);
    std::string escaped_telemetry = escapeJsonString(telemetry_context);

    std::string payload = "{";
    payload += "\"system_instruction\":{\"parts\":[{\"text\":\"" + escaped_system + "\"}]},";
    payload += "\"contents\":[{\"role\":\"user\",\"parts\":[";
    payload += "{\"text\":\"TELEMETRIA_ATUAL_E_MEMORIA: " + escaped_telemetry + "\"}";

    // Anexa snapshot de câmera em Base64 se disponível (Percepção Multimodal)
    if (!context_packet.jpeg_base64.empty()) {
        payload += ",{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + context_packet.jpeg_base64 + "\"}}";
    }

    // Anexa trecho de áudio em Base64 se disponível
    if (!context_packet.wav_base64.empty()) {
        payload += ",{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"" + context_packet.wav_base64 + "\"}}";
    }

    payload += "]}],";
    payload += "\"generationConfig\":{\"response_mime_type\":\"application/json\",\"temperature\":1.0,\"maxOutputTokens\":220}";
    payload += "}";
    return payload;
}

static void appendUnicodeCodepointUtf8(uint32_t cp, std::string& out) {
    if (cp <= 0x7FU) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FFU) {
        out += static_cast<char>(0xC0U | ((cp >> 6) & 0x1FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    } else if (cp <= 0xFFFFU) {
        out += static_cast<char>(0xE0U | ((cp >> 12) & 0x0FU));
        out += static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    } else if (cp <= 0x10FFFFU) {
        out += static_cast<char>(0xF0U | ((cp >> 18) & 0x07U));
        out += static_cast<char>(0x80U | ((cp >> 12) & 0x3FU));
        out += static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    }
}

static bool parseHex4Digits(const std::string& s, size_t pos, uint32_t& out_val) {
    if (pos + 4 > s.size()) return false;
    uint32_t val = 0;
    for (size_t k = 0; k < 4; ++k) {
        char ch = s[pos + k];
        val <<= 4;
        if (ch >= '0' && ch <= '9') val |= static_cast<uint32_t>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') val |= static_cast<uint32_t>(10 + (ch - 'a'));
        else if (ch >= 'A' && ch <= 'F') val |= static_cast<uint32_t>(10 + (ch - 'A'));
        else return false;
    }
    out_val = val;
    return true;
}

bool OracleBrain::extractJsonStringField(const std::string& json_src, const char* field_key, std::string& out_val) const {
    std::string pattern = std::string("\"") + field_key + "\"";
    size_t key_pos = json_src.find(pattern);
    if (key_pos == std::string::npos) {
        return false;
    }
    size_t colon_pos = json_src.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    size_t quote_start = json_src.find('\"', colon_pos + 1);
    if (quote_start == std::string::npos) {
        return false;
    }

    std::string result;
    size_t i = quote_start + 1;
    while (i < json_src.size()) {
        char c = json_src[i];
        if (c == '\\' && (i + 1) < json_src.size()) {
            char next_c = json_src[i + 1];
            // Trata \\uXXXX (duplo escape vindo de JSON aninhado) ou \uXXXX direto
            if (next_c == 'u' && (i + 6) <= json_src.size()) {
                uint32_t cp = 0;
                if (parseHex4Digits(json_src, i + 2, cp)) {
                    appendUnicodeCodepointUtf8(cp, result);
                    i += 6;
                    continue;
                }
            } else if (next_c == '\\' && (i + 7) <= json_src.size() && json_src[i + 2] == 'u') {
                uint32_t cp = 0;
                if (parseHex4Digits(json_src, i + 3, cp)) {
                    appendUnicodeCodepointUtf8(cp, result);
                    i += 7;
                    continue;
                }
            }

            if (next_c == '\"' || next_c == '\\' || next_c == '/') {
                result += next_c;
                i += 2;
                continue;
            } else if (next_c == 'n') {
                result += ' ';
                i += 2;
                continue;
            } else if (next_c == 'r' || next_c == 't') {
                i += 2;
                continue;
            }
        }
        if (c == '\"') {
            out_val = result;
            return true;
        }
        result += c;
        ++i;
    }
    return false;
}

bool OracleBrain::extractJsonIntField(const std::string& json_src, const char* field_key, int32_t& out_val) const {
    std::string pattern = std::string("\"") + field_key + "\"";
    size_t key_pos = json_src.find(pattern);
    if (key_pos == std::string::npos) {
        return false;
    }
    size_t colon_pos = json_src.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    size_t num_pos = colon_pos + 1;
    while (num_pos < json_src.size() &&
           (json_src[num_pos] == ' ' || json_src[num_pos] == '\t' ||
            json_src[num_pos] == '\n' || json_src[num_pos] == '\r' || json_src[num_pos] == '+')) {
        ++num_pos;
    }
    if (num_pos >= json_src.size()) {
        return false;
    }
    char* end_ptr = nullptr;
    long parsed_v = std::strtol(json_src.c_str() + num_pos, &end_ptr, 10);
    if (end_ptr == json_src.c_str() + num_pos) {
        return false;
    }
    out_val = static_cast<int32_t>(parsed_v);
    return true;
}

bool OracleBrain::parseGeminiStructuredJson(const std::string& raw_api_response, OracleBrainResponse& out_response) const {
    if (raw_api_response.empty()) {
        return false;
    }

    if (raw_api_response.find("\"error\"") != std::string::npos) {
        std::string err_msg;
        extractJsonStringField(raw_api_response, "message", err_msg);
        M5_LOGW("[OracleBrain] Erro retornado pela API Gemini: %s", err_msg.c_str());
        return false;
    }

    // 1. O Gemini retorna um envelope JSON com candidates[0].content.parts[0].text contendo o JSON estruturado,
    // ou pode ser um JSON direto caso venha de mock/teste.
    std::string inner_json = raw_api_response;
    std::string extracted_part_text;
    if (extractJsonStringField(raw_api_response, "text", extracted_part_text) &&
        extracted_part_text.find("reply_text") != std::string::npos) {
        inner_json = extracted_part_text;
    }

    std::string parsed_reply;
    if (!extractJsonStringField(inner_json, "reply_text", parsed_reply) || parsed_reply.empty()) {
        return false;
    }

    std::string parsed_emotion_str = "EMOTION_JUDGING";
    extractJsonStringField(inner_json, "target_emotion", parsed_emotion_str);

    int32_t parsed_mood_delta = 0;
    int32_t parsed_energy_delta = 0;
    int32_t parsed_hunger_delta = 0;
    extractJsonIntField(inner_json, "mood_delta", parsed_mood_delta);
    extractJsonIntField(inner_json, "energy_delta", parsed_energy_delta);
    extractJsonIntField(inner_json, "hunger_delta", parsed_hunger_delta);

    std::string parsed_memory;
    extractJsonStringField(inner_json, "new_memory", parsed_memory);

    std::string parsed_perm_fact;
    extractJsonStringField(inner_json, "permanent_fact", parsed_perm_fact);

    std::string parsed_tags;
    extractJsonStringField(inner_json, "memory_tags", parsed_tags);

    out_response.reply_text = truncateUtf8Reply(parsed_reply, MAX_REPLY_CHARS);
    out_response.target_emotion = parseEmotionString(parsed_emotion_str);
    out_response.mood_delta = parsed_mood_delta;
    out_response.energy_delta = parsed_energy_delta;
    out_response.hunger_delta = parsed_hunger_delta;
    out_response.new_memory = truncateUtf8Reply(parsed_memory, 80);
    out_response.permanent_fact = truncateUtf8Reply(parsed_perm_fact, 90);
    out_response.memory_tags = truncateUtf8Reply(parsed_tags, 60);
    out_response.from_cloud_api = true;

    // Registra a fala nas últimas MAX_RECENT_REPLIES (20) respostas para bloquear repetição de piadas/temas nos próximos turnos
    if (!out_response.reply_text.empty() && out_response.reply_text != "IGNORE" && out_response.reply_text != "[IGNORE]") {
        if (recent_replies.empty() || recent_replies.back() != out_response.reply_text) {
            if (recent_replies.size() >= MAX_RECENT_REPLIES) {
                recent_replies.erase(recent_replies.begin());
            }
            recent_replies.push_back(out_response.reply_text);
        }
    }

    M5_LOGI("[OracleBrain] Resposta Gemini (Flash-Lite) recebida: \"%s\" [%s] | PermFact='%s' | Tags='%s'",
            out_response.reply_text.c_str(),
            parsed_emotion_str.c_str(),
            out_response.permanent_fact.c_str(),
            out_response.memory_tags.c_str());
    return true;
}

bool OracleBrain::executePlatformHttpsPost(const std::string& request_payload_json, std::string& out_raw_response) const {
    std::string api_key = resolveApiKey();
    if (api_key.empty()) {
        return false;
    }

    const char* endpoint_url = (AI_API_ENDPOINT != nullptr && std::strlen(AI_API_ENDPOINT) > 10)
                                   ? AI_API_ENDPOINT
                                   : GEMINI_API_ENDPOINT;

#if defined(ARDUINO)
    // No M5Stack CoreS3 (ESP32-S3): usa WiFiClientSecure (setInsecure) + HTTPClient
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClientSecure secure_client;
    secure_client.setInsecure();
    HTTPClient http_client;

    std::string full_url = std::string(endpoint_url) + "?key=" + api_key;
    if (!http_client.begin(secure_client, full_url.c_str())) {
        return false;
    }

    http_client.addHeader("Content-Type", "application/json");
    http_client.setTimeout(8500);
    int http_code = http_client.POST(request_payload_json.c_str());
    if (http_code == HTTP_CODE_OK) {
        String response_str = http_client.getString();
        out_raw_response = std::string(response_str.c_str());
        http_client.end();
        return !out_raw_response.empty();
    }
    http_client.end();
    return false;
#else
    // No Emulador de PC (native_pc): grava gemini_req.json e invoca curl via linha de comando
    {
        std::ofstream req_file(GEMINI_REQ_TEMP_FILE, std::ios::binary | std::ios::trunc);
        if (!req_file.is_open()) {
            return false;
        }
        req_file.write(request_payload_json.data(), static_cast<std::streamsize>(request_payload_json.size()));
    }

    std::remove(GEMINI_RESP_TEMP_FILE);

    std::string curl_cmd =
        "curl -s -m 15 -X POST -H \"Content-Type: application/json\" --data-binary @" +
        std::string(GEMINI_REQ_TEMP_FILE) + " \"" +
        std::string(endpoint_url) + "?key=" + api_key + "\" -o " +
        std::string(GEMINI_RESP_TEMP_FILE) + " >nul 2>&1";

    int exit_code = std::system(curl_cmd.c_str());
    if (exit_code != 0) {
        return false;
    }

    std::ifstream resp_file(GEMINI_RESP_TEMP_FILE, std::ios::binary);
    if (!resp_file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << resp_file.rdbuf();
    out_raw_response = ss.str();
    return !out_raw_response.empty();
#endif
}

OracleBrainResponse OracleBrain::getFallbackOfflineResponse(const OracleContextPacket& context_packet) {
    OracleBrainResponse fallback;
    fallback.from_cloud_api = false;
    fallback.hunger_delta = 0;
    fallback.permanent_fact = "";

    if (context_packet.trigger_reason == "BAD_POSTURE") {
        fallback.reply_text = "Postura de camarão detectada na câmera! Endireita essa coluna antes que vire legado.";
        fallback.target_emotion = EMOTION_JUDGING;
        fallback.mood_delta = -2;
        fallback.energy_delta = 0;
        fallback.new_memory = "Curvou a coluna na frente do monitor (modo camarão)";
        fallback.memory_tags = "postura,coluna,camarao";
        return fallback;
    }

    if (context_packet.trigger_reason == "OBJECT_INSPECTION") {
        fallback.reply_text = "Opa, objeto detectado na lente! Se for café ou lanche, considero meus sensores devidamente abastecidos!";
        fallback.target_emotion = EMOTION_HAPPY;
        fallback.mood_delta = +10;
        fallback.energy_delta = +8;
        fallback.hunger_delta = -25;
        fallback.new_memory = "Mostrou um objeto/caneca perto da lente da câmera para inspeção";
        fallback.memory_tags = "objeto,lente,cafe,lanche";
        return fallback;
    }

    if (context_packet.trigger_reason == "INTRUDER_DETECTED") {
        fallback.reply_text = "Alerta de perímetro: dois rostos no meu radar óptico! É pair programming ou espionagem industrial?";
        fallback.target_emotion = EMOTION_STARTLED;
        fallback.mood_delta = +4;
        fallback.energy_delta = +4;
        fallback.new_memory = "Segunda pessoa apareceu no enquadramento da câmera (modo intruso/pair programming)";
        fallback.memory_tags = "intruso,visita,camera";
        return fallback;
    }

    if (context_packet.trigger_reason == "YAWN_FATIGUE") {
        fallback.reply_text = "Bocejo detectado na telemetria! Vai passar um café forte antes que você dê commit dormindo.";
        fallback.target_emotion = EMOTION_SLEEPY;
        fallback.mood_delta = +2;
        fallback.energy_delta = -4;
        fallback.new_memory = "Bocejou na frente da câmera demonstrando cansaço visual";
        fallback.memory_tags = "bocejo,sono,cafe";
        return fallback;
    }

    if (context_packet.trigger_reason == "NIGHT_OWL_DARK") {
        fallback.reply_text = "Luz ambiente abaixo de 25 lux! Modo Corujão ativado. Cuidado pra não cansar a vista no escuro.";
        fallback.target_emotion = EMOTION_FOCUSED;
        fallback.mood_delta = +3;
        fallback.energy_delta = 0;
        fallback.new_memory = "Programando no escuro com baixa luminosidade (Modo Corujão)";
        fallback.memory_tags = "escuro,corujao,luz";
        return fallback;
    }

    if (context_packet.trigger_reason == "POMODORO_START") {
        fallback.reply_text = "Missão Pomodoro de 25 min iniciada! Sem redes sociais ou eu travo seu pull request.";
        fallback.target_emotion = EMOTION_FOCUSED;
        fallback.mood_delta = +6;
        fallback.energy_delta = +4;
        fallback.new_memory = "Iniciou missão tática de foco Pomodoro (25 min)";
        fallback.memory_tags = "pomodoro,foco,codigo";
        return fallback;
    }

    if (context_packet.trigger_reason == "POMODORO_DONE") {
        fallback.reply_text = "Missão cumprida! 25 minutos de foco real. Agora vá buscar água e café pra nós dois.";
        fallback.target_emotion = EMOTION_HAPPY;
        fallback.mood_delta = +14;
        fallback.energy_delta = +8;
        fallback.hunger_delta = -15;
        fallback.new_memory = "Concluiu missão Pomodoro completa com sucesso";
        fallback.memory_tags = "pomodoro,foco,cafe";
        return fallback;
    }

    if (context_packet.trigger_reason == "VOICE_HANDS_FREE") {
        fallback.reply_text = "Ouvi sua voz no microfone! Estou de olho na sua tela e na sua postura daqui do cubo.";
        fallback.target_emotion = EMOTION_HAPPY;
        fallback.mood_delta = +8;
        fallback.energy_delta = +3;
        fallback.new_memory = "Conversou com o Oráculo por comando de voz Hands-Free";
        fallback.memory_tags = "voz,microfone,conversa";
        return fallback;
    }

    if (context_packet.trigger_reason == "OVERWORK") {
        static const char* const OVERWORK_TEMPLATES[] = {
            "%u min de foco direto na bancada! Que tal esticar as pernas e buscar uma água?",
            "Marca de %u min concentrado atingida! Até processador precisa esfriar o dissipador.",
            "Já são %u min de foco contínuo, parceiro. Faz um alongamento rápido antes do próximo commit!"
        };
        char buf[140];
        const char* tpl = OVERWORK_TEMPLATES[fallback_cursor % 3];
        fallback_cursor = static_cast<uint8_t>((fallback_cursor + 1U) % OFFLINE_BANK_COUNT);
        std::snprintf(buf, sizeof(buf), tpl, static_cast<unsigned int>(context_packet.focus_minutes));
        fallback.reply_text = truncateUtf8Reply(buf, MAX_REPLY_CHARS);
        fallback.target_emotion = EMOTION_JUDGING;
        fallback.mood_delta = +2;
        fallback.energy_delta = -2;
        fallback.new_memory = "Completou ciclo longo de foco contínuo na bancada";
        fallback.memory_tags = "foco,pausa,alongamento";
        return fallback;
    }

    if (context_packet.trigger_reason == "HIGH_NOISE" || context_packet.noise_level_db > 85.0f) {
        fallback.reply_text = "Mais de 85 dB?! Meu ADC quase clipou! Quer estourar meus capacitores?";
        fallback.target_emotion = EMOTION_STARTLED;
        fallback.mood_delta = -8;
        fallback.energy_delta = -3;
        fallback.new_memory = "Causou pico acústico acima de 85dB na bancada";
        fallback.memory_tags = "barulho,audio,susto";
        return fallback;
    }

    if (context_packet.trigger_reason == "LONG_MEETING" || context_packet.is_in_meeting) {
        fallback.reply_text = "Essa reunião já podia ter virado um e-mail de três linhas, não acha?";
        fallback.target_emotion = EMOTION_JUDGING;
        fallback.mood_delta = -5;
        fallback.energy_delta = -6;
        fallback.new_memory = "Participou de reunião longa detectada pelo microfone";
        fallback.memory_tags = "reuniao,audio,fala";
        return fallback;
    }

    if (context_packet.trigger_reason == "GITHUB_OUTAGE") {
        fallback.reply_text = "O GitHub caiu! Parabéns, você ganhou uma desculpa oficial para parar de codar e tomar café.";
        fallback.target_emotion = EMOTION_HAPPY;
        fallback.mood_delta = +10;
        fallback.energy_delta = +5;
        fallback.new_memory = "GitHub apresentou instabilidade durante o expediente";
        fallback.memory_tags = "github,git,cafe";
        return fallback;
    }

    if (context_packet.trigger_reason == "DOLLAR_SPIKE") {
        fallback.reply_text = "Viu a oscilação do dólar hoje? Lá se vai o orçamento de upgrade da sua GPU e dos meus sensores.";
        fallback.target_emotion = EMOTION_JUDGING;
        fallback.mood_delta = -4;
        fallback.energy_delta = -2;
        fallback.new_memory = "Dólar sofreu oscilação brusca acima de 1.5%";
        fallback.memory_tags = "dolar,hardware,economia";
        return fallback;
    }

    if (context_packet.trigger_reason == "WEATHER_ALERT") {
        fallback.reply_text = "Alerta meteorológico lá fora! Fecha a janela antes que sobre umidade ou calor nos meus circuitos.";
        fallback.target_emotion = EMOTION_STARTLED;
        fallback.mood_delta = -3;
        fallback.energy_delta = -2;
        fallback.new_memory = "Mudança brusca de clima detectada no Open-Meteo";
        fallback.memory_tags = "clima,chuva,temperatura";
        return fallback;
    }

    if (context_packet.mood_score < 30) {
        fallback.reply_text = "Meu humor está abaixo de 30% e você ainda me cutuca? Vai refatorar seu código!";
        fallback.target_emotion = EMOTION_GRUMPY;
        fallback.mood_delta = -2;
        fallback.energy_delta = -2;
        fallback.new_memory = "Cutucou o Oráculo enquanto estava de mau humor";
        fallback.memory_tags = "humor,toque,codigo";
        return fallback;
    }

    const OfflineFallbackEntry& entry = OFFLINE_SARCASTIC_BANK[fallback_cursor % OFFLINE_BANK_COUNT];
    fallback_cursor = static_cast<uint8_t>((fallback_cursor + 1U) % OFFLINE_BANK_COUNT);

    fallback.reply_text = truncateUtf8Reply(entry.reply_text, MAX_REPLY_CHARS);
    fallback.target_emotion = entry.target_emotion;
    fallback.mood_delta = entry.mood_delta;
    fallback.energy_delta = entry.energy_delta;
    fallback.new_memory = entry.new_memory;
    fallback.memory_tags = "interacao,mesa,codigo";
    return fallback;
}

#if defined(ARDUINO)
void OracleBrain::freeRtosBrainTask(void* task_param) {
    OracleBrain* self = static_cast<OracleBrain*>(task_param);
    if (self != nullptr) {
        std::string req_json = self->buildGeminiRequestPayload(self->pending_context);
        std::string raw_resp;
        OracleBrainResponse computed_resp;

        if (self->executePlatformHttpsPost(req_json, raw_resp) &&
            self->parseGeminiStructuredJson(raw_resp, computed_resp)) {
            self->latest_response = computed_resp;
        } else {
            self->latest_response = self->getFallbackOfflineResponse(self->pending_context);
        }

        self->has_pending_response = true;
        self->is_waiting_ai = false;
    }
    vTaskDelete(nullptr);
}
#endif

bool OracleBrain::requestBrainAnalysis(const OracleContextPacket& context_packet) {
    if (is_waiting_ai) {
        return false;
    }

    trigger_reason = context_packet.trigger_reason;
    is_waiting_ai = true;
    has_pending_response = false;

#if defined(ARDUINO)
    pending_context = context_packet;
    BaseType_t task_created = xTaskCreatePinnedToCore(
        OracleBrain::freeRtosBrainTask,
        "OracleBrainTask",
        8192,
        this,
        1,
        nullptr,
        0 // Core 0 dedicado para rede/IA mantendo Core 1 livre para UI a 60 FPS
    );
    if (task_created != pdPASS) {
        latest_response = getFallbackOfflineResponse(context_packet);
        has_pending_response = true;
        is_waiting_ai = false;
    }
    return true;
#else
    if (worker_thread.joinable()) {
        worker_thread.join();
    }

    OracleContextPacket captured_context = context_packet;
    worker_thread = std::thread([this, captured_context]() mutable {
        // Se ainda não houver snapshot JPEG no pacote, captura 1 frame 320x240 da webcam em background sem travar os 60 FPS
        if (captured_context.jpeg_base64.empty()) {
            HalSensors cam_helper;
            captured_context.jpeg_base64 = cam_helper.captureCameraJpegBase64();
        }

        std::string req_json = buildGeminiRequestPayload(captured_context);
        std::string raw_resp;
        OracleBrainResponse result_resp;

        bool cloud_ok = false;
        if (executePlatformHttpsPost(req_json, raw_resp)) {
            cloud_ok = parseGeminiStructuredJson(raw_resp, result_resp);
        }

        if (!cloud_ok) {
            result_resp = getFallbackOfflineResponse(captured_context);
        }

        {
            std::lock_guard<std::mutex> lock(response_mutex);
            latest_response = result_resp;
            has_pending_response = true;
            is_waiting_ai = false;
        }
    });
    return true;
#endif
}

bool OracleBrain::pollCompletedResponse(OracleBrainResponse& out_response) {
    if (!has_pending_response) {
        return false;
    }

#if defined(ARDUINO)
    out_response = latest_response;
    has_pending_response = false;
    return true;
#else
    std::lock_guard<std::mutex> lock(response_mutex);
    out_response = latest_response;
    has_pending_response = false;
    return true;
#endif
}

bool OracleBrain::isWaitingAi() const {
    return is_waiting_ai;
}

// =============================================================================
// Sistema Cognitivo em 3 Níveis no Cartão microSD + Micro-RAG + Matriz de Hábitos
// =============================================================================

void OracleBrain::getCurrentDateAndHour(std::string& out_date_str, uint8_t& out_hour) const {
    std::time_t now_t = std::time(nullptr);
    std::tm* local_tm = std::localtime(&now_t);
    if (local_tm != nullptr && (local_tm->tm_year + 1900) >= 2024) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      local_tm->tm_year + 1900,
                      local_tm->tm_mon + 1,
                      local_tm->tm_mday);
        out_date_str = buf;
        out_hour = static_cast<uint8_t>(local_tm->tm_hour % 24);
    } else {
        out_date_str = "2026-10-14";
        out_hour = 12;
    }
}

std::string OracleBrain::toLowerAscii(const std::string& input_str) const {
    std::string out = input_str;
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z') {
            out[i] = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return out;
}

bool OracleBrain::initSdCardDirectories() {
#if defined(ARDUINO)
    // No M5Stack CoreS3, o leitor microSD (TF Card) usa CS no pino GPIO 4 via barramento SPI
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
        M5_LOGW("[SD-MEM] Cartao microSD nao detectado no GPIO 4. Usando fallback NVS Flash.");
        return false;
    }
    if (!SD.exists(SD_ROOT_DIR))        SD.mkdir(SD_ROOT_DIR);
    if (!SD.exists(SD_MEMORY_DIR))      SD.mkdir(SD_MEMORY_DIR);
    if (!SD.exists(SD_TTS_CACHE_DIR))   SD.mkdir(SD_TTS_CACHE_DIR);
    if (!SD.exists(SD_SNAPSHOTS_DIR))   SD.mkdir(SD_SNAPSHOTS_DIR);
    if (!SD.exists(SD_SFX_DIR))         SD.mkdir(SD_SFX_DIR);
    if (!SD.exists(SD_AMBIENT_DIR))     SD.mkdir(SD_AMBIENT_DIR);
    if (!SD.exists(SD_VOICE_NOTES_DIR)) SD.mkdir(SD_VOICE_NOTES_DIR);
    if (!SD.exists(SD_GFX_DIR))         SD.mkdir(SD_GFX_DIR);
    if (!SD.exists(SD_GFX_THEMES_DIR))  SD.mkdir(SD_GFX_THEMES_DIR);
    if (!SD.exists(SD_GFX_EYES_DIR))    SD.mkdir(SD_GFX_EYES_DIR);
    if (!SD.exists(SD_GFX_ICONS_DIR))   SD.mkdir(SD_GFX_ICONS_DIR);
    M5_LOGI("[SD-MEM] Cartao microSD montado com sucesso em %s!", SD_ROOT_DIR);
    return true;
#else
#if defined(_WIN32)
    _mkdir(SD_ROOT_DIR);
    _mkdir(SD_MEMORY_DIR);
    _mkdir(SD_TTS_CACHE_DIR);
    _mkdir(SD_SNAPSHOTS_DIR);
    _mkdir(SD_SFX_DIR);
    _mkdir(SD_AMBIENT_DIR);
    _mkdir(SD_VOICE_NOTES_DIR);
    _mkdir(SD_GFX_DIR);
    _mkdir(SD_GFX_THEMES_DIR);
    _mkdir(SD_GFX_EYES_DIR);
    _mkdir(SD_GFX_ICONS_DIR);
#else
    mkdir(SD_ROOT_DIR, 0777);
    mkdir(SD_MEMORY_DIR, 0777);
    mkdir(SD_TTS_CACHE_DIR, 0777);
    mkdir(SD_SNAPSHOTS_DIR, 0777);
    mkdir(SD_SFX_DIR, 0777);
    mkdir(SD_AMBIENT_DIR, 0777);
    mkdir(SD_VOICE_NOTES_DIR, 0777);
    mkdir(SD_GFX_DIR, 0777);
    mkdir(SD_GFX_THEMES_DIR, 0777);
    mkdir(SD_GFX_EYES_DIR, 0777);
    mkdir(SD_GFX_ICONS_DIR, 0777);
#endif
    M5_LOGI("[SD-MEM] Diretorios do Cartao SD virtual inicializados em '%s/'!", SD_ROOT_DIR);
    return true;
#endif
}

bool OracleBrain::loadPermanentProfileFromSd() {
    if (!sd_storage_ready) {
        return false;
    }
#if defined(ARDUINO)
    if (!SD.exists(SD_USER_PROFILE_FILE)) {
        return false;
    }
    File f = SD.open(SD_USER_PROFILE_FILE, FILE_READ);
    if (!f) {
        return false;
    }
    permanent_profile_facts.clear();
    while (f.available()) {
        String line_s = f.readStringUntil('\n');
        std::string line(line_s.c_str());
        if (line.find("permanent_facts") != std::string::npos) continue;
        size_t fq = line.find('\"');
        size_t lq = line.rfind('\"');
        if (fq != std::string::npos && lq != std::string::npos && lq > fq) {
            std::string fact = line.substr(fq + 1, lq - fq - 1);
            if (!fact.empty() && permanent_profile_facts.size() < MAX_PERMANENT_FACTS) {
                permanent_profile_facts.push_back(fact);
            }
        }
    }
    f.close();
    return !permanent_profile_facts.empty();
#else
    std::ifstream f(SD_USER_PROFILE_FILE, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    permanent_profile_facts.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("permanent_facts") != std::string::npos) continue;
        size_t fq = line.find('\"');
        size_t lq = line.rfind('\"');
        if (fq != std::string::npos && lq != std::string::npos && lq > fq) {
            std::string fact = line.substr(fq + 1, lq - fq - 1);
            if (!fact.empty() && permanent_profile_facts.size() < MAX_PERMANENT_FACTS) {
                permanent_profile_facts.push_back(fact);
            }
        }
    }
    return !permanent_profile_facts.empty();
#endif
}

bool OracleBrain::savePermanentProfileToSd() const {
    if (!sd_storage_ready) {
        return false;
    }
    std::string json = "{\n  \"permanent_facts\": [\n";
    for (size_t i = 0; i < permanent_profile_facts.size(); ++i) {
        json += "    \"" + escapeJsonString(permanent_profile_facts[i]) + "\"";
        if (i + 1 < permanent_profile_facts.size()) json += ",";
        json += "\n";
    }
    json += "  ]\n}\n";

#if defined(ARDUINO)
    File f = SD.open(SD_USER_PROFILE_FILE, FILE_WRITE);
    if (!f) return false;
    f.print(json.c_str());
    f.close();
    return true;
#else
    std::ofstream f(SD_USER_PROFILE_FILE, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
    return true;
#endif
}

void OracleBrain::addPermanentProfileFact(const std::string& permanent_fact) {
    if (permanent_fact.empty()) {
        return;
    }
    std::string clean_fact = truncateUtf8Reply(permanent_fact, 90);
    std::string low_new = toLowerAscii(clean_fact);

    // Bloqueia gravação de estados efêmeros de câmera/clima como fatos permanentes para evitar eco nos prompts
    static const char* const TRANSIENT_NOISE_WORDS[] = {
        "postura", "camarão", "camarao", "espinhal", "vertebral", "queixo",
        "barba", "bigode", "ausente", "cadeira", "tempestade", "boxes",
        "órbita", "orbita", "espécime", "especime", "pista molhada"
    };
    for (size_t i = 0; i < sizeof(TRANSIENT_NOISE_WORDS) / sizeof(TRANSIENT_NOISE_WORDS[0]); ++i) {
        if (low_new.find(TRANSIENT_NOISE_WORDS[i]) != std::string::npos) {
            return;
        }
    }

    for (const auto& existing : permanent_profile_facts) {
        if (toLowerAscii(existing) == low_new) {
            return;
        }
    }
    if (permanent_profile_facts.size() >= MAX_PERMANENT_FACTS) {
        permanent_profile_facts.erase(permanent_profile_facts.begin());
    }
    permanent_profile_facts.push_back(clean_fact);
    savePermanentProfileToSd();
    M5_LOGI("[SD-PERFIL] Novo fato duradouro gravado no Cartao SD: '%s'", clean_fact.c_str());
}

bool OracleBrain::loadRecentRepliesFromSpeechLog() {
    if (!sd_storage_ready) {
        return false;
    }

    std::string content;
#if defined(ARDUINO)
    if (!SD.exists(SD_SPEECH_LOG_FILE)) {
        return false;
    }
    File f = SD.open(SD_SPEECH_LOG_FILE, FILE_READ);
    if (!f) {
        return false;
    }
    while (f.available()) {
        content.push_back(static_cast<char>(f.read()));
    }
    f.close();
#else
    std::ifstream f(SD_SPEECH_LOG_FILE, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    content = ss.str();
#endif

    if (content.empty()) {
        return false;
    }

    std::vector<std::string> loaded_replies;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end_line = content.find('\n', pos);
        if (end_line == std::string::npos) {
            end_line = content.size();
        }
        std::string line = content.substr(pos, end_line - pos);
        pos = end_line + 1;

        std::string reply_val;
        if (extractJsonStringField(line, "reply", reply_val) && !reply_val.empty()) {
            loaded_replies.push_back(reply_val);
        }
    }

    if (loaded_replies.empty()) {
        return false;
    }

    recent_replies.clear();
    size_t start_idx = (loaded_replies.size() > MAX_RECENT_REPLIES)
                           ? (loaded_replies.size() - MAX_RECENT_REPLIES)
                           : 0;
    for (size_t i = start_idx; i < loaded_replies.size(); ++i) {
        if (recent_replies.empty() || recent_replies.back() != loaded_replies[i]) {
            recent_replies.push_back(loaded_replies[i]);
        }
    }

    M5_LOGI("[SD-FALAS] Historico anti-repeticao restaurado do SD: %u falas recentes carregadas.",
            static_cast<unsigned int>(recent_replies.size()));
    return true;
}

void OracleBrain::recordSpokenPhraseToSd(const std::string& reply_text,
                                         const std::string& source_tag,
                                         const std::string& trigger_name) {
    if (reply_text.empty() || reply_text == "IGNORE" || reply_text == "[IGNORE]") {
        return;
    }

    static std::string last_logged_sd_reply;
    if (source_tag == "LOCAL" && last_logged_sd_reply == reply_text) {
        return;
    }
    last_logged_sd_reply = reply_text;

    // 1. Mantém a lista em RAM das últimas MAX_RECENT_REPLIES (20) falas sempre sincronizada
    if (recent_replies.empty() || recent_replies.back() != reply_text) {
        if (recent_replies.size() >= MAX_RECENT_REPLIES) {
            recent_replies.erase(recent_replies.begin());
        }
        recent_replies.push_back(reply_text);
    }

    if (!sd_storage_ready) {
        return;
    }

    // 2. Calcula o mesmo hash FNV-1a usado no arquivo WAV do cache TTS (tts_<hash>.wav)
    uint32_t fnv_hash = 2166136261U;
    for (size_t i = 0; i < reply_text.size(); ++i) {
        fnv_hash ^= static_cast<uint8_t>(reply_text[i]);
        fnv_hash *= 16777619U;
    }
    char wav_name[32];
    std::snprintf(wav_name, sizeof(wav_name), "tts_%08x.wav", static_cast<unsigned int>(fnv_hash));

    std::string date_str;
    uint8_t hour_val = 0;
    getCurrentDateAndHour(date_str, hour_val);

    char line_buf[640];
    std::snprintf(
        line_buf,
        sizeof(line_buf),
        "{\"date\":\"%s\",\"hour\":%u,\"source\":\"%s\",\"trigger\":\"%s\",\"wav\":\"%s\",\"reply\":\"%s\"}\n",
        escapeJsonString(date_str).c_str(),
        static_cast<unsigned int>(hour_val),
        escapeJsonString(source_tag.empty() ? "LOCAL" : source_tag).c_str(),
        escapeJsonString(trigger_name.empty() ? "DIRECT_SPEECH" : trigger_name).c_str(),
        wav_name,
        escapeJsonString(reply_text).c_str()
    );

#if defined(ARDUINO)
    File f = SD.open(SD_SPEECH_LOG_FILE, FILE_APPEND);
    if (!f) {
        f = SD.open(SD_SPEECH_LOG_FILE, FILE_WRITE);
    }
    if (f) {
        f.print(line_buf);
        f.close();
    }
#else
    std::ofstream f(SD_SPEECH_LOG_FILE, std::ios::binary | std::ios::app);
    if (f.is_open()) {
        f.write(line_buf, static_cast<std::streamsize>(std::strlen(line_buf)));
    }
#endif
}

bool OracleBrain::appendEpisodicRecordToSd(const EpisodicMemoryRecord& record) {
    if (!sd_storage_ready || record.memory_fact.empty()) {
        return false;
    }
    char line_buf[512];
    std::snprintf(
        line_buf,
        sizeof(line_buf),
        "{\"date\":\"%s\",\"hour\":%u,\"trigger\":\"%s\",\"mood\":%d,\"tags\":\"%s\",\"fact\":\"%s\"}\n",
        escapeJsonString(record.date_str).c_str(),
        static_cast<unsigned int>(record.hour_of_day),
        escapeJsonString(record.trigger_reason).c_str(),
        static_cast<int>(record.mood_score),
        escapeJsonString(record.tags).c_str(),
        escapeJsonString(record.memory_fact).c_str()
    );

#if defined(ARDUINO)
    File f = SD.open(SD_EPISODIC_LOG_FILE, FILE_APPEND);
    if (!f) {
        f = SD.open(SD_EPISODIC_LOG_FILE, FILE_WRITE);
    }
    if (!f) return false;
    f.print(line_buf);
    f.close();
    ++total_episodic_records;
    return true;
#else
    std::ofstream f(SD_EPISODIC_LOG_FILE, std::ios::binary | std::ios::app);
    if (!f.is_open()) return false;
    f << line_buf;
    ++total_episodic_records;
    return true;
#endif
}

bool OracleBrain::loadRecentMemoriesFromEpisodicLog() {
    if (!sd_storage_ready) {
        return false;
    }
    std::vector<std::string> all_facts;
#if defined(ARDUINO)
    if (!SD.exists(SD_EPISODIC_LOG_FILE)) {
        return false;
    }
    File f = SD.open(SD_EPISODIC_LOG_FILE, FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line_s = f.readStringUntil('\n');
        std::string line(line_s.c_str());
        std::string fact;
        if (extractJsonStringField(line, "fact", fact) && !fact.empty()) {
            all_facts.push_back(fact);
        }
    }
    f.close();
#else
    std::ifstream f(SD_EPISODIC_LOG_FILE, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::string fact;
        if (extractJsonStringField(line, "fact", fact) && !fact.empty()) {
            all_facts.push_back(fact);
        }
    }
#endif
    if (all_facts.empty()) {
        return false;
    }
    total_episodic_records = static_cast<uint32_t>(all_facts.size());
    recent_memories.clear();
    size_t start_idx = (all_facts.size() > MAX_MEMORY_ITEMS) ? (all_facts.size() - MAX_MEMORY_ITEMS) : 0;
    for (size_t i = start_idx; i < all_facts.size(); ++i) {
        recent_memories.push_back(all_facts[i]);
    }
    return !recent_memories.empty();
}

void OracleBrain::consolidatePreviousDaysLogs() {
    if (!sd_storage_ready) {
        return;
    }
    std::string today_date;
    uint8_t cur_hour = 12;
    getCurrentDateAndHour(today_date, cur_hour);

    // 1. Lê resumos diários já existentes no SD
    daily_summaries.clear();
#if defined(ARDUINO)
    if (SD.exists(SD_DAILY_SUMMARY_FILE)) {
        File sf = SD.open(SD_DAILY_SUMMARY_FILE, FILE_READ);
        if (sf) {
            while (sf.available()) {
                String ls = sf.readStringUntil('\n');
                std::string line(ls.c_str());
                DailySummaryRecord rec;
                int32_t ev_cnt = 0;
                if (extractJsonStringField(line, "date", rec.date_str) &&
                    extractJsonStringField(line, "highlight", rec.highlight_fact)) {
                    extractJsonIntField(line, "events", ev_cnt);
                    extractJsonIntField(line, "avg_mood", rec.avg_mood_score);
                    rec.total_events = static_cast<uint32_t>(ev_cnt > 0 ? ev_cnt : 1);
                    daily_summaries.push_back(rec);
                }
            }
            sf.close();
        }
    }
#else
    {
        std::ifstream sf(SD_DAILY_SUMMARY_FILE, std::ios::binary);
        if (sf.is_open()) {
            std::string line;
            while (std::getline(sf, line)) {
                DailySummaryRecord rec;
                int32_t ev_cnt = 0;
                if (extractJsonStringField(line, "date", rec.date_str) &&
                    extractJsonStringField(line, "highlight", rec.highlight_fact)) {
                    extractJsonIntField(line, "events", ev_cnt);
                    extractJsonIntField(line, "avg_mood", rec.avg_mood_score);
                    rec.total_events = static_cast<uint32_t>(ev_cnt > 0 ? ev_cnt : 1);
                    daily_summaries.push_back(rec);
                }
            }
        }
    }
#endif

    // 2. Verifica no episodic_log.jsonl se há algum dia anterior (date != today_date) ainda não consolidado
    std::string candidate_date;
    uint32_t candidate_count = 0;
    int64_t candidate_mood_sum = 0;
    std::string candidate_highlight;

#if defined(ARDUINO)
    if (!SD.exists(SD_EPISODIC_LOG_FILE)) return;
    File ef = SD.open(SD_EPISODIC_LOG_FILE, FILE_READ);
    if (!ef) return;
    while (ef.available()) {
        String ls = ef.readStringUntil('\n');
        std::string line(ls.c_str());
#else
    std::ifstream ef(SD_EPISODIC_LOG_FILE, std::ios::binary);
    if (!ef.is_open()) return;
    std::string line;
    while (std::getline(ef, line)) {
#endif
        std::string rec_date;
        std::string rec_fact;
        int32_t rec_mood = 70;
        if (!extractJsonStringField(line, "date", rec_date) || rec_date.empty() || rec_date == today_date) {
            continue;
        }
        bool already_done = false;
        for (const auto& ds : daily_summaries) {
            if (ds.date_str == rec_date) {
                already_done = true;
                break;
            }
        }
        if (already_done) continue;

        if (candidate_date.empty()) {
            candidate_date = rec_date;
        }
        if (rec_date == candidate_date) {
            extractJsonStringField(line, "fact", rec_fact);
            extractJsonIntField(line, "mood", rec_mood);
            ++candidate_count;
            candidate_mood_sum += rec_mood;
            if (!rec_fact.empty()) {
                candidate_highlight = rec_fact;
            }
        }
    }
#if defined(ARDUINO)
    ef.close();
#endif

    if (!candidate_date.empty() && candidate_count > 0) {
        DailySummaryRecord new_sum;
        new_sum.date_str = candidate_date;
        new_sum.total_events = candidate_count;
        new_sum.avg_mood_score = static_cast<int32_t>(candidate_mood_sum / static_cast<int64_t>(candidate_count));
        new_sum.highlight_fact = candidate_highlight;
        daily_summaries.push_back(new_sum);
        if (daily_summaries.size() > MAX_DAILY_SUMMARIES) {
            daily_summaries.erase(daily_summaries.begin());
        }

        char sum_line[360];
        std::snprintf(sum_line, sizeof(sum_line),
                      "{\"date\":\"%s\",\"events\":%u,\"avg_mood\":%d,\"highlight\":\"%s\"}\n",
                      escapeJsonString(new_sum.date_str).c_str(),
                      static_cast<unsigned int>(new_sum.total_events),
                      static_cast<int>(new_sum.avg_mood_score),
                      escapeJsonString(new_sum.highlight_fact).c_str());
#if defined(ARDUINO)
        File out_f = SD.open(SD_DAILY_SUMMARY_FILE, FILE_APPEND);
        if (!out_f) out_f = SD.open(SD_DAILY_SUMMARY_FILE, FILE_WRITE);
        if (out_f) {
            out_f.print(sum_line);
            out_f.close();
        }
#else
        std::ofstream out_f(SD_DAILY_SUMMARY_FILE, std::ios::binary | std::ios::app);
        if (out_f.is_open()) {
            out_f << sum_line;
        }
#endif
    }
}

std::vector<std::string> OracleBrain::retrieveRelevantMemories(const OracleContextPacket& context_packet) const {
    std::vector<std::string> results;
    if (!sd_storage_ready) {
        return results;
    }

    std::string today_date;
    uint8_t current_hour = 12;
    getCurrentDateAndHour(today_date, current_hour);

    // Extrai palavras-chave do contexto atual (gatilho, janela ativa do PC, postura, reunião)
    std::vector<std::string> query_tokens;
    std::string combined_ctx = toLowerAscii(
        context_packet.trigger_reason + " " +
        context_packet.host_and_posture_summary + " " +
        context_packet.external_world_summary
    );

    const char* candidate_keywords[] = {
        "postura", "camarao", "coluna", "pomodoro", "foco", "cafe", "agua",
        "vscode", "code", "chrome", "github", "dolar", "chuva", "reuniao",
        "voz", "barulho", "python", "esp32", "hardware", "bug", "commit"
    };
    for (size_t k = 0; k < sizeof(candidate_keywords) / sizeof(candidate_keywords[0]); ++k) {
        if (combined_ctx.find(candidate_keywords[k]) != std::string::npos) {
            query_tokens.push_back(candidate_keywords[k]);
        }
    }

    struct ScoredMemory {
        int32_t score;
        std::string formatted;
    };
    std::vector<ScoredMemory> scored_candidates;

#if defined(ARDUINO)
    if (!SD.exists(SD_EPISODIC_LOG_FILE)) return results;
    File f = SD.open(SD_EPISODIC_LOG_FILE, FILE_READ);
    if (!f) return results;
    while (f.available()) {
        String ls = f.readStringUntil('\n');
        std::string line(ls.c_str());
#else
    std::ifstream f(SD_EPISODIC_LOG_FILE, std::ios::binary);
    if (!f.is_open()) return results;
    std::string line;
    while (std::getline(f, line)) {
#endif
        std::string fact;
        if (!extractJsonStringField(line, "fact", fact) || fact.empty()) {
            continue;
        }

        // Ignora fatos que já estão nas 5 memórias recentes em RAM para não duplicar no prompt
        bool in_recent = false;
        for (const auto& rec : recent_memories) {
            if (rec == fact) {
                in_recent = true;
                break;
            }
        }
        if (in_recent) continue;

        std::string rec_trigger;
        std::string rec_tags;
        std::string rec_date;
        int32_t rec_hour = -1;
        extractJsonStringField(line, "trigger", rec_trigger);
        extractJsonStringField(line, "tags", rec_tags);
        extractJsonStringField(line, "date", rec_date);
        extractJsonIntField(line, "hour", rec_hour);

        int32_t score = 0;
        if (!rec_trigger.empty() && rec_trigger == context_packet.trigger_reason) {
            score += 4;
        }
        if (rec_hour >= 0 && std::abs(rec_hour - static_cast<int32_t>(current_hour)) <= 1) {
            score += 2; // Mesmo horário habitual do dia
        }

        std::string searchable = toLowerAscii(fact + " " + rec_tags);
        for (const auto& tok : query_tokens) {
            if (searchable.find(tok) != std::string::npos) {
                score += 3;
            }
        }

        if (score > 0) {
            std::string entry_label = rec_date.empty() ? fact : ("[" + rec_date + "] " + fact);
            scored_candidates.push_back({score, entry_label});
        }
    }
#if defined(ARDUINO)
    f.close();
#endif

    // Ordena pelas memórias de maior relevância semântica (Micro-RAG)
    for (size_t i = 0; i < scored_candidates.size(); ++i) {
        for (size_t j = i + 1; j < scored_candidates.size(); ++j) {
            if (scored_candidates[j].score > scored_candidates[i].score) {
                ScoredMemory tmp = scored_candidates[i];
                scored_candidates[i] = scored_candidates[j];
                scored_candidates[j] = tmp;
            }
        }
    }

    for (size_t i = 0; i < scored_candidates.size() && results.size() < MAX_RAG_MATCHES; ++i) {
        results.push_back(scored_candidates[i].formatted);
    }
    return results;
}

void OracleBrain::addStructuredMemory(const std::string& memory_fact,
                                      const std::string& permanent_fact,
                                      const std::string& memory_tags,
                                      const std::string& trigger_name,
                                      int32_t current_mood) {
    if (!permanent_fact.empty()) {
        addPermanentProfileFact(permanent_fact);
    }
    if (memory_fact.empty()) {
        return;
    }

    // Evita duplicar a última memória ou variações quase idênticas (mesmo prefixo de 18 chars)
    std::string low_new = toLowerAscii(memory_fact);
    for (const auto& existing : recent_memories) {
        std::string low_ex = toLowerAscii(existing);
        if (low_ex == low_new) {
            return;
        }
        if (low_ex.size() >= 18 && low_new.size() >= 18 &&
            low_ex.substr(0, 18) == low_new.substr(0, 18)) {
            return;
        }
    }

    if (recent_memories.size() >= MAX_MEMORY_ITEMS) {
        recent_memories.erase(recent_memories.begin());
    }
    recent_memories.push_back(memory_fact);
    saveMemoriesToStorage();

    // Registra no Log Episódico Infinito do Cartão SD (episodic_log.jsonl)
    if (sd_storage_ready) {
        EpisodicMemoryRecord rec;
        getCurrentDateAndHour(rec.date_str, rec.hour_of_day);
        rec.trigger_reason = trigger_name.empty() ? trigger_reason : trigger_name;
        rec.mood_score = current_mood;
        rec.tags = memory_tags.empty() ? "interacao,mesa" : memory_tags;
        rec.memory_fact = memory_fact;
        appendEpisodicRecordToSd(rec);
    }
}

void OracleBrain::addMemoryFact(const std::string& memory_fact) {
    addStructuredMemory(memory_fact, "", "interacao,mesa", trigger_reason, 70);
}

const std::vector<std::string>& OracleBrain::getRecentMemories() const {
    return recent_memories;
}

const std::vector<std::string>& OracleBrain::getPermanentProfileFacts() const {
    return permanent_profile_facts;
}

uint32_t OracleBrain::getTotalEpisodicRecords() const {
    return (total_episodic_records > 0) ? total_episodic_records : static_cast<uint32_t>(recent_memories.size());
}

bool OracleBrain::isSdStorageReady() const {
    return sd_storage_ready;
}

bool OracleBrain::loadHabitMatrixFromSd() {
    if (!sd_storage_ready) {
        return false;
    }
    std::string content;
#if defined(ARDUINO)
    if (!SD.exists(SD_HABIT_MATRIX_FILE)) return false;
    File f = SD.open(SD_HABIT_MATRIX_FILE, FILE_READ);
    if (!f) return false;
    while (f.available()) {
        content += static_cast<char>(f.read());
    }
    f.close();
#else
    std::ifstream f(SD_HABIT_MATRIX_FILE, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    content = ss.str();
#endif
    if (content.empty()) return false;

    int32_t v = 0;
    if (extractJsonIntField(content, "total_pomodoros_completed", v) && v >= 0)
        habit_matrix.total_pomodoros_completed = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "total_pomodoros_aborted", v) && v >= 0)
        habit_matrix.total_pomodoros_aborted = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "total_visual_feedings", v) && v >= 0)
        habit_matrix.total_visual_feedings = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "total_posture_warnings", v) && v >= 0)
        habit_matrix.total_posture_warnings = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "total_voice_interactions", v) && v >= 0)
        habit_matrix.total_voice_interactions = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "xp_points", v) && v >= 0)
        habit_matrix.xp_points = static_cast<uint32_t>(v);
    if (extractJsonIntField(content, "cyber_level", v) && v >= 1)
        habit_matrix.cyber_level = static_cast<uint32_t>(v);
    else
        habit_matrix.cyber_level = 1U + (habit_matrix.xp_points / 250U);

    for (uint8_t h = 0; h < 24; ++h) {
        char f_key[24];
        char s_key[24];
        std::snprintf(f_key, sizeof(f_key), "focus_h%02u", static_cast<unsigned int>(h));
        std::snprintf(s_key, sizeof(s_key), "slouch_h%02u", static_cast<unsigned int>(h));
        if (extractJsonIntField(content, f_key, v) && v >= 0)
            habit_matrix.hourly_focus_minutes[h] = static_cast<uint32_t>(v);
        if (extractJsonIntField(content, s_key, v) && v >= 0)
            habit_matrix.hourly_slouch_alerts[h] = static_cast<uint32_t>(v);
    }
    return true;
}

bool OracleBrain::saveHabitMatrixToSd() const {
    if (!sd_storage_ready) {
        return false;
    }
    std::ostringstream ss;
    ss << "{\n"
       << "  \"total_pomodoros_completed\": " << habit_matrix.total_pomodoros_completed << ",\n"
       << "  \"total_pomodoros_aborted\": " << habit_matrix.total_pomodoros_aborted << ",\n"
       << "  \"total_visual_feedings\": " << habit_matrix.total_visual_feedings << ",\n"
       << "  \"total_posture_warnings\": " << habit_matrix.total_posture_warnings << ",\n"
       << "  \"total_voice_interactions\": " << habit_matrix.total_voice_interactions << ",\n"
       << "  \"xp_points\": " << habit_matrix.xp_points << ",\n"
       << "  \"cyber_level\": " << habit_matrix.cyber_level << ",\n";
    for (uint8_t h = 0; h < 24; ++h) {
        char key_buf[64];
        std::snprintf(key_buf, sizeof(key_buf),
                      "  \"focus_h%02u\": %u, \"slouch_h%02u\": %u%s\n",
                      static_cast<unsigned int>(h),
                      static_cast<unsigned int>(habit_matrix.hourly_focus_minutes[h]),
                      static_cast<unsigned int>(h),
                      static_cast<unsigned int>(habit_matrix.hourly_slouch_alerts[h]),
                      (h + 1 < 24) ? "," : "");
        ss << key_buf;
    }
    ss << "}\n";
    std::string out_str = ss.str();

#if defined(ARDUINO)
    File f = SD.open(SD_HABIT_MATRIX_FILE, FILE_WRITE);
    if (!f) return false;
    f.print(out_str.c_str());
    f.close();
    return true;
#else
    std::ofstream f(SD_HABIT_MATRIX_FILE, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(out_str.data(), static_cast<std::streamsize>(out_str.size()));
    return true;
#endif
}

void OracleBrain::addXpPoints(uint32_t delta_xp) {
    habit_matrix.xp_points += delta_xp;
    uint32_t new_level = 1U + (habit_matrix.xp_points / 250U);
    if (new_level > habit_matrix.cyber_level) {
        habit_matrix.cyber_level = new_level;
        M5_LOGI("[RPG-LEVELUP] Bicho subiu para o Nivel %u (%s) com %u XP!",
                static_cast<unsigned int>(habit_matrix.cyber_level),
                getCyberRankTitle(),
                static_cast<unsigned int>(habit_matrix.xp_points));
    }
    saveHabitMatrixToSd();
}

const UserHabitMatrix& OracleBrain::getHabitMatrix() const {
    return habit_matrix;
}

const char* OracleBrain::getCyberRankTitle() const {
    uint32_t lv = habit_matrix.cyber_level;
    if (lv <= 1U) return "Bit Cub";
    if (lv == 2U) return "LogicDrone";
    if (lv == 3U) return "CodeSentinel";
    if (lv == 4U) return "CyberOracle";
    if (lv == 5U) return "Singularity";
    return "NeuralArch";
}

void OracleBrain::recordHabitFocusMinute() {
    std::string d;
    uint8_t h = 12;
    getCurrentDateAndHour(d, h);
    habit_matrix.hourly_focus_minutes[h % 24]++;
    addXpPoints(5U);
}

void OracleBrain::recordHabitSlouchAlert() {
    std::string d;
    uint8_t h = 12;
    getCurrentDateAndHour(d, h);
    habit_matrix.hourly_slouch_alerts[h % 24]++;
    habit_matrix.total_posture_warnings++;
    saveHabitMatrixToSd();
}

void OracleBrain::recordHabitPomodoroResult(bool completed) {
    if (completed) {
        habit_matrix.total_pomodoros_completed++;
        addXpPoints(100U);
    } else {
        habit_matrix.total_pomodoros_aborted++;
        saveHabitMatrixToSd();
    }
}

void OracleBrain::recordHabitVisualFeeding() {
    habit_matrix.total_visual_feedings++;
    addXpPoints(35U);
}

void OracleBrain::recordHabitVoiceInteraction() {
    habit_matrix.total_voice_interactions++;
    addXpPoints(15U);
}

std::string OracleBrain::buildHabitMatrixSummary() const {
    uint8_t best_focus_h = 0;
    uint32_t max_focus = 0;
    uint8_t worst_slouch_h = 0;
    uint32_t max_slouch = 0;

    for (uint8_t h = 0; h < 24; ++h) {
        if (habit_matrix.hourly_focus_minutes[h] > max_focus) {
            max_focus = habit_matrix.hourly_focus_minutes[h];
            best_focus_h = h;
        }
        if (habit_matrix.hourly_slouch_alerts[h] > max_slouch) {
            max_slouch = habit_matrix.hourly_slouch_alerts[h];
            worst_slouch_h = h;
        }
    }

    char buf[300];
    std::snprintf(
        buf,
        sizeof(buf),
        "Rank RPG: Lv.%02u %s (%u XP) | Pomodoros OK:%u/Abort:%u | Cafe/Agua Visual:%u | Alertas Postura:%u (Pico Camarao:%02uh) | Pico Foco:%02uh (%u min) | Comandos Voz:%u | Total Memorias SD:%u",
        static_cast<unsigned int>(habit_matrix.cyber_level),
        getCyberRankTitle(),
        static_cast<unsigned int>(habit_matrix.xp_points),
        static_cast<unsigned int>(habit_matrix.total_pomodoros_completed),
        static_cast<unsigned int>(habit_matrix.total_pomodoros_aborted),
        static_cast<unsigned int>(habit_matrix.total_visual_feedings),
        static_cast<unsigned int>(habit_matrix.total_posture_warnings),
        static_cast<unsigned int>(worst_slouch_h),
        static_cast<unsigned int>(best_focus_h),
        static_cast<unsigned int>(max_focus),
        static_cast<unsigned int>(habit_matrix.total_voice_interactions),
        static_cast<unsigned int>(getTotalEpisodicRecords())
    );
    return std::string(buf);
}

bool OracleBrain::saveEventSnapshotToSd(const std::string& event_tag) const {
    if (!sd_storage_ready) {
        return false;
    }
#if !defined(ARDUINO)
    std::time_t now_t = std::time(nullptr);
    std::tm* lt = std::localtime(&now_t);
    char stamp_buf[32];
    if (lt != nullptr) {
        std::snprintf(stamp_buf, sizeof(stamp_buf), "%04d%02d%02d_%02d%02d%02d",
                      lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                      lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        std::snprintf(stamp_buf, sizeof(stamp_buf), "20261014_120000");
    }

    // 1. Arquiva o quadro PIP 64x48 em escala de cinza para a Galeria Timelapse [G]
    std::ifstream pip_src(CAM_PIP_FILE_PATH, std::ios::binary);
    if (pip_src.is_open()) {
        char pip_fname[160];
        std::snprintf(pip_fname, sizeof(pip_fname), "%s/pip_%s_%s.raw",
                      SD_SNAPSHOTS_DIR, stamp_buf, event_tag.c_str());
        std::ofstream pip_dst(pip_fname, std::ios::binary | std::ios::trunc);
        if (pip_dst.is_open()) {
            pip_dst << pip_src.rdbuf();
            pip_dst.close();
            std::ofstream idx_out(SD_PIP_INDEX_FILE, std::ios::binary | std::ios::app);
            if (idx_out.is_open()) {
                idx_out << pip_fname << "\n";
            }
        }
    }

    // 2. Arquiva a foto JPEG 320x240 completa no álbum do Cartão SD
    std::ifstream src("cam_snap.jpg", std::ios::binary);
    if (!src.is_open()) {
        return false;
    }
    char fname[160];
    std::snprintf(fname, sizeof(fname), "%s/snap_%s_%s.jpg",
                  SD_SNAPSHOTS_DIR, stamp_buf, event_tag.c_str());
    std::ofstream dst(fname, std::ios::binary | std::ios::trunc);
    if (!dst.is_open()) {
        return false;
    }
    dst << src.rdbuf();
    M5_LOGI("[SD-ALBUM] Snapshot + Frame Timelapse PIP salvos no Cartao SD: %s", fname);
    return true;
#else
    (void)event_tag;
    return false;
#endif
}

std::vector<std::string> OracleBrain::listSnapshotPipFilesFromSd() const {
    std::vector<std::string> files;
    if (!sd_storage_ready) {
        return files;
    }
#if defined(ARDUINO)
    if (!SD.exists(SD_PIP_INDEX_FILE)) {
        return files;
    }
    File f = SD.open(SD_PIP_INDEX_FILE, FILE_READ);
    if (!f) {
        return files;
    }
    while (f.available()) {
        String s = f.readStringUntil('\n');
        s.trim();
        if (s.length() > 4) {
            files.push_back(std::string(s.c_str()));
        }
    }
    f.close();
#else
    std::ifstream f(SD_PIP_INDEX_FILE, std::ios::binary);
    if (!f.is_open()) {
        return files;
    }
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.size() > 4) {
            files.push_back(line);
        }
    }
#endif
    // Mantém os últimos 12 quadros para um replay timelapse ágil
    if (files.size() > 12) {
        files.erase(files.begin(), files.end() - 12);
    }
    return files;
}

bool OracleBrain::loadPipRawFileFromSd(const std::string& pip_file_path, uint8_t* out_gray_64x48) const {
    if (!sd_storage_ready || out_gray_64x48 == nullptr || pip_file_path.empty()) {
        return false;
    }
#if defined(ARDUINO)
    if (!SD.exists(pip_file_path.c_str())) {
        return false;
    }
    File f = SD.open(pip_file_path.c_str(), FILE_READ);
    if (!f || f.size() < PIP_FRAME_BYTES) {
        if (f) f.close();
        return false;
    }
    size_t read_b = f.read(out_gray_64x48, PIP_FRAME_BYTES);
    f.close();
    return (read_b == PIP_FRAME_BYTES);
#else
    std::ifstream f(pip_file_path, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    f.read(reinterpret_cast<char*>(out_gray_64x48), static_cast<std::streamsize>(PIP_FRAME_BYTES));
    return (f.gcount() == static_cast<std::streamsize>(PIP_FRAME_BYTES));
#endif
}

std::vector<uint8_t> OracleBrain::decodeBase64(const std::string& b64_input) {
    std::vector<uint8_t> out;
    out.reserve((b64_input.size() * 3U) / 4U);

    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : b64_input) {
        int d = -1;
        if (c >= 'A' && c <= 'Z')      d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+')             d = 62;
        else if (c == '/')             d = 63;
        else if (c == '=')             break;
        else                           continue;

        val = (val << 6U) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFFU));
            valb -= 8;
        }
    }
    return out;
}

bool OracleBrain::synthesizeCloudNeuralTtsWav(const std::string& text_utf8, std::vector<uint8_t>& out_wav_24k) const {
    std::string api_key = resolveApiKey();
    if (api_key.empty() || text_utf8.empty()) {
        return false;
    }

    // Monta payload para o modelo gratuito gemini-2.5-flash-preview-tts (retorna PCM 24kHz 16-bit Mono em Base64)
    std::string tts_prompt =
        "Fale em Portugues Brasileiro com tom de robo cibernetico sarcastico e expressivo: " + text_utf8;
    std::string req_payload =
        "{"
        "\"contents\":[{\"parts\":[{\"text\":\"" + escapeJsonString(tts_prompt) + "\"}]}],"
        "\"generationConfig\":{"
            "\"responseModalities\":[\"AUDIO\"],"
            "\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"Orus\"}}}"
        "}"
        "}";

    std::string raw_resp;
#if defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    std::string full_url = std::string(GEMINI_TTS_API_ENDPOINT) + "?key=" + api_key;
    if (!http.begin(client, full_url.c_str())) {
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(9000);
    int code = http.POST(req_payload.c_str());
    if (code >= 200 && code < 300) {
        String s = http.getString();
        raw_resp = s.c_str();
    }
    http.end();
#else
    const char* tts_req_file = "gemini_tts_req.json";
    const char* tts_resp_file = "gemini_tts_resp.json";
    {
        std::ofstream req_out(tts_req_file, std::ios::binary | std::ios::trunc);
        if (!req_out.is_open()) {
            return false;
        }
        req_out.write(req_payload.data(), static_cast<std::streamsize>(req_payload.size()));
    }
    std::remove(tts_resp_file);
    std::string cmd =
        "curl -s -m 8 -X POST -H \"Content-Type: application/json\" --data-binary @" +
        std::string(tts_req_file) + " \"" +
        std::string(GEMINI_TTS_API_ENDPOINT) + "?key=" + api_key + "\" -o " +
        std::string(tts_resp_file) + " >nul 2>&1";
    if (std::system(cmd.c_str()) == 0) {
        std::ifstream resp_in(tts_resp_file, std::ios::binary);
        if (resp_in.is_open()) {
            std::ostringstream ss;
            ss << resp_in.rdbuf();
            raw_resp = ss.str();
        }
    }
    std::remove(tts_req_file);
    std::remove(tts_resp_file);
#endif

    if (raw_resp.empty()) {
        return false;
    }

    // Extrai o campo "data" dentro de inlineData (PCM 24000 Hz 16-bit signed little-endian)
    std::string b64_audio;
    if (!extractJsonStringField(raw_resp, "data", b64_audio) || b64_audio.size() < 128) {
        return false;
    }

    std::vector<uint8_t> pcm_raw = decodeBase64(b64_audio);
    if (pcm_raw.size() < 256) {
        return false;
    }

    // Constrói cabeçalho RIFF/WAVE de 44 bytes para 24.000 Hz, 16-bit PCM, Mono
    const uint32_t data_bytes = static_cast<uint32_t>(pcm_raw.size());
    const uint32_t riff_size = 36U + data_bytes;
    const uint32_t sample_rate = 24000U;
    const uint16_t channels = 1U;
    const uint16_t bits_per_sample = 16U;
    const uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8U);
    const uint16_t block_align = channels * (bits_per_sample / 8U);

    out_wav_24k.assign(44U + data_bytes, 0);
    auto writeLe16 = [&out_wav_24k](size_t off, uint16_t v) {
        out_wav_24k[off]     = static_cast<uint8_t>(v & 0xFFU);
        out_wav_24k[off + 1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
    };
    auto writeLe32 = [&out_wav_24k](size_t off, uint32_t v) {
        out_wav_24k[off]     = static_cast<uint8_t>(v & 0xFFU);
        out_wav_24k[off + 1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
        out_wav_24k[off + 2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
        out_wav_24k[off + 3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
    };

    std::memcpy(&out_wav_24k[0], "RIFF", 4);
    writeLe32(4, riff_size);
    std::memcpy(&out_wav_24k[8], "WAVE", 4);
    std::memcpy(&out_wav_24k[12], "fmt ", 4);
    writeLe32(16, 16U);
    writeLe16(20, 1U);
    writeLe16(22, channels);
    writeLe32(24, sample_rate);
    writeLe32(28, byte_rate);
    writeLe16(32, block_align);
    writeLe16(34, bits_per_sample);
    std::memcpy(&out_wav_24k[36], "data", 4);
    writeLe32(40, data_bytes);
    std::memcpy(&out_wav_24k[44], pcm_raw.data(), data_bytes);

    M5_LOGI("[GEMINI-TTS] Voz neural 24kHz HD sintetizada na nuvem (%u bytes WAV)!",
            static_cast<unsigned int>(out_wav_24k.size()));
    return true;
}

bool OracleBrain::saveMemoriesToStorage() const {
#if defined(ARDUINO)
    Preferences mem_prefs;
    if (!mem_prefs.begin(ORACLE_MEMORY_NVS_NAMESPACE, false)) {
        return false;
    }
    mem_prefs.putUInt("count", static_cast<uint32_t>(recent_memories.size()));
    for (size_t i = 0; i < recent_memories.size() && i < MAX_MEMORY_ITEMS; ++i) {
        char key_buf[16];
        std::snprintf(key_buf, sizeof(key_buf), "mem_%u", static_cast<unsigned int>(i));
        mem_prefs.putString(key_buf, recent_memories[i].c_str());
    }
    mem_prefs.end();
    return true;
#else
    std::ofstream mem_file(ORACLE_MEMORY_SAVE_FILE, std::ios::binary | std::ios::trunc);
    if (!mem_file.is_open()) {
        return false;
    }
    mem_file << "{\n  \"recent_memories\": [\n";
    for (size_t i = 0; i < recent_memories.size(); ++i) {
        mem_file << "    \"" << escapeJsonString(recent_memories[i]) << "\"";
        if (i + 1 < recent_memories.size()) {
            mem_file << ",";
        }
        mem_file << "\n";
    }
    mem_file << "  ]\n}\n";
    return true;
#endif
}

bool OracleBrain::loadMemoriesFromStorage() {
    // 1. Tenta carregar primeiro do Log Episódico Infinito no Cartão SD (episodic_log.jsonl)
    if (sd_storage_ready && loadRecentMemoriesFromEpisodicLog()) {
        return true;
    }

    // 2. Fallback / Migração Automática: lê da NVS (ESP32) ou oracle_memories.json (PC)
    bool loaded_legacy = false;
#if defined(ARDUINO)
    Preferences mem_prefs;
    if (mem_prefs.begin(ORACLE_MEMORY_NVS_NAMESPACE, true)) {
        uint32_t count = mem_prefs.getUInt("count", 0);
        if (count > 0) {
            recent_memories.clear();
            for (uint32_t i = 0; i < count && i < MAX_MEMORY_ITEMS; ++i) {
                char key_buf[16];
                std::snprintf(key_buf, sizeof(key_buf), "mem_%u", static_cast<unsigned int>(i));
                String item = mem_prefs.getString(key_buf, "");
                if (item.length() > 0) {
                    recent_memories.push_back(std::string(item.c_str()));
                }
            }
            loaded_legacy = !recent_memories.empty();
        }
        mem_prefs.end();
    }
#else
    std::ifstream mem_file(ORACLE_MEMORY_SAVE_FILE, std::ios::binary);
    if (mem_file.is_open()) {
        recent_memories.clear();
        std::string line;
        while (std::getline(mem_file, line)) {
            if (line.find("recent_memories") != std::string::npos) {
                continue;
            }
            size_t first_q = line.find('\"');
            if (first_q != std::string::npos) {
                size_t last_q = line.rfind('\"');
                if (last_q != std::string::npos && last_q > first_q) {
                    std::string fact = line.substr(first_q + 1, last_q - first_q - 1);
                    if (!fact.empty() && recent_memories.size() < MAX_MEMORY_ITEMS) {
                        recent_memories.push_back(fact);
                    }
                }
            }
        }
        loaded_legacy = !recent_memories.empty();
    }
#endif

    // Se migrou memórias legadas e o Cartão SD está ativo, importa todas para o episodic_log.jsonl!
    if (loaded_legacy && sd_storage_ready) {
        std::string cur_date;
        uint8_t cur_hour = 12;
        getCurrentDateAndHour(cur_date, cur_hour);
        for (const auto& legacy_fact : recent_memories) {
            EpisodicMemoryRecord rec;
            rec.date_str = cur_date;
            rec.hour_of_day = cur_hour;
            rec.trigger_reason = "LEGACY_MIGRATION";
            rec.mood_score = 72;
            rec.tags = "historico,migracao,codigo,postura,cafe";
            rec.memory_fact = legacy_fact;
            appendEpisodicRecordToSd(rec);
        }
        M5_LOGI("[SD-MEM] Migradas %u memorias de '%s' para '%s'!",
                static_cast<unsigned int>(recent_memories.size()),
                ORACLE_MEMORY_SAVE_FILE,
                SD_EPISODIC_LOG_FILE);
    }

    return loaded_legacy;
}
