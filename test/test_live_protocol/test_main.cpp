#include <unity.h>
#include <string>
#include <vector>
#include "live_protocol.h"

void setUp() {}
void tearDown() {}

void test_base64_roundtrip() {
    const uint8_t data[] = {0, 1, 2, 250, 255, 128, 7};
    for (size_t len = 0; len <= sizeof(data); ++len) {
        std::string b64 = liveBase64Encode(data, len);
        TEST_ASSERT_EQUAL(static_cast<int>(((len + 2) / 3) * 4), static_cast<int>(b64.size()));
        std::vector<uint8_t> back = liveBase64Decode(b64.c_str());
        TEST_ASSERT_EQUAL(static_cast<int>(len), static_cast<int>(back.size()));
        for (size_t i = 0; i < len; ++i) TEST_ASSERT_EQUAL_UINT8(data[i], back[i]);
    }
    TEST_ASSERT_EQUAL_STRING("TWFu", liveBase64Encode(reinterpret_cast<const uint8_t*>("Man"), 3).c_str());
}

void test_setup_message() {
    LiveSetupOptions opt;
    opt.model = "gemini-3.8-live";
    opt.voice = "Orus";
    opt.system_instruction = "Você é o \"Bicho\".";
    std::string msg = buildLiveSetupMessage(opt);
    TEST_ASSERT_TRUE(msg.find("\"model\":\"models/gemini-3.8-live\"") != std::string::npos);
    TEST_ASSERT_TRUE(msg.find("\"responseModalities\":[\"AUDIO\"]") != std::string::npos);
    TEST_ASSERT_TRUE(msg.find("\"voiceName\":\"Orus\"") != std::string::npos);
    TEST_ASSERT_TRUE(msg.find("\\\"Bicho\\\"") != std::string::npos); // Aspas escapadas
    TEST_ASSERT_TRUE(msg.find("\"inputAudioTranscription\":{}") != std::string::npos);
    TEST_ASSERT_TRUE(msg.find("\"outputAudioTranscription\":{}") != std::string::npos);
}

void test_audio_message() {
    const int16_t pcm[] = {1, -1, 32767};
    std::string msg = buildLiveAudioMessage(pcm, 3);
    TEST_ASSERT_TRUE(msg.find("audio/pcm;rate=16000") != std::string::npos);
    std::string expected = liveBase64Encode(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    TEST_ASSERT_TRUE(msg.find("\"data\":\"" + expected + "\"") != std::string::npos);
}

void test_parse_server_messages() {
    LiveServerMessage m;
    TEST_ASSERT_TRUE(parseLiveServerMessage("{\"setupComplete\":{}}", m));
    TEST_ASSERT_TRUE(m.setup_complete);

    const int16_t pcm[] = {100, -200};
    std::string b64 = liveBase64Encode(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    std::string content =
        "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"" +
        b64 + "\"}},{\"inlineData\":{\"data\":\"" + b64 + "\"}}]},"
        "\"inputTranscription\":{\"text\":\"Bicho, o dólar?\"},\"outputTranscription\":{\"text\":\"R$ 5,05\"}}}";
    TEST_ASSERT_TRUE(parseLiveServerMessage(content, m));
    TEST_ASSERT_EQUAL(4, static_cast<int>(m.audio.size()));
    TEST_ASSERT_EQUAL_INT16(-200, m.audio[3]);
    TEST_ASSERT_EQUAL_STRING("Bicho, o dólar?", m.input_transcript.c_str());
    TEST_ASSERT_EQUAL_STRING("R$ 5,05", m.output_transcript.c_str());
    TEST_ASSERT_FALSE(m.turn_complete);

    TEST_ASSERT_TRUE(parseLiveServerMessage("{\"serverContent\":{\"interrupted\":true}}", m));
    TEST_ASSERT_TRUE(m.interrupted);
    TEST_ASSERT_TRUE(parseLiveServerMessage("{\"serverContent\":{\"turnComplete\":true}}", m));
    TEST_ASSERT_TRUE(m.turn_complete);
    TEST_ASSERT_TRUE(parseLiveServerMessage("{\"goAway\":{\"timeLeft\":\"10s\"}}", m));
    TEST_ASSERT_TRUE(m.go_away);
    TEST_ASSERT_TRUE(parseLiveServerMessage("{\"error\":{\"message\":\"quota\"}}", m));
    TEST_ASSERT_EQUAL_STRING("quota", m.error.c_str());
    TEST_ASSERT_FALSE(parseLiveServerMessage("nada", m));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_base64_roundtrip);
    RUN_TEST(test_setup_message);
    RUN_TEST(test_audio_message);
    RUN_TEST(test_parse_server_messages);
    return UNITY_END();
}
