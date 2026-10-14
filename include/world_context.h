#ifndef WORLD_CONTEXT_H
#define WORLD_CONTEXT_H

#include <M5Unified.h>
#include <stdint.h>
#include <string>

#if !defined(ARDUINO)
#include <thread>
#include <mutex>
#include <atomic>
#endif

// =============================================================================
// Constantes de Sincronização e Endpoints do Mundo Externo (Padrão UPPER_CASE)
// =============================================================================
constexpr uint32_t WEATHER_SYNC_INTERVAL_MS  = 1800000U; // 30 minutos
constexpr uint32_t FINANCE_SYNC_INTERVAL_MS  = 900000U;  // 15 minutos
constexpr uint32_t GITHUB_SYNC_INTERVAL_MS   = 900000U;  // 15 minutos
constexpr uint32_t TRIVIA_SYNC_INTERVAL_MS   = 3600000U; // 60 minutos
constexpr uint32_t HOLIDAY_SYNC_INTERVAL_MS  = 43200000U;// 12 horas
constexpr uint32_t WORLD_TRIGGER_COOLDOWN_MS = 180000U;  // 3 minutos de cooldown por gatilho autônomo

constexpr float EXTREME_HEAT_THRESHOLD_C     = 30.0f;    // Alerta se temperatura > 30.0 °C
constexpr float DOLLAR_SPIKE_THRESHOLD_PCT   = 1.5f;     // Alerta se abs(variação USD) >= 1.5%

constexpr const char* OPEN_METEO_API_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=-23.55&longitude=-46.63&current=temperature_2m,relative_humidity_2m,is_day,precipitation,weather_code";

constexpr const char* AWESOME_FINANCE_API_URL =
    "https://economia.awesomeapi.com.br/last/USD-BRL,BTC-BRL";

constexpr const char* GITHUB_STATUS_API_URL =
    "https://www.githubstatus.com/api/v2/status.json";

constexpr const char* BRASIL_HOLIDAYS_API_URL =
    "https://brasilapi.com.br/api/feriados/v1/2026";

constexpr const char* HACKER_NEWS_TOP_URL =
    "https://hacker-news.firebaseio.com/v0/topstories.json";

constexpr const char* HACKER_NEWS_ITEM_PREFIX =
    "https://hacker-news.firebaseio.com/v0/item/";

constexpr const char* SCRYFALL_RANDOM_CARD_URL =
    "https://api.scryfall.com/cards/random";

constexpr const char* POKEAPI_POKEMON_PREFIX =
    "https://pokeapi.co/api/v2/pokemon/";

// =============================================================================
// Estrutura de Dados do Estado do Mundo Externo (Padrão snake_case)
// =============================================================================
struct ExternalWorldContext {
    float outdoor_temp_celsius;         // Temperatura externa atual em °C
    int32_t humidity_percent;           // Umidade relativa do ar (%)
    bool is_raining;                    // true se precipitation > 0 ou weather_code >= 51
    bool is_day_time;                   // true = dia, false = noite
    float usd_brl_rate;                 // Cotação comercial atual USD -> BRL
    float usd_variation_pct;            // Variação percentual do Dólar no dia (%)
    float btc_variation_pct;            // Variação percentual do Bitcoin no dia (%)
    bool is_github_down;                // true se indicator != "none" no GitHub Status
    std::string github_status_desc;     // Descrição do status operacional do GitHub
    std::string upcoming_holiday_name;  // Nome do feriado nacional hoje/amanhã (ou próximo)
    std::string top_tech_headline;      // Manchete principal do Hacker News
    std::string random_geek_trivia;     // Curiosidade/card Magic ou Pokémon para o Modo Tédio
    uint32_t last_weather_sync_ms;      // Timestamp da última sincronização de clima
    uint32_t last_finance_sync_ms;      // Timestamp da última sincronização financeira
    uint32_t last_github_sync_ms;       // Timestamp da última sincronização do GitHub
    uint32_t last_trivia_sync_ms;       // Timestamp da última sincronização de cultura geek/HN
    uint32_t last_holiday_sync_ms;      // Timestamp da última verificação de feriados
    bool has_valid_sync;                // Indica se pelo menos uma sincronização externa concluiu
};

// =============================================================================
// Classe WorldContextService: Sincronização Não-Bloqueante de Mundo Externo
// =============================================================================
class WorldContextService {
private:
    ExternalWorldContext world_context;
    bool prev_raining_state;
    uint32_t last_weather_alert_ms;
    uint32_t last_dollar_alert_ms;
    uint32_t last_github_alert_ms;

    uint32_t pending_sync_mask;

#if defined(ARDUINO)
    volatile bool is_sync_running;
    volatile bool has_completed_sync;
    ExternalWorldContext staged_context;
    static void freeRtosWorldSyncTask(void* task_param);
#else
    std::atomic<bool> is_sync_running;
    std::atomic<bool> has_completed_sync;
    std::mutex context_mutex;
    std::thread sync_worker_thread;
    ExternalWorldContext staged_context;
#endif

    // Métodos internos de rede e extração de dados das APIs públicas (camelCase)
    bool executeHttpGet(const std::string& url, std::string& out_body) const;
    bool fetchWeatherInfo(ExternalWorldContext& target_ctx) const;
    bool fetchFinanceInfo(ExternalWorldContext& target_ctx) const;
    bool fetchGithubStatusInfo(ExternalWorldContext& target_ctx) const;
    bool fetchNationalHolidaysInfo(ExternalWorldContext& target_ctx) const;
    bool fetchTechAndGeekTrivia(ExternalWorldContext& target_ctx, uint32_t seed_val) const;

    // Helpers leves de parsing JSON (camelCase)
    bool parseJsonFloatAfterKey(const std::string& json_str, const char* key_str, float& out_val, size_t start_offset = 0) const;
    bool parseJsonIntAfterKey(const std::string& json_str, const char* key_str, int32_t& out_val, size_t start_offset = 0) const;
    bool parseJsonStringAfterKey(const std::string& json_str, const char* key_str, std::string& out_val, size_t start_offset = 0) const;
    void runPendingSyncTasks(ExternalWorldContext& working_ctx, uint32_t mask, uint32_t seed_ms) const;

public:
    WorldContextService();
    ~WorldContextService();

    // Inicializa valores padrão e agenda a primeira sincronização em background
    void initWorldContext();

    // Verifica timers no loop principal (sem bloquear os 60 FPS) e dispara sincronização assíncrona
    void syncExternalWorldData(uint32_t now_ms);

    // Avalia gatilhos autônomos do mundo real ("WEATHER_ALERT", "DOLLAR_SPIKE", "GITHUB_OUTAGE")
    bool checkWorldTriggers(uint32_t now_ms, std::string& out_trigger_reason);

    // Formata o resumo enxuto em português brasileiro para injetar no Cérebro (Gemini)
    std::string buildExternalWorldSummary() const;

    // Retorna referência constante para o estado atual do mundo externo
    const ExternalWorldContext& getWorldContext() const;

    // Indica se há sincronização HTTP em andamento em background
    bool isSyncRunning() const;
};

#endif // WORLD_CONTEXT_H
