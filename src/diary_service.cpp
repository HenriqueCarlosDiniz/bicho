#include "diary_service.h"
#include "oracle_brain.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "storage.h"
#if defined(ARDUINO)
#include <esp_pthread.h>
#endif

// =============================================================================
// Utilitários de arquivo e relógio (PC: sd_card/ espelhado | CoreS3: microSD)
// =============================================================================
namespace {

const std::string EVENTS_DIR = std::string(SD_MEMORY_DIR) + "/events";
const std::string DIARY_DIR = std::string(SD_MEMORY_DIR) + "/diary";
const std::string DAYS_FILE = DIARY_DIR + "/days.jsonl";
const std::string WEEK_FILE = DIARY_DIR + "/week.json";

std::string eventsPath(const std::string& date) { return EVENTS_DIR + "/" + date + ".jsonl"; }
std::string chaptersPath(const std::string& date) { return DIARY_DIR + "/" + date + ".jsonl"; }

void ensureDirectory(const std::string& path) { storageMakeDirs(path); }
bool fileExists(const std::string& path) { return storageExists(path); }
bool appendLine(const std::string& path, const std::string& line) { return storageAppendLine(path, line); }
bool writeText(const std::string& path, const std::string& text) { return storageWriteText(path, text); }
std::vector<std::string> readLines(const std::string& path) { return storageReadLines(path); }

// Relógio local válido? (no CoreS3 só depois do NTP; antes disso o ano é 1970)
bool localTimeAt(std::time_t t, std::tm& out) {
#if defined(_WIN32)
    if (localtime_s(&out, &t) != 0) {
        return false;
    }
#else
    if (localtime_r(&t, &out) == nullptr) {
        return false;
    }
#endif
    return (out.tm_year + 1900) >= 2024;
}

std::string formatDate(const std::tm& tm_val) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday);
    return buf;
}

std::string formatHhmm(const std::tm& tm_val) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm_val.tm_hour, tm_val.tm_min);
    return buf;
}

bool currentDateTime(std::string& out_date, std::string& out_hhmm) {
    std::tm now_tm;
    if (!localTimeAt(std::time(nullptr), now_tm)) {
        return false;
    }
    out_date = formatDate(now_tm);
    out_hhmm = formatHhmm(now_tm);
    return true;
}

std::string dateDaysAgo(uint32_t days) {
    std::tm tm_val;
    if (!localTimeAt(std::time(nullptr) - static_cast<std::time_t>(days) * 86400, tm_val)) {
        return "";
    }
    return formatDate(tm_val);
}

const char* jobKindName(int kind) {
    return kind == 0 ? "capitulo" : (kind == 1 ? "dia" : "semana");
}

} // namespace

// =============================================================================
// Ciclo de vida
// =============================================================================
DiaryService::DiaryService()
    : enabled(false),
      store_transcripts(true),
      initialized(false),
      oracle_brain(nullptr),
      today_event_count(0),
      pending_base_index(0),
      last_chapter_ms(0),
      chapter_interval_ms(DIARY_CHAPTER_INTERVAL_MS),
      retry_after_ms(0),
      last_clock_check_ms(0),
      job_in_flight(false),
      worker_stop(false),
      worker_has_job(false),
      worker_has_result(false) {
}

DiaryService::~DiaryService() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_stop = true;
    }
    worker_cv.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void DiaryService::init(const OracleBrain* brain, bool is_enabled, bool keep_transcripts) {
    oracle_brain = brain;
    enabled = is_enabled;
    store_transcripts = keep_transcripts;
    if (!enabled || brain == nullptr || !brain->isSdStorageReady()) {
        enabled = false;
        M5_LOGI("[DIARIO] Diario do Bicho desativado (config ou cartao SD indisponivel).");
        return;
    }

#if !defined(ARDUINO)
    const char* chapter_sec_env = std::getenv("BICHO_DIARY_CHAPTER_SEC");
    if (chapter_sec_env != nullptr && std::atoi(chapter_sec_env) > 0) {
        chapter_interval_ms = static_cast<uint32_t>(std::atoi(chapter_sec_env)) * 1000U;
        M5_LOGI("[DIARIO] Intervalo de capitulo de teste: %u s.", static_cast<unsigned int>(chapter_interval_ms / 1000U));
    }
#endif
    ensureDirectory(EVENTS_DIR);
    ensureDirectory(DIARY_DIR);

    for (const std::string& line : readLines(DAYS_FILE)) {
        DiarySummary sum;
        if (diarySummaryFromJsonLine(line, sum)) {
            day_summaries.push_back(sum);
        }
    }
    if (day_summaries.size() > DIARY_BACKFILL_DAYS + 1) {
        day_summaries.erase(day_summaries.begin(), day_summaries.end() - (DIARY_BACKFILL_DAYS + 1));
    }
    std::vector<std::string> week_lines = readLines(WEEK_FILE);
    if (!week_lines.empty()) {
        diarySummaryFromJsonLine(week_lines.front(), week_summary);
    }

#if defined(ARDUINO)
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = DIARY_TASK_STACK_BYTES;
    cfg.prio = 1;
    cfg.pin_to_core = 0;
    cfg.thread_name = "DiaryTask";
    esp_pthread_set_cfg(&cfg);
#endif
    worker_thread = std::thread([this]() { workerLoop(); });
#if defined(ARDUINO)
    esp_pthread_cfg_t default_cfg = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&default_cfg);
#endif

    last_chapter_ms = M5.millis();
    std::string date, hhmm;
    if (currentDateTime(date, hhmm)) {
        loadDay(date);
        enqueueBackfillJobs();
    }
    M5_LOGI("[DIARIO] Diario iniciado | hoje=%s eventos=%u capitulos=%u | dias resumidos=%u | transcricoes=%s",
            today_date.empty() ? "(relogio pendente)" : today_date.c_str(),
            static_cast<unsigned int>(today_event_count),
            static_cast<unsigned int>(today_chapters.size()),
            static_cast<unsigned int>(day_summaries.size()),
            store_transcripts ? "SIM" : "NAO");
}

void DiaryService::setPrivacyMode(bool enabled) {
    privacy_mode = enabled;
}

bool DiaryService::isPrivacyMode() const {
    return privacy_mode;
}

bool DiaryService::isEnabled() const {
    return enabled;
}

bool DiaryService::storesTranscripts() const {
    return store_transcripts;
}

// =============================================================================
// Registro de eventos
// =============================================================================
void DiaryService::loadDay(const std::string& date) {
    today_date = date;
    today_chapters.clear();
    pending_events.clear();

    for (const std::string& line : readLines(chaptersPath(date))) {
        DiaryChapter ch;
        if (diaryChapterFromJsonLine(line, ch)) {
            today_chapters.push_back(ch);
        }
    }
    pending_base_index = today_chapters.empty() ? 0U : today_chapters.back().upto_event;

    std::vector<std::string> event_lines = readLines(eventsPath(date));
    today_event_count = static_cast<uint32_t>(event_lines.size());
    for (size_t i = pending_base_index; i < event_lines.size(); ++i) {
        DiaryEvent ev;
        if (diaryEventFromJsonLine(event_lines[i], ev)) {
            pending_events.push_back(ev);
        }
    }
    if (pending_base_index > today_event_count) {
        pending_base_index = today_event_count;
    }
}

void DiaryService::writeEvent(const DiaryEvent& ev) {
    appendLine(eventsPath(today_date), diaryEventToJsonLine(ev));
    ++today_event_count;
    pending_events.push_back(ev);
}

void DiaryService::logEvent(const char* type, const std::string& text) {
    if (!enabled || text.empty()) {
        return;
    }
    DiaryEvent ev;
    ev.type = (type != nullptr) ? type : "EVENT";
    ev.text = text;

    std::string date, hhmm;
    if (!initialized || today_date.empty() || !currentDateTime(date, hhmm) || date != today_date) {
        // Relógio ainda inválido ou virada de dia pendente: guarda e grava no próximo update()
        if (pre_clock_events.size() < 40) {
            pre_clock_events.push_back(ev);
        }
        return;
    }
    ev.time_hhmm = hhmm;
    writeEvent(ev);
}

void DiaryService::logConversation(const std::string& user_said, const std::string& reply_text, const std::string& tags) {
    if (privacy_mode) {
        logEvent("VOICE", "Conversa por voz com o Bicho (modo privado)");
        return;
    }
    if (store_transcripts && !user_said.empty()) {
        logEvent("VOICE", "Humano disse: \"" + user_said + "\" | Bicho respondeu: \"" + reply_text + "\"");
    } else {
        // Sem transcrição: registra só que houve conversa e os temas
        logEvent("VOICE", tags.empty() ? std::string("Conversa por voz com o Bicho")
                                       : "Conversa por voz com o Bicho (temas: " + tags + ")");
    }
}

// =============================================================================
// Agendamento de resumos
// =============================================================================
bool DiaryService::hasDaySummary(const std::string& date) const {
    for (const DiarySummary& sum : day_summaries) {
        if (sum.date == date) {
            return true;
        }
    }
    return false;
}

void DiaryService::enqueueDayJob(const std::string& date) {
    if (date.empty() || hasDaySummary(date)) {
        return;
    }
    for (const Job& queued : job_queue) {
        if (queued.kind == JOB_DAY && queued.date == date) {
            return;
        }
    }

    std::vector<DiaryChapter> chapters;
    for (const std::string& line : readLines(chaptersPath(date))) {
        DiaryChapter ch;
        if (diaryChapterFromJsonLine(line, ch)) {
            chapters.push_back(ch);
        }
    }
    uint32_t covered = chapters.empty() ? 0U : chapters.back().upto_event;
    std::vector<std::string> event_lines = readLines(eventsPath(date));
    std::vector<DiaryEvent> loose;
    for (size_t i = covered; i < event_lines.size(); ++i) {
        DiaryEvent ev;
        if (diaryEventFromJsonLine(event_lines[i], ev)) {
            loose.push_back(ev);
        }
    }
    if (chapters.empty() && loose.size() < 2) {
        return; // Dia sem conteúdo suficiente para resumir
    }

    Job job;
    job.kind = JOB_DAY;
    job.date = date;
    job.user_prompt = buildDiaryDayPrompt(date, chapters, loose);
    job_queue.push_back(job);
}

void DiaryService::enqueueWeekJobIfStale() {
    if (week_summary.date == today_date || day_summaries.size() + job_queue.size() < 2) {
        return;
    }
    for (const Job& queued : job_queue) {
        if (queued.kind == JOB_WEEK) {
            return;
        }
    }
    Job job;
    job.kind = JOB_WEEK;
    job.date = today_date;
    job_queue.push_back(job); // Prompt montado no envio, depois dos resumos de dia pendentes
}

void DiaryService::enqueueBackfillJobs() {
    // Do mais antigo para o mais recente, para a semana ficar em ordem
    for (uint32_t days_ago = DIARY_BACKFILL_DAYS; days_ago >= 1U; --days_ago) {
        std::string date = dateDaysAgo(days_ago);
        if (!date.empty() && fileExists(eventsPath(date))) {
            enqueueDayJob(date);
        }
    }
    enqueueWeekJobIfStale();
}

void DiaryService::dispatchJob(const Job& job) {
    Job to_send = job;
    if (to_send.kind == JOB_WEEK) {
        std::vector<DiarySummary> recent_days;
        std::string oldest = dateDaysAgo(DIARY_BACKFILL_DAYS);
        for (const DiarySummary& sum : day_summaries) {
            if (sum.date >= oldest) {
                recent_days.push_back(sum);
            }
        }
        if (recent_days.size() < 2) {
            return;
        }
        to_send.user_prompt = buildDiaryWeekPrompt(recent_days);
    }
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_job = to_send;
        worker_has_job = true;
    }
    job_in_flight = true;
    worker_cv.notify_one();
    M5_LOGI("[DIARIO] Gerando resumo de %s (%s)...", jobKindName(to_send.kind), to_send.date.c_str());
}

void DiaryService::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(worker_mutex);
            worker_cv.wait(lock, [this]() { return worker_stop || worker_has_job; });
            if (worker_stop) {
                return;
            }
            job = worker_job;
            worker_has_job = false;
        }

        JobResult result;
        result.job = job;
        std::string raw;
        if (oracle_brain != nullptr && oracle_brain->requestTextCompletion(DIARY_SYSTEM_PROMPT, job.user_prompt, raw)) {
            result.ok = parseDiarySummaryReply(raw, result.summary, result.lasting_facts);
        }

        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_result = result;
        worker_has_result = true;
    }
}

void DiaryService::applyResult(const JobResult& result, std::vector<std::string>& out_lasting_facts) {
    const Job& job = result.job;
    if (!result.ok) {
        M5_LOGW("[DIARIO] Falha ao resumir %s (%s); nova tentativa em %u min.",
                jobKindName(job.kind), job.date.c_str(), static_cast<unsigned int>(DIARY_RETRY_BACKOFF_MS / 60000U));
        retry_after_ms = M5.millis() + DIARY_RETRY_BACKOFF_MS;
        if (job.kind != JOB_CHAPTER) {
            job_queue.push_back(job); // Capítulos são refeitos sozinhos pelo critério de tempo/eventos
        }
        return;
    }

    if (job.kind == JOB_CHAPTER) {
        DiaryChapter ch;
        ch.from_hhmm = job.from_hhmm;
        ch.to_hhmm = job.to_hhmm;
        ch.upto_event = job.upto_event;
        ch.text = result.summary;
        appendLine(chaptersPath(job.date), diaryChapterToJsonLine(ch));
        if (job.date == today_date) {
            today_chapters.push_back(ch);
            uint32_t covered = job.upto_event - pending_base_index;
            if (covered > pending_events.size()) {
                covered = static_cast<uint32_t>(pending_events.size());
            }
            pending_events.erase(pending_events.begin(), pending_events.begin() + covered);
            pending_base_index = job.upto_event;
        }
        M5_LOGI("[DIARIO] Capitulo %s-%s: %s", ch.from_hhmm.c_str(), ch.to_hhmm.c_str(), ch.text.c_str());
    } else if (job.kind == JOB_DAY) {
        DiarySummary sum;
        sum.date = job.date;
        sum.text = result.summary;
        appendLine(DAYS_FILE, diarySummaryToJsonLine(sum));
        day_summaries.push_back(sum);
        if (day_summaries.size() > DIARY_BACKFILL_DAYS + 1) {
            day_summaries.erase(day_summaries.begin());
        }
        out_lasting_facts.insert(out_lasting_facts.end(), result.lasting_facts.begin(), result.lasting_facts.end());
        M5_LOGI("[DIARIO] Resumo do dia %s: %s", sum.date.c_str(), sum.text.c_str());
    } else {
        week_summary.date = job.date;
        week_summary.text = result.summary;
        writeText(WEEK_FILE, diarySummaryToJsonLine(week_summary) + "\n");
        M5_LOGI("[DIARIO] Resumo da semana: %s", week_summary.text.c_str());
    }
}

void DiaryService::update(uint32_t now_ms, bool network_busy, std::vector<std::string>& out_lasting_facts) {
    if (!enabled) {
        return;
    }

    // 1. Relógio / virada de dia (a cada 5 s)
    if (now_ms - last_clock_check_ms >= 5000U || !initialized) {
        last_clock_check_ms = now_ms;
        std::string date, hhmm;
        if (currentDateTime(date, hhmm)) {
            if (today_date.empty()) {
                loadDay(date);
                enqueueBackfillJobs();
            } else if (date != today_date) {
                std::string finished_day = today_date;
                M5_LOGI("[DIARIO] Virada de dia: %s -> %s", finished_day.c_str(), date.c_str());
                loadDay(date);
                enqueueDayJob(finished_day);
                enqueueWeekJobIfStale();
                last_chapter_ms = now_ms;
            }
            initialized = true;
            if (!pre_clock_events.empty()) {
                for (DiaryEvent& ev : pre_clock_events) {
                    ev.time_hhmm = hhmm;
                    writeEvent(ev);
                }
                pre_clock_events.clear();
            }
        }
    }

    // 2. Resultado do worker
    bool has_result = false;
    JobResult result;
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        if (worker_has_result) {
            result = worker_result;
            worker_has_result = false;
            has_result = true;
        }
    }
    if (has_result) {
        job_in_flight = false;
        applyResult(result, out_lasting_facts);
        if (result.ok && result.job.kind == JOB_CHAPTER) {
            last_chapter_ms = now_ms;
        }
    }

    // 3. Próximo trabalho (um por vez, sem competir com o Oráculo pela rede/TLS)
    if (job_in_flight || network_busy || today_date.empty() || now_ms < retry_after_ms) {
        return;
    }
    if (!job_queue.empty()) {
        Job job = job_queue.front();
        job_queue.pop_front();
        dispatchJob(job);
        return;
    }

    const uint32_t pending = static_cast<uint32_t>(pending_events.size());
    const bool by_count = pending >= DIARY_CHAPTER_EVENT_THRESHOLD;
    const bool by_time = pending >= DIARY_CHAPTER_MIN_EVENTS && (now_ms - last_chapter_ms >= chapter_interval_ms);
    if (by_count || by_time) {
        Job job;
        job.kind = JOB_CHAPTER;
        job.date = today_date;
        job.upto_event = pending_base_index + pending;
        job.from_hhmm = pending_events.front().time_hhmm;
        job.to_hhmm = pending_events.back().time_hhmm;
        job.user_prompt = buildDiaryChapterPrompt(today_date, pending_events);
        dispatchJob(job);
    }
}

// =============================================================================
// Contexto para o Oráculo
// =============================================================================
std::string DiaryService::buildContextJson() const {
    if (!enabled) {
        return "";
    }
    const DiarySummary* previous = nullptr;
    for (const DiarySummary& sum : day_summaries) {
        if (sum.date != today_date && (previous == nullptr || sum.date > previous->date)) {
            previous = &sum;
        }
    }
    return buildDiaryContextJson(today_chapters, pending_events, previous,
                                 week_summary.text.empty() ? nullptr : &week_summary);
}
