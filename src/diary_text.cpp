#include "diary_text.h"
#include <ArduinoJson.h>

namespace {

// Corta em fronteira de caractere UTF-8 (não deixa byte de continuação solto)
std::string truncateUtf8(const std::string& text, size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return text;
    }
    size_t cut = max_bytes;
    while (cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return text.substr(0, cut) + "…";
}

std::string singleLine(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    return out;
}

std::string serialize(const JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

} // namespace

const char* const DIARY_SYSTEM_PROMPT =
    "Você é o arquivista do Bicho, um companheiro cibernético que vive num cubo na mesa de trabalho de um humano. "
    "Recebe eventos registrados pelo Bicho (presença, foco, pausas, postura, pomodoros, commits, conversas por voz, "
    "comentários do Bicho) e escreve resumos em português do Brasil, na terceira pessoa ('o humano'), factuais e concisos. "
    "Destaque padrões úteis para o Bicho conversar depois: quanto focou, quando saiu e voltou, alertas ignorados, humor, "
    "assuntos e pedidos feitos por voz. NUNCA invente o que não está nos eventos. "
    "Responda EXCLUSIVAMENTE em JSON: {\"summary\": \"texto\", \"lasting_facts\": [\"fato\"]}. "
    "'lasting_facts' só recebe fatos duradouros sobre o humano (gostos, rotina, projetos, pessoas próximas) que "
    "apareçam claramente nos eventos; na maioria das vezes deixe a lista vazia.";

std::string diaryEventToJsonLine(const DiaryEvent& ev) {
    JsonDocument doc;
    doc["t"] = ev.time_hhmm;
    doc["type"] = ev.type;
    doc["text"] = truncateUtf8(singleLine(ev.text), DIARY_MAX_EVENT_TEXT);
    return serialize(doc);
}

bool diaryEventFromJsonLine(const std::string& line, DiaryEvent& out_ev) {
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        return false;
    }
    const char* text = doc["text"];
    if (text == nullptr) {
        return false;
    }
    out_ev.time_hhmm = doc["t"] | "";
    out_ev.type = doc["type"] | "";
    out_ev.text = text;
    return true;
}

std::string diaryChapterToJsonLine(const DiaryChapter& ch) {
    JsonDocument doc;
    doc["from"] = ch.from_hhmm;
    doc["to"] = ch.to_hhmm;
    doc["upto"] = ch.upto_event;
    doc["text"] = singleLine(ch.text);
    return serialize(doc);
}

bool diaryChapterFromJsonLine(const std::string& line, DiaryChapter& out_ch) {
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        return false;
    }
    const char* text = doc["text"];
    if (text == nullptr) {
        return false;
    }
    out_ch.from_hhmm = doc["from"] | "";
    out_ch.to_hhmm = doc["to"] | "";
    out_ch.upto_event = doc["upto"] | 0U;
    out_ch.text = text;
    return true;
}

std::string diarySummaryToJsonLine(const DiarySummary& sum) {
    JsonDocument doc;
    doc["date"] = sum.date;
    doc["text"] = singleLine(sum.text);
    return serialize(doc);
}

bool diarySummaryFromJsonLine(const std::string& line, DiarySummary& out_sum) {
    JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        return false;
    }
    const char* date = doc["date"];
    const char* text = doc["text"];
    if (date == nullptr || text == nullptr) {
        return false;
    }
    out_sum.date = date;
    out_sum.text = text;
    return true;
}

static void appendEvents(std::string& out, const std::vector<DiaryEvent>& events) {
    size_t start = (events.size() > DIARY_MAX_PROMPT_EVENTS) ? events.size() - DIARY_MAX_PROMPT_EVENTS : 0;
    if (start > 0) {
        out += "(" + std::to_string(start) + " eventos mais antigos omitidos)\n";
    }
    for (size_t i = start; i < events.size(); ++i) {
        out += events[i].time_hhmm + " [" + events[i].type + "] " + events[i].text + "\n";
    }
}

std::string buildDiaryChapterPrompt(const std::string& date, const std::vector<DiaryEvent>& events) {
    std::string prompt = "CAPÍTULO do dia " + date + ". Resuma em no máximo 3 frases (até 320 caracteres) "
                         "o que aconteceu neste trecho, com os horários aproximados.\nEventos:\n";
    appendEvents(prompt, events);
    return prompt;
}

std::string buildDiaryDayPrompt(const std::string& date,
                                const std::vector<DiaryChapter>& chapters,
                                const std::vector<DiaryEvent>& loose_events) {
    std::string prompt = "RESUMO DO DIA " + date + ". Escreva um resumo do dia inteiro em até 5 frases "
                         "(até 550 caracteres): como foi o foco, pausas, humor, conversas e qualquer padrão notável.\n";
    if (!chapters.empty()) {
        prompt += "Capítulos já resumidos:\n";
        for (const DiaryChapter& ch : chapters) {
            prompt += ch.from_hhmm + "-" + ch.to_hhmm + ": " + ch.text + "\n";
        }
    }
    if (!loose_events.empty()) {
        prompt += "Eventos ainda não resumidos:\n";
        appendEvents(prompt, loose_events);
    }
    return prompt;
}

std::string buildDiaryWeekPrompt(const std::vector<DiarySummary>& days) {
    std::string prompt = "RESUMO DA SEMANA. Com base nos resumos diários abaixo, escreva até 5 frases "
                         "(até 600 caracteres) sobre tendências: dias mais e menos produtivos, hábitos que se repetem, "
                         "assuntos recorrentes e como o humano está em geral.\nDias:\n";
    for (const DiarySummary& day : days) {
        prompt += day.date + ": " + day.text + "\n";
    }
    return prompt;
}

bool parseDiarySummaryReply(const std::string& raw_api_response,
                            std::string& out_summary,
                            std::vector<std::string>& out_lasting_facts) {
    JsonDocument envelope;
    if (deserializeJson(envelope, raw_api_response) != DeserializationError::Ok) {
        return false;
    }
    if (!envelope["error"].isNull()) {
        return false;
    }

    JsonDocument reply_doc;
    const char* part_text = envelope["candidates"][0]["content"]["parts"][0]["text"];
    if (part_text != nullptr) {
        std::string inner(part_text);
        size_t first_brace = inner.find('{');
        size_t last_brace = inner.rfind('}');
        if (first_brace == std::string::npos || last_brace == std::string::npos || last_brace < first_brace) {
            return false;
        }
        if (deserializeJson(reply_doc, inner.substr(first_brace, last_brace - first_brace + 1)) != DeserializationError::Ok) {
            return false;
        }
    } else {
        reply_doc.set(envelope.as<JsonVariantConst>());
    }

    const char* summary = reply_doc["summary"];
    if (summary == nullptr || summary[0] == '\0') {
        return false;
    }
    out_summary = singleLine(summary);
    out_lasting_facts.clear();
    for (JsonVariantConst fact : reply_doc["lasting_facts"].as<JsonArrayConst>()) {
        const char* fact_text = fact.as<const char*>();
        if (fact_text != nullptr && fact_text[0] != '\0') {
            out_lasting_facts.push_back(truncateUtf8(singleLine(fact_text), 90));
        }
    }
    return true;
}

std::string buildDiaryContextJson(const std::vector<DiaryChapter>& today_chapters,
                                  const std::vector<DiaryEvent>& recent_events,
                                  const DiarySummary* yesterday,
                                  const DiarySummary* week) {
    JsonDocument doc;
    if (!today_chapters.empty()) {
        JsonArray chapters = doc["today_so_far"].to<JsonArray>();
        size_t start = (today_chapters.size() > 6) ? today_chapters.size() - 6 : 0;
        for (size_t i = start; i < today_chapters.size(); ++i) {
            chapters.add(today_chapters[i].from_hhmm + "-" + today_chapters[i].to_hhmm + ": " + today_chapters[i].text);
        }
    }
    if (!recent_events.empty()) {
        JsonArray events = doc["today_latest_events"].to<JsonArray>();
        size_t start = (recent_events.size() > 10) ? recent_events.size() - 10 : 0;
        for (size_t i = start; i < recent_events.size(); ++i) {
            events.add(recent_events[i].time_hhmm + " " + recent_events[i].text);
        }
    }
    if (yesterday != nullptr && !yesterday->text.empty()) {
        doc["previous_day"] = yesterday->date + ": " + yesterday->text;
    }
    if (week != nullptr && !week->text.empty()) {
        doc["this_week"] = week->text;
    }
    if (doc.isNull() || doc.size() == 0) {
        return "";
    }
    return serialize(doc);
}
