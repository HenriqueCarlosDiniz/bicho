#include <unity.h>
#include <string>
#include "json_utils.h"

void setUp() {}
void tearDown() {}

// ----------------------------------------------------------------------------- Campos simples / JSONL
void test_json_get_fields_from_jsonl_line() {
    const std::string line = "{\"date\":\"2026-10-14\",\"fact\":\"Tomou caf\\u00e9\",\"mood\":72,\"ok\":true}";
    std::string fact;
    int32_t mood = 0;
    bool ok = false;
    TEST_ASSERT_TRUE(jsonGetString(line, "fact", fact));
    TEST_ASSERT_EQUAL_STRING("Tomou café", fact.c_str());
    TEST_ASSERT_TRUE(jsonGetInt(line, "mood", mood));
    TEST_ASSERT_EQUAL_INT32(72, mood);
    TEST_ASSERT_TRUE(jsonGetBool(line, "ok", ok));
    TEST_ASSERT_TRUE(ok);
}

void test_json_get_missing_or_invalid() {
    std::string out;
    int32_t v = 0;
    TEST_ASSERT_FALSE(jsonGetString("{\"a\":1}", "b", out));
    TEST_ASSERT_FALSE(jsonGetInt("not json", "a", v));
    TEST_ASSERT_FALSE(jsonGetString("[1,2,3]", "a", out));
}

void test_json_get_ignores_nested_key_with_same_name() {
    // O parser manual antigo achava "name" em qualquer nível; agora só o nível superior conta
    const std::string doc = "{\"inner\":{\"name\":\"errado\"},\"name\":\"certo\"}";
    std::string name;
    TEST_ASSERT_TRUE(jsonGetString(doc, "name", name));
    TEST_ASSERT_EQUAL_STRING("certo", name.c_str());
}

// ----------------------------------------------------------------------------- Gemini
void test_gemini_envelope_with_code_fence_and_string_numbers() {
    const std::string raw =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
        "\"```json\\n{\\\"reply_text\\\": \\\"Linha 1\\\\nLinha 2\\\", \\\"target_emotion\\\": \\\"EMOTION_HAPPY\\\","
        " \\\"mood_delta\\\": \\\"+5\\\", \\\"energy_delta\\\": 2, \\\"hunger_delta\\\": -25,"
        " \\\"new_memory\\\": \\\"\\\", \\\"permanent_fact\\\": \\\"Gosta de chá\\\", \\\"memory_tags\\\": \\\"cha,bebida\\\"}\\n```\""
        "}]}}]}";
    OracleReplyFields fields;
    std::string err;
    TEST_ASSERT_EQUAL(GEMINI_PARSE_OK, parseGeminiReply(raw, fields, err));
    TEST_ASSERT_EQUAL_STRING("Linha 1 Linha 2", fields.reply_text.c_str());
    TEST_ASSERT_EQUAL_STRING("EMOTION_HAPPY", fields.target_emotion.c_str());
    TEST_ASSERT_EQUAL_INT32(5, fields.mood_delta);
    TEST_ASSERT_EQUAL_INT32(2, fields.energy_delta);
    TEST_ASSERT_EQUAL_INT32(-25, fields.hunger_delta);
    TEST_ASSERT_EQUAL_STRING("Gosta de chá", fields.permanent_fact.c_str());
    TEST_ASSERT_EQUAL_STRING("cha,bebida", fields.memory_tags.c_str());
}

void test_gemini_direct_persona_json() {
    OracleReplyFields fields;
    std::string err;
    TEST_ASSERT_EQUAL(GEMINI_PARSE_OK,
                      parseGeminiReply("{\"reply_text\":\"IGNORE\",\"mood_delta\":0}", fields, err));
    TEST_ASSERT_EQUAL_STRING("IGNORE", fields.reply_text.c_str());
    TEST_ASSERT_EQUAL_STRING("", fields.user_said.c_str());

    TEST_ASSERT_EQUAL(GEMINI_PARSE_OK,
                      parseGeminiReply("{\"user_said\":\"Bicho, que horas\\nsão?\",\"reply_text\":\"Hora de café.\"}", fields, err));
    TEST_ASSERT_EQUAL_STRING("Bicho, que horas são?", fields.user_said.c_str());
}

void test_gemini_api_error_and_invalid() {
    OracleReplyFields fields;
    std::string err;
    TEST_ASSERT_EQUAL(GEMINI_PARSE_API_ERROR,
                      parseGeminiReply("{\"error\":{\"code\":403,\"message\":\"API key invalida\"}}", fields, err));
    TEST_ASSERT_EQUAL_STRING("API key invalida", err.c_str());
    TEST_ASSERT_EQUAL(GEMINI_PARSE_INVALID, parseGeminiReply("", fields, err));
    TEST_ASSERT_EQUAL(GEMINI_PARSE_INVALID, parseGeminiReply("<html>502</html>", fields, err));
    TEST_ASSERT_EQUAL(GEMINI_PARSE_INVALID,
                      parseGeminiReply("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"sem json\"}]}}]}", fields, err));
}

void test_gemini_tts_audio_extraction() {
    std::string b64;
    TEST_ASSERT_TRUE(parseGeminiTtsAudioBase64(
        "{\"candidates\":[{\"content\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/L16\",\"data\":\"AAEC\"}}]}}]}", b64));
    TEST_ASSERT_EQUAL_STRING("AAEC", b64.c_str());
    TEST_ASSERT_FALSE(parseGeminiTtsAudioBase64("{\"candidates\":[]}", b64));
}

// ----------------------------------------------------------------------------- Mundo externo
void test_open_meteo_current_block() {
    const std::string body =
        "{\"current_units\":{\"temperature_2m\":\"°C\"},"
        "\"current\":{\"temperature_2m\":31.4,\"relative_humidity_2m\":40,\"is_day\":0,\"precipitation\":0.2,\"weather_code\":61}}";
    WeatherReading w;
    TEST_ASSERT_TRUE(parseOpenMeteoCurrent(body, w));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.4f, w.temperature_c);
    TEST_ASSERT_EQUAL_INT32(40, w.humidity_percent);
    TEST_ASSERT_FALSE(w.is_day);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, w.precipitation_mm);
    TEST_ASSERT_EQUAL_INT32(61, w.weather_code);
    TEST_ASSERT_FALSE(parseOpenMeteoCurrent("{\"current_units\":{}}", w));
}

void test_awesome_finance_string_numbers() {
    const std::string body =
        "{\"USDBRL\":{\"bid\":\"5.0312\",\"pctChange\":\"-1.62\"},\"BTCBRL\":{\"bid\":\"600000\",\"pctChange\":\"2.5\"}}";
    FinanceReading f;
    TEST_ASSERT_TRUE(parseAwesomeFinance(body, f));
    TEST_ASSERT_TRUE(f.has_usd);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0312f, f.usd_brl_bid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.62f, f.usd_pct_change);
    TEST_ASSERT_TRUE(f.has_btc);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, f.btc_pct_change);
}

void test_github_status() {
    std::string indicator;
    std::string desc;
    TEST_ASSERT_TRUE(parseGithubStatus(
        "{\"page\":{\"name\":\"GitHub\"},\"status\":{\"indicator\":\"minor\",\"description\":\"Partial Outage\"}}",
        indicator, desc));
    TEST_ASSERT_EQUAL_STRING("minor", indicator.c_str());
    TEST_ASSERT_EQUAL_STRING("Partial Outage", desc.c_str());
}

void test_brasil_holidays_labels() {
    const std::string body =
        "[{\"date\":\"2026-01-01\",\"name\":\"Confraternização mundial\"},"
        "{\"date\":\"2026-10-12\",\"name\":\"Nossa Senhora Aparecida\"},"
        "{\"date\":\"2026-11-02\",\"name\":\"Finados\"},"
        "{\"date\":\"2026-11-15\",\"name\":\"Proclamação da República\"}]";
    std::string label;
    TEST_ASSERT_TRUE(parseBrasilHolidays(body, "2026-10-12", "2026-10-13", label));
    TEST_ASSERT_EQUAL_STRING("HOJE: Nossa Senhora Aparecida", label.c_str());
    TEST_ASSERT_TRUE(parseBrasilHolidays(body, "2026-11-01", "2026-11-02", label));
    TEST_ASSERT_EQUAL_STRING("AMANHÃ: Finados", label.c_str());
    TEST_ASSERT_TRUE(parseBrasilHolidays(body, "2026-10-14", "2026-10-15", label));
    TEST_ASSERT_EQUAL_STRING("Finados (2026-11-02)", label.c_str());
    TEST_ASSERT_TRUE(parseBrasilHolidays(body, "2026-12-20", "2026-12-21", label));
    TEST_ASSERT_EQUAL_STRING("Sem feriado próximo", label.c_str());
}

void test_trivia_sources() {
    long top_id = 0;
    TEST_ASSERT_TRUE(parseHackerNewsTopId("[41234567, 41234000]", top_id));
    TEST_ASSERT_EQUAL_INT32(41234567, static_cast<int32_t>(top_id));
    TEST_ASSERT_FALSE(parseHackerNewsTopId("[]", top_id));

    std::string title;
    TEST_ASSERT_TRUE(parseHackerNewsTitle("{\"by\":\"x\",\"title\":\"Show HN: Bicho\"}", title));
    TEST_ASSERT_EQUAL_STRING("Show HN: Bicho", title.c_str());

    std::string name;
    std::string type_line;
    TEST_ASSERT_TRUE(parseScryfallCard(
        "{\"object\":\"card\",\"name\":\"Lightning Bolt\",\"type_line\":\"Instant\",\"card_faces\":[{\"name\":\"x\"}]}",
        name, type_line));
    TEST_ASSERT_EQUAL_STRING("Lightning Bolt", name.c_str());
    TEST_ASSERT_EQUAL_STRING("Instant", type_line.c_str());

    std::string poke;
    TEST_ASSERT_TRUE(parsePokemonSpeciesName(
        "{\"color\":{\"name\":\"yellow\"},\"is_mythical\":false,\"name\":\"pikachu\"}", poke));
    TEST_ASSERT_EQUAL_STRING("pikachu", poke.c_str());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_json_get_fields_from_jsonl_line);
    RUN_TEST(test_json_get_missing_or_invalid);
    RUN_TEST(test_json_get_ignores_nested_key_with_same_name);
    RUN_TEST(test_gemini_envelope_with_code_fence_and_string_numbers);
    RUN_TEST(test_gemini_direct_persona_json);
    RUN_TEST(test_gemini_api_error_and_invalid);
    RUN_TEST(test_gemini_tts_audio_extraction);
    RUN_TEST(test_open_meteo_current_block);
    RUN_TEST(test_awesome_finance_string_numbers);
    RUN_TEST(test_github_status);
    RUN_TEST(test_brasil_holidays_labels);
    RUN_TEST(test_trivia_sources);
    return UNITY_END();
}
