// =============================================================================
// Teste de bancada da sessão Live (transporte WinHTTP + protocolo), sem o emulador:
//   .pio/build/native_live_probe/program.exe <entrada_16k.wav> [saida_24k.wav]
// Envia o WAV no ritmo real, imprime o que o Gemini ouviu e falou e a latência até o primeiro áudio.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "live_session.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static bool readWavPcm(const char* path, std::vector<int16_t>& out) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44) return false;
    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        uint32_t size = static_cast<uint8_t>(bytes[pos + 4]) | (static_cast<uint8_t>(bytes[pos + 5]) << 8) |
                        (static_cast<uint8_t>(bytes[pos + 6]) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[pos + 7])) << 24);
        if (std::memcmp(&bytes[pos], "data", 4) == 0) {
            size_t n = std::min<size_t>(size, bytes.size() - pos - 8) / 2;
            out.resize(n);
            std::memcpy(out.data(), &bytes[pos + 8], n * 2);
            return true;
        }
        pos += 8 + size + (size & 1U);
    }
    return false;
}

static void writeWav(const char* path, const std::vector<int16_t>& pcm, uint32_t rate) {
    std::ofstream f(path, std::ios::binary);
    uint32_t data = static_cast<uint32_t>(pcm.size() * 2), riff = 36 + data, byte_rate = rate * 2;
    uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    uint32_t fmt_size = 16;
    f.write("RIFF", 4); f.write(reinterpret_cast<char*>(&riff), 4); f.write("WAVEfmt ", 8);
    f.write(reinterpret_cast<char*>(&fmt_size), 4); f.write(reinterpret_cast<char*>(&fmt), 2);
    f.write(reinterpret_cast<char*>(&ch), 2); f.write(reinterpret_cast<char*>(&rate), 4);
    f.write(reinterpret_cast<char*>(&byte_rate), 4); f.write(reinterpret_cast<char*>(&align), 2);
    f.write(reinterpret_cast<char*>(&bits), 2); f.write("data", 4); f.write(reinterpret_cast<char*>(&data), 4);
    f.write(reinterpret_cast<const char*>(pcm.data()), data);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: program.exe entrada_16k.wav [saida_24k.wav]\n";
        return 2;
    }
    std::vector<int16_t> input;
    if (!readWavPcm(argv[1], input)) {
        std::cerr << "WAV invalido\n";
        return 2;
    }
    LiveSetupOptions options;
    options.system_instruction = "Você é o Bicho, companheiro de mesa bem-humorado. Responda curto em português do Brasil. "
                                 "Dados de hoje: dólar R$ 5,05; 20 graus e úmido.";
    LiveSession session;
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    };
    if (!session.start(AI_API_KEY, options, {})) {
        std::cerr << "nao foi possivel iniciar (transporte indisponivel?)\n";
        return 1;
    }

    std::vector<int16_t> output;
    std::string heard, said;
    long ready_at = -1, speech_end = -1, first_audio = -1;
    size_t sent = 0;
    const size_t step = LIVE_INPUT_SAMPLE_RATE / 10;
    bool done = false;
    while (!done && ms() < 45000) {
        LiveEvent ev;
        while (session.popEvent(ev)) {
            if (ev.type == LIVE_EVENT_READY) ready_at = ms();
            if (ev.type == LIVE_EVENT_INPUT_TEXT) heard += ev.text;
            if (ev.type == LIVE_EVENT_OUTPUT_TEXT) said += ev.text;
            if (ev.type == LIVE_EVENT_TURN_COMPLETE && first_audio >= 0) done = true;
            if (ev.type == LIVE_EVENT_CLOSED) { std::cout << "fechou: " << ev.text << "\n"; done = true; }
        }
        std::vector<int16_t> chunk;
        if (session.popAudio(chunk)) {
            if (first_audio < 0) first_audio = ms();
            output.insert(output.end(), chunk.begin(), chunk.end());
        }
        if (session.isReady()) {
            // Fala no ritmo real e depois 1,5 s de silêncio para o VAD do servidor fechar o turno
            std::vector<int16_t> block(step, 0);
            if (sent < input.size()) {
                size_t n = std::min(step, input.size() - sent);
                std::memcpy(block.data(), &input[sent], n * 2);
                sent += n;
                if (sent >= input.size()) speech_end = ms();
            }
            if (sent < input.size() + LIVE_INPUT_SAMPLE_RATE * 3 / 2) {
                session.pushMicAudio(block.data(), block.size());
                if (sent >= input.size()) sent += step;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    session.stop();
    std::cout << "pronto em: " << ready_at << " ms\n";
    std::cout << "ouviu: " << heard << "\n";
    std::cout << "disse: " << said << "\n";
    if (first_audio >= 0 && speech_end >= 0) {
        std::cout << "primeiro audio " << (first_audio - speech_end) << " ms apos o fim da fala\n";
    }
    std::cout << "audio: " << output.size() / static_cast<double>(LIVE_OUTPUT_SAMPLE_RATE) << " s\n";
    if (argc >= 3 && !output.empty()) writeWav(argv[2], output, LIVE_OUTPUT_SAMPLE_RATE);
    return output.empty() ? 1 : 0;
}
