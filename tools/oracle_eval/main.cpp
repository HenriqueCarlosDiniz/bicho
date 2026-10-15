// =============================================================================
// Regressão do prompt do Oráculo: monta o prompt com o MESMO oracle_prompt.cpp do firmware,
// chama o Gemini para cada caso de tools/oracle_eval/cases.json e confere as respostas.
//
//   python -m platformio run -e native_eval -t exec            (todos os casos, chama a API)
//   BICHO_EVAL_OFFLINE=1 ... -t exec                           (só tamanhos e filtros do contexto)
//   BICHO_EVAL_ONLY=voz_luto ... -t exec                       (um caso)
//
// Sai com código 1 se algum caso falhar (dá para usar em CI com a chave da API como segredo).
// =============================================================================
#include <ArduinoJson.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include "json_utils.h"
#include "oracle_prompt.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {

const char* const REQ_FILE = "oracle_eval_req.json";
const char* const RESP_FILE = "oracle_eval_resp.json";
#if defined(_WIN32)
const char* const NULL_REDIRECT = " >nul 2>&1";
#else
const char* const NULL_REDIRECT = " >/dev/null 2>&1";
#endif

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string base64(const std::string& data) {
    static const char* const TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t n = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8) | uint8_t(data[i + 2]);
        out += TABLE[(n >> 18) & 63]; out += TABLE[(n >> 12) & 63]; out += TABLE[(n >> 6) & 63]; out += TABLE[n & 63];
        i += 3;
    }
    if (i < data.size()) {
        uint32_t n = uint8_t(data[i]) << 16;
        if (i + 1 < data.size()) n |= uint8_t(data[i + 1]) << 8;
        out += TABLE[(n >> 18) & 63]; out += TABLE[(n >> 12) & 63];
        out += (i + 1 < data.size()) ? TABLE[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string apiKey() {
    const char* env = std::getenv("GEMINI_API_KEY");
    if (env != nullptr && std::strlen(env) > 10) return env;
    if (std::strlen(AI_API_KEY) > 10 && std::strcmp(AI_API_KEY, "SUA_CHAVE_DE_API_MULTIMODAL_AQUI") != 0) return AI_API_KEY;
    return "";
}

bool quota_exhausted = false;

// POST via curl com a chave num arquivo de headers (fora da linha de comando).
// Espera BICHO_EVAL_DELAY_MS (padrão 4 s) entre chamadas para respeitar o limite por minuto da API.
bool postGemini(const std::string& payload, std::string& out_response) {
    static bool first_call = true;
    const char* delay_env = std::getenv("BICHO_EVAL_DELAY_MS");
    const int delay_ms = (delay_env != nullptr) ? std::atoi(delay_env) : 4000;
    if (!first_call && delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    first_call = false;
    const std::string key = apiKey();
    if (key.empty()) return false;
    const std::string header_file = std::string(REQ_FILE) + ".hdr";
    {
        std::ofstream h(header_file, std::ios::binary);
        h << "x-goog-api-key: " << key << "\nContent-Type: application/json\n";
        std::ofstream r(REQ_FILE, std::ios::binary);
        r << payload;
    }
    std::remove(RESP_FILE);
    std::string cmd = std::string("curl -s -m 40 -X POST -H @") + header_file + " --data-binary @" + REQ_FILE +
                      " \"" + AI_API_ENDPOINT + "\" -o " + RESP_FILE + NULL_REDIRECT;
    int rc = std::system(cmd.c_str());
    std::remove(header_file.c_str());
    std::remove(REQ_FILE);
    bool ok = (rc == 0) && readFile(RESP_FILE, out_response) && !out_response.empty();
    std::remove(RESP_FILE);
    return ok;
}

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (char c : s) if ((static_cast<uint8_t>(c) & 0xC0U) != 0x80U) ++n;
    return n;
}

std::vector<std::string> toStrings(JsonVariantConst v) {
    std::vector<std::string> out;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
        const char* s = item.as<const char*>();
        if (s != nullptr) out.emplace_back(s);
    }
    return out;
}

void applyInputs(OraclePromptInputs& in, JsonVariantConst src) {
    if (src.isNull()) return;
    if (src["emotion_code"].is<const char*>()) in.emotion_code = src["emotion_code"].as<const char*>();
    if (src["mood_score"].is<int>()) in.mood_score = src["mood_score"];
    if (src["energy_level"].is<int>()) in.energy_level = src["energy_level"];
    if (src["hunger_level"].is<int>()) in.hunger_level = src["hunger_level"];
    if (src["focus_minutes"].is<int>()) in.focus_minutes = src["focus_minutes"];
    if (src["world_summary"].is<const char*>()) in.world_summary = src["world_summary"].as<const char*>();
    if (src["host_posture_summary"].is<const char*>()) in.host_posture_summary = src["host_posture_summary"].as<const char*>();
    if (src["habit_summary"].is<const char*>()) in.habit_summary = src["habit_summary"].as<const char*>();
    if (src["diary_json"].is<const char*>()) in.diary_json = src["diary_json"].as<const char*>();
    if (src["profile_facts"].is<JsonArrayConst>()) in.profile_facts = toStrings(src["profile_facts"]);
    if (src["recent_memories"].is<JsonArrayConst>()) in.recent_memories = toStrings(src["recent_memories"]);
    if (src["recent_replies"].is<JsonArrayConst>()) in.recent_replies = toStrings(src["recent_replies"]);
    if (src["conversation_lines"].is<JsonArrayConst>()) in.conversation_lines = toStrings(src["conversation_lines"]);
}

// Juiz: segunda chamada que responde {"pass": bool, "reason": "..."} para critérios subjetivos
bool judge(const std::string& criterion, const std::string& user_said, const std::string& reply, std::string& reason) {
    JsonDocument req;
    req["system_instruction"]["parts"][0]["text"] =
        "Você avalia respostas de um assistente de mesa bem-humorado. Responda só JSON {\"pass\": true|false, \"reason\": \"curto\"}.";
    std::string prompt = "Critério: " + criterion + "\nFala do humano (pode estar vazia): " + user_said +
                         "\nResposta do assistente: " + reply;
    req["contents"][0]["role"] = "user";
    req["contents"][0]["parts"][0]["text"] = prompt;
    req["generationConfig"]["response_mime_type"] = "application/json";
    req["generationConfig"]["temperature"] = 0.0;
    std::string payload, raw;
    serializeJson(req, payload);
    if (!postGemini(payload, raw)) { reason = "sem resposta do juiz"; return false; }
    JsonDocument env, verdict;
    if (deserializeJson(env, raw) != DeserializationError::Ok) { reason = "juiz: resposta inválida"; return false; }
    const char* text = env["candidates"][0]["content"]["parts"][0]["text"];
    std::string inner = (text != nullptr) ? text : "";
    size_t a = inner.find('{'), b = inner.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b < a ||
        deserializeJson(verdict, inner.substr(a, b - a + 1)) != DeserializationError::Ok) {
        reason = "juiz: JSON inválido";
        return false;
    }
    reason = verdict["reason"] | "";
    return verdict["pass"] | false;
}

} // namespace

int main() {
    const bool offline = std::getenv("BICHO_EVAL_OFFLINE") != nullptr;
    const char* only = std::getenv("BICHO_EVAL_ONLY");

    std::string cases_text;
    if (!readFile("tools/oracle_eval/cases.json", cases_text)) {
        std::cerr << "Nao achei tools/oracle_eval/cases.json (rode a partir da raiz do projeto)\n";
        return 2;
    }
    JsonDocument cases_doc;
    if (deserializeJson(cases_doc, cases_text) != DeserializationError::Ok) {
        std::cerr << "cases.json invalido\n";
        return 2;
    }
    if (!offline && apiKey().empty()) {
        std::cerr << "Sem chave da API (secrets.h ou GEMINI_API_KEY). Use BICHO_EVAL_OFFLINE=1 para so checar o contexto.\n";
        return 2;
    }

    int failures = 0, total = 0;
    for (JsonVariantConst c : cases_doc["cases"].as<JsonArrayConst>()) {
        const std::string id = c["id"] | "";
        if (only != nullptr && id != only) continue;
        ++total;

        OraclePromptInputs in;
        in.trigger_reason = c["trigger"] | "TOUCH_POKE";
        applyInputs(in, cases_doc["defaults"]);
        applyInputs(in, c["overrides"]);

        std::string wav_b64;
        const char* audio = c["audio"];
        if (audio != nullptr) {
            std::string wav;
            if (!readFile(audio, wav)) { std::cout << "[FALHA] " << id << ": audio nao encontrado " << audio << "\n"; ++failures; continue; }
            wav_b64 = base64(wav);
            in.has_audio = true;
        }

        JsonVariantConst checks = c["checks"];
        const int repeat = c["repeat"] | 1;
        std::vector<std::string> failures_here;
        std::vector<std::string> openings;
        int lens = -1;

        for (int run = 0; run < repeat; ++run) {
            lens = static_cast<int>(chooseCreativeLens(static_cast<uint32_t>(std::rand()) * 2654435761U + run, lens));
            in.lens_index = lens;
            const std::string telemetry = buildOracleTelemetryJson(in);
            if (run == 0) {
                std::cout << "\n== " << id << " (" << in.trigger_reason << ") | telemetria " << telemetry.size() << " bytes\n";
                for (const std::string& term : toStrings(checks["telemetry_excludes"])) {
                    if (telemetry.find(term) != std::string::npos) failures_here.push_back("contexto contem '" + term + "'");
                }
            }
            if (offline) break;

            std::string raw;
            OracleReplyFields fields;
            std::string api_error;
            if (!postGemini(buildOracleRequestPayload(telemetry, "", wav_b64), raw) ||
                parseGeminiReply(raw, fields, api_error) != GEMINI_PARSE_OK) {
                if (api_error.find("quota") != std::string::npos) {
                    quota_exhausted = true;
                    std::cout << "   [SEM COTA] limite da API atingido; caso nao avaliado\n";
                    break;
                }
                failures_here.push_back("sem resposta valida da API " + api_error);
                continue;
            }
            const std::string reply = fields.reply_text;
            const std::string reply_l = lower(reply);
            std::cout << "   ouviu: \"" << fields.user_said << "\"\n   disse: \"" << reply << "\"\n";

            const bool is_ignore = (reply == "IGNORE" || reply == "[IGNORE]");
            if ((checks["expect_ignore"] | false) && !is_ignore) failures_here.push_back("esperava IGNORE");
            if ((checks["not_ignore"] | false) && is_ignore) failures_here.push_back("ignorou a fala");
            if (!is_ignore && utf8Length(reply) > (checks["max_chars"] | 115)) failures_here.push_back("passou do limite de caracteres");

            std::vector<std::string> any = toStrings(checks["contains_any"]);
            if (!any.empty()) {
                bool found = false;
                for (const std::string& term : any) found = found || reply_l.find(lower(term)) != std::string::npos;
                if (!found) failures_here.push_back("nao contem nenhum de contains_any");
            }
            for (const std::string& term : toStrings(checks["not_contains"])) {
                if (reply_l.find(lower(term)) != std::string::npos) failures_here.push_back("contem '" + term + "'");
            }
            for (const std::string& prefix : toStrings(checks["not_starts_with"])) {
                if (reply_l.rfind(lower(prefix), 0) == 0) failures_here.push_back("comeca com '" + prefix + "'");
            }
            const char* criterion = checks["judge"];
            if (criterion != nullptr && !is_ignore) {
                std::string reason;
                bool pass = judge(criterion, fields.user_said, reply, reason);
                std::cout << "   juiz: " << (pass ? "OK" : "NAO") << " - " << reason << "\n";
                if (!pass) failures_here.push_back("juiz reprovou");
            }
            openings.push_back(lower(replyOpening(reply, 2)));
            in.recent_replies.push_back(reply); // Próxima repetição enxerga as falas anteriores, como no Bicho
        }

        if ((checks["distinct_openings"] | false) && openings.size() > 1) {
            for (size_t i = 0; i < openings.size(); ++i)
                for (size_t j = i + 1; j < openings.size(); ++j)
                    if (openings[i] == openings[j]) failures_here.push_back("aberturas repetidas: '" + openings[i] + "'");
        }

        if (quota_exhausted) {
            std::cout << "\nCota da API esgotada: avaliacao interrompida (tente mais tarde ou com outra chave).\n";
            return 3;
        }
        if (failures_here.empty()) {
            std::cout << "   [OK]\n";
        } else {
            ++failures;
            for (const std::string& f : failures_here) std::cout << "   [FALHA] " << f << "\n";
        }
    }

    std::cout << "\nResultado: " << (total - failures) << "/" << total << " casos OK" << (offline ? " (modo offline)" : "") << "\n";
    return failures == 0 ? 0 : 1;
}
