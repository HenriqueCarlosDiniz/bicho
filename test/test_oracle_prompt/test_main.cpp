#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "oracle_prompt.h"

void setUp() {}
void tearDown() {}

static OraclePromptInputs fullInputs(const char* trigger) {
    OraclePromptInputs in;
    in.trigger_reason = trigger;
    in.emotion_code = "EMOTION_HAPPY";
    in.world_summary = "Clima externo: 20.5C, umidade 91% | Economia: Dolar R$ 5.05 (+0.45% hoje) | "
                       "GitHub: operacional | Trivia Geek: \"Pokemon #85 sorteado: dodrio\"";
    in.host_posture_summary = "Janela Ativa: 'VS Code' | Postura: CURVADA";
    in.habit_summary = "Rank RPG: Lv.13";
    in.profile_facts = {"Gosta de café"};
    in.diary_json = "{\"previous_day\":\"2026-10-13: dia focado\"}";
    in.recent_memories = {"Pediu o dólar"};
    in.conversation_lines = {"[há 30s] Humano: oi | Bicho: olá"};
    in.recent_replies = {"O espécime observa a 20.5°C enquanto o Dodrio corre."};
    in.lens_index = 0;
    return in;
}

void test_posture_gets_body_context_only() {
    std::string t = buildOracleTelemetryJson(fullInputs("BAD_POSTURE"));
    TEST_ASSERT_TRUE(t.find("host_and_posture") != std::string::npos);
    TEST_ASSERT_TRUE(t.find("habits") != std::string::npos);
    TEST_ASSERT_TRUE(t.find("external_world") == std::string::npos);
    TEST_ASSERT_TRUE(t.find("recent_conversation") == std::string::npos);
    TEST_ASSERT_TRUE(t.find("creative_lens") != std::string::npos);
}

void test_voice_gets_conversation_and_no_lens() {
    OraclePromptInputs in = fullInputs("VOICE_HANDS_FREE");
    in.has_audio = true;
    std::string t = buildOracleTelemetryJson(in);
    TEST_ASSERT_TRUE(t.find("recent_conversation") != std::string::npos);
    TEST_ASSERT_TRUE(t.find("external_world") != std::string::npos); // Fonte de consulta se ele perguntar
    TEST_ASSERT_TRUE(t.find("host_and_posture") == std::string::npos);
    TEST_ASSERT_TRUE(t.find("creative_lens") == std::string::npos);
    TEST_ASSERT_TRUE(t.find("\"vad_keyword_mode\":true") != std::string::npos);
}

void test_briefing_gets_picked_items_and_avoid_openings() {
    std::string t = buildOracleTelemetryJson(fullInputs("DAILY_BRIEFING"));
    TEST_ASSERT_TRUE(t.find("briefing_items") != std::string::npos);
    TEST_ASSERT_TRUE(t.find("external_world") == std::string::npos);
    TEST_ASSERT_TRUE(t.find("\"avoid_openings\":[\"O espécime\"]") != std::string::npos);
}

void test_pick_briefing_items_avoids_recently_used() {
    std::string world = "Clima externo: 20.5C, umidade 91% | Economia: Dolar R$ 5.05 | GitHub: operacional | "
                        "Trivia Geek: \"Pokemon #85 sorteado: dodrio\"";
    std::vector<std::string> recent = {"Noite a 20,5°C enquanto o Dodrio (#85) corre."};
    for (uint32_t seed = 0; seed < 20; ++seed) {
        std::vector<std::string> picked = pickBriefingItems(world, recent, 2, seed);
        TEST_ASSERT_EQUAL(2, static_cast<int>(picked.size()));
        for (const std::string& item : picked) {
            TEST_ASSERT_TRUE(item.find("Clima") == std::string::npos);
            TEST_ASSERT_TRUE(item.find("Trivia") == std::string::npos);
        }
    }
    TEST_ASSERT_TRUE(pickBriefingItems("", recent, 2, 1).empty());
}

void test_lens_never_repeats_previous() {
    for (uint32_t seed = 0; seed < 200; ++seed) {
        for (int prev = 0; prev < static_cast<int>(oracleCreativeLensCount()); ++prev) {
            size_t lens = chooseCreativeLens(seed, prev);
            TEST_ASSERT_TRUE(lens < oracleCreativeLensCount());
            TEST_ASSERT_TRUE(static_cast<int>(lens) != prev);
        }
    }
}

void test_reply_opening() {
    TEST_ASSERT_EQUAL_STRING("O espécime", replyOpening("O espécime examina a tela.", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("Box, box!", replyOpening("Box, box! Curva acentuada.", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("Oi", replyOpening("Oi", 2).c_str());
}

void test_payload_and_system_rules() {
    std::string payload = buildOracleRequestPayload("{\"trigger_reason\":\"X\"}", "", "AAAA");
    TEST_ASSERT_TRUE(payload.find("\"mime_type\":\"audio/wav\",\"data\":\"AAAA\"") != std::string::npos);
    TEST_ASSERT_TRUE(payload.find("image/jpeg") == std::string::npos);
    TEST_ASSERT_TRUE(payload.find("TELEMETRIA: {\\\"trigger_reason\\\":\\\"X\\\"}") != std::string::npos);
    TEST_ASSERT_NOT_NULL(std::strstr(ORACLE_SYSTEM_PROMPT, "TEMAS SENSÍVEIS"));
    TEST_ASSERT_NOT_NULL(std::strstr(ORACLE_SYSTEM_PROMPT, "avoid_openings"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_posture_gets_body_context_only);
    RUN_TEST(test_voice_gets_conversation_and_no_lens);
    RUN_TEST(test_briefing_gets_picked_items_and_avoid_openings);
    RUN_TEST(test_pick_briefing_items_avoids_recently_used);
    RUN_TEST(test_lens_never_repeats_previous);
    RUN_TEST(test_reply_opening);
    RUN_TEST(test_payload_and_system_rules);
    return UNITY_END();
}
