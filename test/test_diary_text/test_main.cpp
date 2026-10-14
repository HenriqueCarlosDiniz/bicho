#include <unity.h>
#include <string>
#include <vector>
#include "diary_text.h"

void setUp() {}
void tearDown() {}

void test_event_line_roundtrip_flattens_and_truncates() {
    DiaryEvent ev;
    ev.time_hhmm = "14:02";
    ev.type = "VOICE";
    ev.text = "Humano disse: \"oi\"\nBicho respondeu: \"olá\"";
    std::string line = diaryEventToJsonLine(ev);
    TEST_ASSERT_TRUE(line.find('\n') == std::string::npos);

    DiaryEvent back;
    TEST_ASSERT_TRUE(diaryEventFromJsonLine(line, back));
    TEST_ASSERT_EQUAL_STRING("14:02", back.time_hhmm.c_str());
    TEST_ASSERT_EQUAL_STRING("VOICE", back.type.c_str());
    TEST_ASSERT_EQUAL_STRING("Humano disse: \"oi\" Bicho respondeu: \"olá\"", back.text.c_str());

    DiaryEvent longer;
    longer.text = std::string(400, 'a') + "ção";
    DiaryEvent longer_back;
    TEST_ASSERT_TRUE(diaryEventFromJsonLine(diaryEventToJsonLine(longer), longer_back));
    TEST_ASSERT_TRUE(longer_back.text.size() <= DIARY_MAX_EVENT_TEXT + 3);

    TEST_ASSERT_FALSE(diaryEventFromJsonLine("lixo", back));
}

void test_chapter_and_summary_lines() {
    DiaryChapter ch;
    ch.from_hhmm = "09:10";
    ch.to_hhmm = "09:55";
    ch.upto_event = 31;
    ch.text = "Focou 40 min.";
    DiaryChapter ch_back;
    TEST_ASSERT_TRUE(diaryChapterFromJsonLine(diaryChapterToJsonLine(ch), ch_back));
    TEST_ASSERT_EQUAL_UINT32(31, ch_back.upto_event);
    TEST_ASSERT_EQUAL_STRING("09:55", ch_back.to_hhmm.c_str());

    DiarySummary sum;
    sum.date = "2026-10-13";
    sum.text = "Dia produtivo.";
    DiarySummary sum_back;
    TEST_ASSERT_TRUE(diarySummaryFromJsonLine(diarySummaryToJsonLine(sum), sum_back));
    TEST_ASSERT_EQUAL_STRING("2026-10-13", sum_back.date.c_str());
    TEST_ASSERT_FALSE(diarySummaryFromJsonLine("{\"text\":\"sem data\"}", sum_back));
}

void test_prompts_include_events_and_cap_size() {
    std::vector<DiaryEvent> events;
    for (int i = 0; i < 80; ++i) {
        DiaryEvent ev;
        ev.time_hhmm = "10:00";
        ev.type = "FOCUS";
        ev.text = "evento " + std::to_string(i);
        events.push_back(ev);
    }
    std::string prompt = buildDiaryChapterPrompt("2026-10-14", events);
    TEST_ASSERT_TRUE(prompt.find("2026-10-14") != std::string::npos);
    TEST_ASSERT_TRUE(prompt.find("evento 79") != std::string::npos);
    TEST_ASSERT_TRUE(prompt.find("evento 0\n") == std::string::npos); // Mais antigos omitidos
    TEST_ASSERT_TRUE(prompt.find("20 eventos mais antigos omitidos") != std::string::npos);

    std::vector<DiaryChapter> chapters(1);
    chapters[0].from_hhmm = "09:00";
    chapters[0].to_hhmm = "10:00";
    chapters[0].text = "Manhã focada.";
    std::string day = buildDiaryDayPrompt("2026-10-14", chapters, {});
    TEST_ASSERT_TRUE(day.find("09:00-10:00: Manhã focada.") != std::string::npos);

    std::vector<DiarySummary> days(2);
    days[0].date = "2026-10-12";
    days[0].text = "A";
    days[1].date = "2026-10-13";
    days[1].text = "B";
    std::string week = buildDiaryWeekPrompt(days);
    TEST_ASSERT_TRUE(week.find("2026-10-13: B") != std::string::npos);
}

void test_parse_summary_reply_envelope_and_direct() {
    std::string summary;
    std::vector<std::string> facts;
    const std::string envelope =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
        "\"```json\\n{\\\"summary\\\": \\\"Das 9h às 10h focou.\\\", \\\"lasting_facts\\\": [\\\"Gosta de café\\\", \\\"\\\"]}\\n```\""
        "}]}}]}";
    TEST_ASSERT_TRUE(parseDiarySummaryReply(envelope, summary, facts));
    TEST_ASSERT_EQUAL_STRING("Das 9h às 10h focou.", summary.c_str());
    TEST_ASSERT_EQUAL(1, static_cast<int>(facts.size()));
    TEST_ASSERT_EQUAL_STRING("Gosta de café", facts[0].c_str());

    TEST_ASSERT_TRUE(parseDiarySummaryReply("{\"summary\":\"Direto\"}", summary, facts));
    TEST_ASSERT_EQUAL(0, static_cast<int>(facts.size()));

    TEST_ASSERT_FALSE(parseDiarySummaryReply("{\"error\":{\"message\":\"x\"}}", summary, facts));
    TEST_ASSERT_FALSE(parseDiarySummaryReply("{\"summary\":\"\"}", summary, facts));
    TEST_ASSERT_FALSE(parseDiarySummaryReply("nada", summary, facts));
}

void test_context_json() {
    TEST_ASSERT_EQUAL_STRING("", buildDiaryContextJson({}, {}, nullptr, nullptr).c_str());

    std::vector<DiaryChapter> chapters(1);
    chapters[0].from_hhmm = "09:00";
    chapters[0].to_hhmm = "10:00";
    chapters[0].text = "Focou.";
    std::vector<DiaryEvent> events(1);
    events[0].time_hhmm = "10:05";
    events[0].text = "Saiu da mesa";
    DiarySummary yesterday;
    yesterday.date = "2026-10-13";
    yesterday.text = "Dia leve.";
    DiarySummary week;
    week.text = "Semana estável.";
    std::string ctx = buildDiaryContextJson(chapters, events, &yesterday, &week);
    TEST_ASSERT_TRUE(ctx.find("\"today_so_far\":[\"09:00-10:00: Focou.\"]") != std::string::npos);
    TEST_ASSERT_TRUE(ctx.find("\"today_latest_events\":[\"10:05 Saiu da mesa\"]") != std::string::npos);
    TEST_ASSERT_TRUE(ctx.find("\"previous_day\":\"2026-10-13: Dia leve.\"") != std::string::npos);
    TEST_ASSERT_TRUE(ctx.find("\"this_week\":\"Semana estável.\"") != std::string::npos);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_event_line_roundtrip_flattens_and_truncates);
    RUN_TEST(test_chapter_and_summary_lines);
    RUN_TEST(test_prompts_include_events_and_cap_size);
    RUN_TEST(test_parse_summary_reply_envelope_and_direct);
    RUN_TEST(test_context_json);
    return UNITY_END();
}
