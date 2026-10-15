#include "live_protocol.h"
#include <ArduinoJson.h>
#include <cstring>

namespace {
const char* const B64_TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
} // namespace

std::string liveBase64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += B64_TABLE[(n >> 18) & 63];
        out += B64_TABLE[(n >> 12) & 63];
        out += B64_TABLE[(n >> 6) & 63];
        out += B64_TABLE[n & 63];
        i += 3;
    }
    if (i < len) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
        out += B64_TABLE[(n >> 18) & 63];
        out += B64_TABLE[(n >> 12) & 63];
        out += (i + 1 < len) ? B64_TABLE[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::vector<uint8_t> liveBase64Decode(const char* text) {
    std::vector<uint8_t> out;
    if (text == nullptr) return out;
    out.reserve(std::strlen(text) * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        int v = b64Value(*p);
        if (v < 0) continue; // Ignora '=', quebras de linha etc.
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFU));
        }
    }
    return out;
}

std::string buildLiveSetupMessage(const LiveSetupOptions& options) {
    JsonDocument doc;
    JsonObject setup = doc["setup"].to<JsonObject>();
    setup["model"] = "models/" + options.model;
    JsonObject gen = setup["generationConfig"].to<JsonObject>();
    gen["responseModalities"].to<JsonArray>().add("AUDIO");
    gen["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = options.voice;
    if (!options.system_instruction.empty()) {
        setup["systemInstruction"]["parts"][0]["text"] = options.system_instruction;
    }
    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string buildLiveAudioMessage(const int16_t* samples, size_t count) {
    std::string b64 = liveBase64Encode(reinterpret_cast<const uint8_t*>(samples), count * sizeof(int16_t));
    std::string out;
    out.reserve(b64.size() + 96);
    out += "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    out += b64;
    out += "\"}}}";
    return out;
}

bool parseLiveServerMessage(const std::string& json_text, LiveServerMessage& out) {
    out = LiveServerMessage();
    JsonDocument doc;
    if (deserializeJson(doc, json_text) != DeserializationError::Ok) {
        return false;
    }
    if (!doc["error"].isNull()) {
        out.error = doc["error"]["message"] | "erro desconhecido";
        return true;
    }
    if (!doc["setupComplete"].isNull()) out.setup_complete = true;
    if (!doc["goAway"].isNull()) out.go_away = true;

    JsonVariantConst sc = doc["serverContent"];
    if (!sc.isNull()) {
        out.turn_complete = sc["turnComplete"] | false;
        out.interrupted = sc["interrupted"] | false;
        out.input_transcript = sc["inputTranscription"]["text"] | "";
        out.output_transcript = sc["outputTranscription"]["text"] | "";
        for (JsonVariantConst part : sc["modelTurn"]["parts"].as<JsonArrayConst>()) {
            const char* data = part["inlineData"]["data"];
            if (data == nullptr) continue;
            std::vector<uint8_t> bytes = liveBase64Decode(data);
            size_t samples = bytes.size() / 2;
            size_t base = out.audio.size();
            out.audio.resize(base + samples);
            std::memcpy(out.audio.data() + base, bytes.data(), samples * 2);
        }
    }
    return true;
}
