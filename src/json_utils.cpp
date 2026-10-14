#include "json_utils.h"
#include <ArduinoJson.h>
#include <cstdlib>

namespace {

// Números podem chegar como número JSON ou como string ("5.0312" na AwesomeAPI, "+5" vindo do modelo)
bool variantToFloat(JsonVariantConst v, float& out_val) {
    if (v.is<float>()) {
        out_val = v.as<float>();
        return true;
    }
    const char* s = v.as<const char*>();
    if (s == nullptr || *s == '\0') {
        return false;
    }
    char* end_ptr = nullptr;
    float parsed = std::strtof(s, &end_ptr);
    if (end_ptr == s) {
        return false;
    }
    out_val = parsed;
    return true;
}

bool variantToInt(JsonVariantConst v, int32_t& out_val) {
    if (v.is<int32_t>()) {
        out_val = v.as<int32_t>();
        return true;
    }
    if (v.is<float>()) {
        out_val = static_cast<int32_t>(v.as<float>());
        return true;
    }
    const char* s = v.as<const char*>();
    if (s == nullptr || *s == '\0') {
        return false;
    }
    char* end_ptr = nullptr;
    long parsed = std::strtol(s, &end_ptr, 10);
    if (end_ptr == s) {
        return false;
    }
    out_val = static_cast<int32_t>(parsed);
    return true;
}

bool variantToString(JsonVariantConst v, std::string& out_val) {
    const char* s = v.as<const char*>();
    if (s == nullptr) {
        return false;
    }
    out_val = s;
    return true;
}

// Texto curto para a tela: quebras de linha/tabulações viram espaço
std::string flattenWhitespace(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    return out;
}

bool parseObject(const std::string& json_src, JsonDocument& doc) {
    return deserializeJson(doc, json_src) == DeserializationError::Ok && doc.is<JsonObjectConst>();
}

} // namespace

bool jsonGetString(const std::string& json_src, const char* key, std::string& out_val) {
    JsonDocument doc;
    return parseObject(json_src, doc) && variantToString(doc[key], out_val);
}

bool jsonGetInt(const std::string& json_src, const char* key, int32_t& out_val) {
    JsonDocument doc;
    return parseObject(json_src, doc) && variantToInt(doc[key], out_val);
}

bool jsonGetFloat(const std::string& json_src, const char* key, float& out_val) {
    JsonDocument doc;
    return parseObject(json_src, doc) && variantToFloat(doc[key], out_val);
}

bool jsonGetBool(const std::string& json_src, const char* key, bool& out_val) {
    JsonDocument doc;
    if (!parseObject(json_src, doc) || !doc[key].is<bool>()) {
        return false;
    }
    out_val = doc[key].as<bool>();
    return true;
}

GeminiParseStatus parseGeminiReply(const std::string& raw_api_response,
                                   OracleReplyFields& out_fields,
                                   std::string& out_error_message) {
    if (raw_api_response.empty()) {
        return GEMINI_PARSE_INVALID;
    }

    JsonDocument envelope;
    if (deserializeJson(envelope, raw_api_response) != DeserializationError::Ok) {
        return GEMINI_PARSE_INVALID;
    }

    if (!envelope["error"].isNull()) {
        const char* msg = envelope["error"]["message"] | "erro desconhecido";
        out_error_message = msg;
        return GEMINI_PARSE_API_ERROR;
    }

    // O JSON da persona vem como string em candidates[0].content.parts[0].text (ou direto, em mocks)
    JsonDocument persona_doc;
    const char* part_text = envelope["candidates"][0]["content"]["parts"][0]["text"];
    if (part_text != nullptr) {
        std::string inner(part_text);
        size_t first_brace = inner.find('{');
        size_t last_brace = inner.rfind('}');
        if (first_brace == std::string::npos || last_brace == std::string::npos || last_brace < first_brace) {
            return GEMINI_PARSE_INVALID;
        }
        inner = inner.substr(first_brace, last_brace - first_brace + 1); // Remove cercas ```json ... ```
        if (deserializeJson(persona_doc, inner) != DeserializationError::Ok) {
            return GEMINI_PARSE_INVALID;
        }
    } else {
        persona_doc.set(envelope.as<JsonVariantConst>());
    }

    JsonVariantConst persona = persona_doc.as<JsonVariantConst>();
    std::string reply;
    if (!variantToString(persona["reply_text"], reply) || reply.empty()) {
        return GEMINI_PARSE_INVALID;
    }

    out_fields = OracleReplyFields();
    out_fields.reply_text = flattenWhitespace(reply);
    if (variantToString(persona["user_said"], out_fields.user_said)) {
        out_fields.user_said = flattenWhitespace(out_fields.user_said);
    }
    variantToString(persona["target_emotion"], out_fields.target_emotion);
    variantToInt(persona["mood_delta"], out_fields.mood_delta);
    variantToInt(persona["energy_delta"], out_fields.energy_delta);
    variantToInt(persona["hunger_delta"], out_fields.hunger_delta);
    if (variantToString(persona["new_memory"], out_fields.new_memory)) {
        out_fields.new_memory = flattenWhitespace(out_fields.new_memory);
    }
    if (variantToString(persona["permanent_fact"], out_fields.permanent_fact)) {
        out_fields.permanent_fact = flattenWhitespace(out_fields.permanent_fact);
    }
    variantToString(persona["memory_tags"], out_fields.memory_tags);
    return GEMINI_PARSE_OK;
}

bool parseGeminiTtsAudioBase64(const std::string& raw_api_response, std::string& out_b64) {
    JsonDocument doc;
    if (deserializeJson(doc, raw_api_response) != DeserializationError::Ok) {
        return false;
    }
    JsonVariantConst part = doc["candidates"][0]["content"]["parts"][0];
    const char* data = part["inlineData"]["data"];
    if (data == nullptr) {
        data = part["inline_data"]["data"];
    }
    if (data == nullptr) {
        return false;
    }
    out_b64 = data;
    return true;
}

bool parseOpenMeteoCurrent(const std::string& body, WeatherReading& out_reading) {
    JsonDocument filter;
    filter["current"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
        return false;
    }
    JsonVariantConst current = doc["current"];
    if (!variantToFloat(current["temperature_2m"], out_reading.temperature_c)) {
        return false;
    }
    variantToInt(current["relative_humidity_2m"], out_reading.humidity_percent);
    int32_t is_day_val = out_reading.is_day ? 1 : 0;
    if (variantToInt(current["is_day"], is_day_val)) {
        out_reading.is_day = (is_day_val != 0);
    }
    variantToFloat(current["precipitation"], out_reading.precipitation_mm);
    variantToInt(current["weather_code"], out_reading.weather_code);
    return true;
}

bool parseAwesomeFinance(const std::string& body, FinanceReading& out_reading) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        return false;
    }
    JsonVariantConst usd = doc["USDBRL"];
    if (variantToFloat(usd["bid"], out_reading.usd_brl_bid)) {
        out_reading.has_usd = true;
        variantToFloat(usd["pctChange"], out_reading.usd_pct_change);
    }
    if (variantToFloat(doc["BTCBRL"]["pctChange"], out_reading.btc_pct_change)) {
        out_reading.has_btc = true;
    }
    return out_reading.has_usd || out_reading.has_btc;
}

bool parseGithubStatus(const std::string& body, std::string& out_indicator, std::string& out_description) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        return false;
    }
    if (!variantToString(doc["status"]["indicator"], out_indicator)) {
        return false;
    }
    variantToString(doc["status"]["description"], out_description);
    return true;
}

bool parseBrasilHolidays(const std::string& body,
                         const std::string& today_ymd,
                         const std::string& tomorrow_ymd,
                         std::string& out_label) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonArrayConst>()) {
        return false;
    }

    std::string next_name;
    std::string next_date;
    for (JsonVariantConst holiday : doc.as<JsonArrayConst>()) {
        std::string date;
        std::string name;
        if (!variantToString(holiday["date"], date) || !variantToString(holiday["name"], name)) {
            continue;
        }
        if (date == today_ymd) {
            out_label = "HOJE: " + name;
            return true;
        }
        if (date == tomorrow_ymd) {
            out_label = "AMANHÃ: " + name;
            return true;
        }
        if (date >= today_ymd && (next_date.empty() || date < next_date)) {
            next_date = date;
            next_name = name;
        }
    }

    out_label = next_date.empty() ? "Sem feriado próximo" : (next_name + " (" + next_date + ")");
    return true;
}

bool parseHackerNewsTopId(const std::string& body, long& out_id) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc[0].is<long>()) {
        return false;
    }
    out_id = doc[0].as<long>();
    return out_id > 0;
}

bool parseHackerNewsTitle(const std::string& body, std::string& out_title) {
    return jsonGetString(body, "title", out_title) && !out_title.empty();
}

bool parseScryfallCard(const std::string& body, std::string& out_name, std::string& out_type_line) {
    JsonDocument filter;
    filter["name"] = true;
    filter["type_line"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
        return false;
    }
    if (!variantToString(doc["name"], out_name) || out_name.empty()) {
        return false;
    }
    variantToString(doc["type_line"], out_type_line);
    return true;
}

bool parsePokemonSpeciesName(const std::string& body, std::string& out_name) {
    JsonDocument filter;
    filter["name"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
        return false;
    }
    return variantToString(doc["name"], out_name) && !out_name.empty();
}
