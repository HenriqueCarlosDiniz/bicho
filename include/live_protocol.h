#ifndef LIVE_PROTOCOL_H
#define LIVE_PROTOCOL_H

#include <stdint.h>
#include <string>
#include <vector>

// =============================================================================
// Protocolo da API Live do Gemini (BidiGenerateContent via WebSocket) — funções puras.
// Entrada: PCM 16 bits mono 16 kHz | Saída: PCM 16 bits mono 24 kHz.
// =============================================================================

constexpr const char* LIVE_API_HOST = "generativelanguage.googleapis.com";
constexpr const char* LIVE_API_PATH =
    "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
constexpr uint32_t LIVE_INPUT_SAMPLE_RATE = 16000;
constexpr uint32_t LIVE_OUTPUT_SAMPLE_RATE = 24000;

struct LiveSetupOptions {
    std::string model = "gemini-3.8-live";
    std::string voice = "Orus";
    std::string system_instruction;
};

// Primeira mensagem da sessão (modelo, voz, persona, transcrições de entrada/saída)
std::string buildLiveSetupMessage(const LiveSetupOptions& options);

// Trecho de áudio do microfone (PCM16 16 kHz)
std::string buildLiveAudioMessage(const int16_t* samples, size_t count);

// Mensagem do servidor já interpretada
struct LiveServerMessage {
    bool setup_complete = false;
    bool turn_complete = false;
    bool interrupted = false;      // O humano falou por cima: parar a fala do Bicho
    bool go_away = false;          // O servidor vai encerrar a conexão em breve
    std::string input_transcript;  // Pedaço do que o humano disse
    std::string output_transcript; // Pedaço do que o Bicho está dizendo
    std::vector<int16_t> audio;    // PCM16 24 kHz da fala do Bicho
    std::string error;             // Mensagem de erro, se houver
};

bool parseLiveServerMessage(const std::string& json_text, LiveServerMessage& out);

// Base64 (usado pelo protocolo; exposto para testes)
std::string liveBase64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> liveBase64Decode(const char* text);

#endif // LIVE_PROTOCOL_H
