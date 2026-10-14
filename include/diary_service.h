#ifndef DIARY_SERVICE_H
#define DIARY_SERVICE_H

#include <stdint.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "diary_text.h"

class OracleBrain;

// =============================================================================
// Constantes do Diário do Bicho (Padrão UPPER_CASE)
// =============================================================================
constexpr uint32_t DIARY_CHAPTER_EVENT_THRESHOLD = 25U;              // Fecha capítulo com N eventos pendentes...
constexpr uint32_t DIARY_CHAPTER_INTERVAL_MS = 40U * 60U * 1000U;    // ...ou a cada 40 min de atividade
constexpr uint32_t DIARY_CHAPTER_MIN_EVENTS = 4U;                    // Mínimo de eventos para resumir por tempo
constexpr uint32_t DIARY_RETRY_BACKOFF_MS = 10U * 60U * 1000U;       // Espera após falha de rede
constexpr uint32_t DIARY_BACKFILL_DAYS = 7U;                         // Dias anteriores verificados no boot
constexpr uint32_t DIARY_TASK_STACK_BYTES = 20480U;                  // Pilha do worker no ESP32-S3 (TLS)

// =============================================================================
// Classe DiaryService: diário de eventos no SD + resumos em camadas (capítulo / dia / semana)
// Arquivos (dentro de SD_MEMORY_DIR):
//   events/AAAA-MM-DD.jsonl  -> eventos brutos do dia
//   diary/AAAA-MM-DD.jsonl   -> capítulos resumidos do dia
//   diary/days.jsonl         -> resumo de cada dia
//   diary/week.json          -> resumo móvel dos últimos 7 dias
// =============================================================================
class DiaryService {
public:
    DiaryService();
    ~DiaryService();

    // Carrega o dia atual, agenda resumos atrasados e inicia o worker (chamar uma vez, no loop principal)
    void init(const OracleBrain* brain, bool enabled, bool store_transcripts);

    // Registra um evento local (loop principal)
    void logEvent(const char* type, const std::string& text);

    // Registra uma troca por voz respeitando a configuração de transcrições
    void logConversation(const std::string& user_said, const std::string& reply_text, const std::string& tags);

    // Virada de dia, disparo de capítulos e aplicação de resumos prontos (loop principal, a cada frame)
    // Fatos duradouros extraídos dos resumos de dia são devolvidos em out_lasting_facts.
    void update(uint32_t now_ms, bool network_busy, std::vector<std::string>& out_lasting_facts);

    // Objeto JSON com "hoje até agora", "ontem" e "esta semana" para a telemetria do Oráculo
    std::string buildContextJson() const;

    bool isEnabled() const;
    bool storesTranscripts() const;

private:
    enum JobKind { JOB_CHAPTER = 0, JOB_DAY, JOB_WEEK };

    struct Job {
        JobKind kind = JOB_CHAPTER;
        std::string date;
        std::string user_prompt;    // Vazio em JOB_WEEK: montado na hora do envio, com os dias já resumidos
        uint32_t upto_event = 0;
        std::string from_hhmm;
        std::string to_hhmm;
    };

    struct JobResult {
        Job job;
        bool ok = false;
        std::string summary;
        std::vector<std::string> lasting_facts;
    };

    // Estado do dia (loop principal)
    bool enabled;
    bool store_transcripts;
    bool initialized;
    const OracleBrain* oracle_brain;
    std::string today_date;
    uint32_t today_event_count;            // Total de eventos gravados hoje
    uint32_t pending_base_index;           // Índice (no dia) do primeiro evento ainda não resumido
    std::vector<DiaryEvent> pending_events;
    std::vector<DiaryEvent> pre_clock_events; // Eventos antes do relógio (NTP) ficar válido
    std::vector<DiaryChapter> today_chapters;
    std::vector<DiarySummary> day_summaries;
    DiarySummary week_summary;
    uint32_t last_chapter_ms;
    uint32_t chapter_interval_ms;          // DIARY_CHAPTER_INTERVAL_MS (no PC: BICHO_DIARY_CHAPTER_SEC para testes)
    uint32_t retry_after_ms;
    uint32_t last_clock_check_ms;
    bool job_in_flight;
    std::deque<Job> job_queue;

    // Worker (thread de rede)
    std::thread worker_thread;
    std::mutex worker_mutex;
    std::condition_variable worker_cv;
    bool worker_stop;
    bool worker_has_job;
    Job worker_job;
    bool worker_has_result;
    JobResult worker_result;

    void workerLoop();
    void dispatchJob(const Job& job);
    void applyResult(const JobResult& result, std::vector<std::string>& out_lasting_facts);
    void loadDay(const std::string& date);
    void enqueueBackfillJobs();
    void enqueueDayJob(const std::string& date);
    void enqueueWeekJobIfStale();
    bool hasDaySummary(const std::string& date) const;
    void writeEvent(const DiaryEvent& ev);
};

#endif // DIARY_SERVICE_H
