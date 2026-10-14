#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdint.h>
#include <string>

// =============================================================================
// Parsers JSON puros (ArduinoJson 7) sem dependência de hardware/rede.
// Usados pelo Cérebro (Gemini), pelo Mundo Externo (APIs públicas) e pela HAL (config/save),
// e cobertos pelos testes de unidade em test/test_json_utils.
// =============================================================================

// Leitura de um campo de nível superior de um objeto JSON (documento inteiro ou linha JSONL)
bool jsonGetString(const std::string& json_src, const char* key, std::string& out_val);
bool jsonGetInt(const std::string& json_src, const char* key, int32_t& out_val);
bool jsonGetFloat(const std::string& json_src, const char* key, float& out_val);
bool jsonGetBool(const std::string& json_src, const char* key, bool& out_val);

// ---------------------------------------------------------------------------- Gemini
struct OracleReplyFields {
    std::string user_said;          // Transcrição do áudio do humano (vazia quando não houve fala)
    std::string reply_text;
    std::string target_emotion;
    int32_t mood_delta = 0;
    int32_t energy_delta = 0;
    int32_t hunger_delta = 0;
    std::string new_memory;
    std::string permanent_fact;
    std::string memory_tags;
};

enum GeminiParseStatus {
    GEMINI_PARSE_OK = 0,
    GEMINI_PARSE_API_ERROR,   // A API respondeu {"error": {...}}
    GEMINI_PARSE_INVALID      // Resposta vazia, malformada ou sem reply_text
};

// Aceita o envelope do generateContent (candidates[0].content.parts[0].text com o JSON da persona,
// inclusive cercado por ```json ... ```) ou o JSON da persona direto (mock/teste).
GeminiParseStatus parseGeminiReply(const std::string& raw_api_response,
                                   OracleReplyFields& out_fields,
                                   std::string& out_error_message);

// Extrai o áudio Base64 de candidates[0].content.parts[0].inlineData.data (Gemini TTS)
bool parseGeminiTtsAudioBase64(const std::string& raw_api_response, std::string& out_b64);

// ---------------------------------------------------------------------------- Mundo externo
struct WeatherReading {
    float temperature_c = 0.0f;
    int32_t humidity_percent = 0;
    bool is_day = true;
    float precipitation_mm = 0.0f;
    int32_t weather_code = 0;
};
bool parseOpenMeteoCurrent(const std::string& body, WeatherReading& out_reading);

struct FinanceReading {
    bool has_usd = false;
    float usd_brl_bid = 0.0f;
    float usd_pct_change = 0.0f;
    bool has_btc = false;
    float btc_pct_change = 0.0f;
};
bool parseAwesomeFinance(const std::string& body, FinanceReading& out_reading);

bool parseGithubStatus(const std::string& body, std::string& out_indicator, std::string& out_description);

// Monta o rótulo "HOJE: X", "AMANHÃ: X", "X (YYYY-MM-DD)" ou "Sem feriado próximo"
bool parseBrasilHolidays(const std::string& body,
                         const std::string& today_ymd,
                         const std::string& tomorrow_ymd,
                         std::string& out_label);

bool parseHackerNewsTopId(const std::string& body, long& out_id);
bool parseHackerNewsTitle(const std::string& body, std::string& out_title);
bool parseScryfallCard(const std::string& body, std::string& out_name, std::string& out_type_line);
bool parsePokemonSpeciesName(const std::string& body, std::string& out_name);

#endif // JSON_UTILS_H
