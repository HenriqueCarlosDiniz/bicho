#ifndef DIARY_TEXT_H
#define DIARY_TEXT_H

#include <stdint.h>
#include <string>
#include <vector>

// =============================================================================
// Diário do Bicho — formatos de arquivo, prompts e parsing (funções puras, sem hardware/rede)
// Coberto pelos testes de unidade em test/test_json_utils.
// =============================================================================

constexpr size_t DIARY_MAX_EVENT_TEXT = 240;     // Corte do texto de um evento
constexpr size_t DIARY_MAX_PROMPT_EVENTS = 60;   // Eventos enviados por resumo de capítulo/dia

struct DiaryEvent {
    std::string time_hhmm;   // "14:02"
    std::string type;        // "VOICE", "LEAVE", "RETURN", "BAD_POSTURE", ...
    std::string text;        // Descrição curta em português
};

struct DiaryChapter {
    std::string from_hhmm;
    std::string to_hhmm;
    uint32_t upto_event = 0; // Quantidade de eventos do dia cobertos até este capítulo (índice exclusivo)
    std::string text;
};

struct DiarySummary {
    std::string date;        // Dia resumido (ou data em que o resumo semanal foi gerado)
    std::string text;
};

// Linhas JSONL dos arquivos do diário
std::string diaryEventToJsonLine(const DiaryEvent& ev);
bool diaryEventFromJsonLine(const std::string& line, DiaryEvent& out_ev);
std::string diaryChapterToJsonLine(const DiaryChapter& ch);
bool diaryChapterFromJsonLine(const std::string& line, DiaryChapter& out_ch);
std::string diarySummaryToJsonLine(const DiarySummary& sum);
bool diarySummaryFromJsonLine(const std::string& line, DiarySummary& out_sum);

// Prompts de resumo (instrução de sistema + texto do usuário)
extern const char* const DIARY_SYSTEM_PROMPT;
std::string buildDiaryChapterPrompt(const std::string& date, const std::vector<DiaryEvent>& events);
std::string buildDiaryDayPrompt(const std::string& date,
                                const std::vector<DiaryChapter>& chapters,
                                const std::vector<DiaryEvent>& loose_events);
std::string buildDiaryWeekPrompt(const std::vector<DiarySummary>& days);

// Resposta do Gemini: {"summary": "...", "lasting_facts": ["..."]} (envelope generateContent ou JSON direto)
bool parseDiarySummaryReply(const std::string& raw_api_response,
                            std::string& out_summary,
                            std::vector<std::string>& out_lasting_facts);

// Objeto JSON injetado na telemetria do Oráculo
std::string buildDiaryContextJson(const std::vector<DiaryChapter>& today_chapters,
                                  const std::vector<DiaryEvent>& recent_events,
                                  const DiarySummary* yesterday,
                                  const DiarySummary* week);

#endif // DIARY_TEXT_H
