#include "world_context.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(ARDUINO)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#else
#include <fstream>
#include <sstream>
#endif

// =============================================================================
// Flags Internas de Sincronização Seletiva por Intervalo (UPPER_CASE)
// =============================================================================
constexpr uint32_t SYNC_FLAG_WEATHER  = (1U << 0);
constexpr uint32_t SYNC_FLAG_FINANCE  = (1U << 1);
constexpr uint32_t SYNC_FLAG_GITHUB   = (1U << 2);
constexpr uint32_t SYNC_FLAG_HOLIDAY  = (1U << 3);
constexpr uint32_t SYNC_FLAG_TRIVIA   = (1U << 4);
constexpr uint32_t SYNC_FLAG_ALL      = 0x1FU;

constexpr const char* WORLD_HTTP_TEMP_FILE = "temp_world_http.json";

// =============================================================================
// Implementação da Classe WorldContextService
// =============================================================================

WorldContextService::WorldContextService()
    : prev_raining_state(false),
      last_weather_alert_ms(0),
      last_dollar_alert_ms(0),
      last_github_alert_ms(0),
      pending_sync_mask(0),
      is_sync_running(false),
      has_completed_sync(false)
{
    world_context = {
        23.5f,                                      // outdoor_temp_celsius
        58,                                         // humidity_percent
        false,                                      // is_raining
        true,                                       // is_day_time
        5.12f,                                      // usd_brl_rate
        0.15f,                                      // usd_variation_pct
        0.80f,                                      // btc_variation_pct
        false,                                      // is_github_down
        "All Systems Operational",                  // github_status_desc
        "Nenhum feriado hoje/amanhã",               // upcoming_holiday_name
        "Sincronizando Hacker News...",             // top_tech_headline
        "Carregando trivia geek...",                // random_geek_trivia
        0U,                                         // last_weather_sync_ms
        0U,                                         // last_finance_sync_ms
        0U,                                         // last_github_sync_ms
        0U,                                         // last_trivia_sync_ms
        0U,                                         // last_holiday_sync_ms
        false                                       // has_valid_sync
    };
    staged_context = world_context;
}

WorldContextService::~WorldContextService() {
#if !defined(ARDUINO)
    if (sync_worker_thread.joinable()) {
        sync_worker_thread.join();
    }
#endif
}

void WorldContextService::initWorldContext() {
    // Força a primeira sincronização completa alguns segundos após o boot sem travar a UI
    world_context.last_weather_sync_ms = 0U;
    world_context.last_finance_sync_ms = 0U;
    world_context.last_github_sync_ms  = 0U;
    world_context.last_trivia_sync_ms  = 0U;
    world_context.last_holiday_sync_ms = 0U;
    staged_context = world_context;
}

bool WorldContextService::executeHttpGet(const std::string& url, std::string& out_body) const {
    out_body.clear();

#if defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClientSecure secure_client;
    secure_client.setInsecure();
    HTTPClient http_client;

    if (!http_client.begin(secure_client, url.c_str())) {
        return false;
    }

    http_client.addHeader("User-Agent", "M5StackCoreS3-CyberCompanion/1.0");
    http_client.addHeader("Accept", "application/json");
    http_client.setTimeout(5000);

    int http_code = http_client.GET();
    if (http_code == HTTP_CODE_OK) {
        String response_str = http_client.getString();
        out_body = std::string(response_str.c_str());
        http_client.end();
        return !out_body.empty();
    }
    http_client.end();
    return false;
#else
    std::remove(WORLD_HTTP_TEMP_FILE);

    std::string curl_cmd =
        "curl -s -L --max-time 5 -H \"User-Agent: M5StackCoreS3-CyberCompanion/1.0\" "
        "-H \"Accept: application/json\" \"" + url + "\" -o " +
        std::string(WORLD_HTTP_TEMP_FILE) + " >nul 2>&1";

    int exit_code = std::system(curl_cmd.c_str());
    if (exit_code != 0) {
        return false;
    }

    std::ifstream resp_file(WORLD_HTTP_TEMP_FILE, std::ios::binary);
    if (!resp_file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << resp_file.rdbuf();
    out_body = ss.str();
    return !out_body.empty();
#endif
}

bool WorldContextService::parseJsonFloatAfterKey(const std::string& json_str, const char* key_str, float& out_val, size_t start_offset) const {
    std::string pattern = std::string("\"") + key_str + "\"";
    size_t key_pos = json_str.find(pattern, start_offset);
    if (key_pos == std::string::npos) {
        return false;
    }
    size_t colon_pos = json_str.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    size_t pos = colon_pos + 1;
    while (pos < json_str.size() &&
           (json_str[pos] == ' ' || json_str[pos] == '\t' ||
            json_str[pos] == '\n' || json_str[pos] == '\r' || json_str[pos] == '\"')) {
        ++pos;
    }
    if (pos >= json_str.size()) {
        return false;
    }
    char* end_ptr = nullptr;
    float val = std::strtof(json_str.c_str() + pos, &end_ptr);
    if (end_ptr == json_str.c_str() + pos) {
        return false;
    }
    out_val = val;
    return true;
}

bool WorldContextService::parseJsonIntAfterKey(const std::string& json_str, const char* key_str, int32_t& out_val, size_t start_offset) const {
    std::string pattern = std::string("\"") + key_str + "\"";
    size_t key_pos = json_str.find(pattern, start_offset);
    if (key_pos == std::string::npos) {
        return false;
    }
    size_t colon_pos = json_str.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    size_t pos = colon_pos + 1;
    while (pos < json_str.size() &&
           (json_str[pos] == ' ' || json_str[pos] == '\t' ||
            json_str[pos] == '\n' || json_str[pos] == '\r' || json_str[pos] == '\"')) {
        ++pos;
    }
    if (pos >= json_str.size()) {
        return false;
    }
    char* end_ptr = nullptr;
    long val = std::strtol(json_str.c_str() + pos, &end_ptr, 10);
    if (end_ptr == json_str.c_str() + pos) {
        return false;
    }
    out_val = static_cast<int32_t>(val);
    return true;
}

bool WorldContextService::parseJsonStringAfterKey(const std::string& json_str, const char* key_str, std::string& out_val, size_t start_offset) const {
    std::string pattern = std::string("\"") + key_str + "\"";
    size_t key_pos = json_str.find(pattern, start_offset);
    if (key_pos == std::string::npos) {
        return false;
    }
    size_t colon_pos = json_str.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    size_t quote_start = json_str.find('\"', colon_pos + 1);
    if (quote_start == std::string::npos) {
        return false;
    }

    std::string extracted;
    size_t i = quote_start + 1;
    while (i < json_str.size()) {
        char c = json_str[i];
        if (c == '\\' && (i + 1) < json_str.size()) {
            char next_c = json_str[i + 1];
            if (next_c == '\"' || next_c == '\\' || next_c == '/') {
                extracted += next_c;
                i += 2;
                continue;
            } else if (next_c == 'n' || next_c == 'r' || next_c == 't') {
                extracted += ' ';
                i += 2;
                continue;
            }
        }
        if (c == '\"') {
            out_val = extracted;
            return true;
        }
        extracted += c;
        ++i;
    }
    return false;
}

bool WorldContextService::fetchWeatherInfo(ExternalWorldContext& target_ctx) const {
    std::string body;
    if (!executeHttpGet(OPEN_METEO_API_URL, body)) {
        return false;
    }

    // Procura o bloco "current":{...} da resposta do Open-Meteo (evita pegar "current_units")
    size_t current_block_pos = body.find("\"current\":{");
    if (current_block_pos == std::string::npos) {
        current_block_pos = body.find("\"current\": {");
    }
    if (current_block_pos == std::string::npos) {
        return false;
    }

    float temp_c = target_ctx.outdoor_temp_celsius;
    int32_t humidity = target_ctx.humidity_percent;
    int32_t is_day_val = target_ctx.is_day_time ? 1 : 0;
    float precip_mm = 0.0f;
    int32_t weather_code = 0;

    if (!parseJsonFloatAfterKey(body, "temperature_2m", temp_c, current_block_pos)) {
        return false;
    }
    parseJsonIntAfterKey(body, "relative_humidity_2m", humidity, current_block_pos);
    parseJsonIntAfterKey(body, "is_day", is_day_val, current_block_pos);
    parseJsonFloatAfterKey(body, "precipitation", precip_mm, current_block_pos);
    parseJsonIntAfterKey(body, "weather_code", weather_code, current_block_pos);

    target_ctx.outdoor_temp_celsius = temp_c;
    target_ctx.humidity_percent = humidity;
    target_ctx.is_day_time = (is_day_val != 0);
    target_ctx.is_raining = (precip_mm > 0.0f) || (weather_code >= 51);
    target_ctx.has_valid_sync = true;
    return true;
}

bool WorldContextService::fetchFinanceInfo(ExternalWorldContext& target_ctx) const {
    std::string body;
    if (!executeHttpGet(AWESOME_FINANCE_API_URL, body)) {
        return false;
    }

    size_t usd_pos = body.find("\"USDBRL\"");
    size_t btc_pos = body.find("\"BTCBRL\"");

    bool updated = false;
    if (usd_pos != std::string::npos) {
        float bid_rate = target_ctx.usd_brl_rate;
        float var_pct = target_ctx.usd_variation_pct;
        if (parseJsonFloatAfterKey(body, "bid", bid_rate, usd_pos)) {
            target_ctx.usd_brl_rate = bid_rate;
            updated = true;
        }
        if (parseJsonFloatAfterKey(body, "pctChange", var_pct, usd_pos)) {
            target_ctx.usd_variation_pct = var_pct;
        }
    }

    if (btc_pos != std::string::npos) {
        float btc_var = target_ctx.btc_variation_pct;
        if (parseJsonFloatAfterKey(body, "pctChange", btc_var, btc_pos)) {
            target_ctx.btc_variation_pct = btc_var;
            updated = true;
        }
    }

    if (updated) {
        target_ctx.has_valid_sync = true;
    }
    return updated;
}

bool WorldContextService::fetchGithubStatusInfo(ExternalWorldContext& target_ctx) const {
    std::string body;
    if (!executeHttpGet(GITHUB_STATUS_API_URL, body)) {
        return false;
    }

    size_t status_pos = body.find("\"status\"");
    if (status_pos == std::string::npos) {
        return false;
    }

    std::string indicator;
    std::string description;
    if (!parseJsonStringAfterKey(body, "indicator", indicator, status_pos)) {
        return false;
    }
    parseJsonStringAfterKey(body, "description", description, status_pos);

    target_ctx.is_github_down = (indicator != "none" && !indicator.empty());
    if (!description.empty()) {
        target_ctx.github_status_desc = description;
    }
    target_ctx.has_valid_sync = true;
    return true;
}

bool WorldContextService::fetchNationalHolidaysInfo(ExternalWorldContext& target_ctx) const {
    // Obtém a data local atual e do dia seguinte no formato YYYY-MM-DD
    std::time_t now_time = std::time(nullptr);
    std::tm local_tm = {};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif

    char holiday_url[96] = {0};
    std::snprintf(holiday_url, sizeof(holiday_url),
                  "https://brasilapi.com.br/api/feriados/v1/%04d",
                  local_tm.tm_year + 1900);

    std::string body;
    if (!executeHttpGet(holiday_url, body)) {
        return false;
    }

    char today_str[16] = {0};
    std::snprintf(today_str, sizeof(today_str), "%04d-%02d-%02d",
                  local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);

    std::time_t tomorrow_time = now_time + 86400;
    std::tm tomorrow_tm = {};
#if defined(_WIN32)
    localtime_s(&tomorrow_tm, &tomorrow_time);
#else
    localtime_r(&tomorrow_time, &tomorrow_tm);
#endif

    char tomorrow_str[16] = {0};
    std::snprintf(tomorrow_str, sizeof(tomorrow_str), "%04d-%02d-%02d",
                  tomorrow_tm.tm_year + 1900, tomorrow_tm.tm_mon + 1, tomorrow_tm.tm_mday);

    // Verifica se hoje é feriado nacional
    size_t today_pos = body.find(today_str);
    if (today_pos != std::string::npos) {
        std::string holiday_name;
        if (parseJsonStringAfterKey(body, "name", holiday_name, today_pos)) {
            target_ctx.upcoming_holiday_name = "HOJE: " + holiday_name;
            return true;
        }
    }

    // Verifica se amanhã é feriado nacional
    size_t tomorrow_pos = body.find(tomorrow_str);
    if (tomorrow_pos != std::string::npos) {
        std::string holiday_name;
        if (parseJsonStringAfterKey(body, "name", holiday_name, tomorrow_pos)) {
            target_ctx.upcoming_holiday_name = "AMANHÃ: " + holiday_name;
            return true;
        }
    }

    // Caso nem hoje nem amanhã sejam feriados, localiza o próximo feriado do ano no calendário
    size_t search_pos = 0;
    while (true) {
        std::string h_date;
        size_t date_key = body.find("\"date\"", search_pos);
        if (date_key == std::string::npos) {
            break;
        }
        if (parseJsonStringAfterKey(body, "date", h_date, date_key)) {
            if (h_date >= std::string(today_str)) {
                std::string h_name;
                if (parseJsonStringAfterKey(body, "name", h_name, date_key)) {
                    target_ctx.upcoming_holiday_name = h_name + " (" + h_date + ")";
                    return true;
                }
            }
        }
        search_pos = date_key + 6;
    }

    target_ctx.upcoming_holiday_name = "Sem feriado próximo";
    return true;
}

bool WorldContextService::fetchTechAndGeekTrivia(ExternalWorldContext& target_ctx, uint32_t seed_val) const {
    bool any_ok = false;

    // 1. Hacker News Top Story: pega o primeiro ID de topstories.json e consulta item/{id}.json
    std::string hn_list;
    if (executeHttpGet(HACKER_NEWS_TOP_URL, hn_list)) {
        size_t bracket_pos = hn_list.find('[');
        if (bracket_pos != std::string::npos) {
            long top_id = std::strtol(hn_list.c_str() + bracket_pos + 1, nullptr, 10);
            if (top_id > 0) {
                char item_url[128];
                std::snprintf(item_url, sizeof(item_url), "%s%ld.json", HACKER_NEWS_ITEM_PREFIX, top_id);
                std::string item_body;
                if (executeHttpGet(item_url, item_body)) {
                    std::string hn_title;
                    if (parseJsonStringAfterKey(item_body, "title", hn_title) && !hn_title.empty()) {
                        if (hn_title.size() > 90) {
                            hn_title = hn_title.substr(0, 87) + "...";
                        }
                        target_ctx.top_tech_headline = hn_title;
                        any_ok = true;
                    }
                }
            }
        }
    }

    // 2. Curiosidade Geek Aleatória (Modo Tédio): alterna entre Scryfall (Magic) e PokéAPI (1 a 251)
    bool use_scryfall = ((seed_val & 1U) == 0U);
    bool geek_loaded = false;

    if (use_scryfall) {
        std::string card_body;
        if (executeHttpGet(SCRYFALL_RANDOM_CARD_URL, card_body)) {
            std::string card_name;
            std::string type_line;
            if (parseJsonStringAfterKey(card_body, "name", card_name) && !card_name.empty()) {
                parseJsonStringAfterKey(card_body, "type_line", type_line);
                target_ctx.random_geek_trivia = "Card MTG: " + card_name +
                    (type_line.empty() ? "" : (" (" + type_line + ")"));
                geek_loaded = true;
                any_ok = true;
            }
        }
    }

    if (!geek_loaded) {
        uint32_t poke_id = 1U + ((seed_val / 3U) % 251U);
        char poke_url[128];
        std::snprintf(poke_url, sizeof(poke_url), "%s%u", POKEAPI_POKEMON_PREFIX, static_cast<unsigned int>(poke_id));
        std::string poke_body;
        if (executeHttpGet(poke_url, poke_body)) {
            // Na PokéAPI, "species":{"name":"..."} tem o nome limpo do Pokémon
            size_t species_pos = poke_body.find("\"species\"");
            std::string poke_name;
            if (species_pos != std::string::npos &&
                parseJsonStringAfterKey(poke_body, "name", poke_name, species_pos)) {
                char trivia_buf[128];
                std::snprintf(trivia_buf, sizeof(trivia_buf), "Pokémon #%u sorteado: %s",
                              static_cast<unsigned int>(poke_id), poke_name.c_str());
                target_ctx.random_geek_trivia = trivia_buf;
                any_ok = true;
            }
        }
    }

    return any_ok;
}

void WorldContextService::runPendingSyncTasks(ExternalWorldContext& working_ctx, uint32_t mask, uint32_t seed_ms) const {
    if ((mask & SYNC_FLAG_WEATHER) != 0U) {
        fetchWeatherInfo(working_ctx);
    }
    if ((mask & SYNC_FLAG_FINANCE) != 0U) {
        fetchFinanceInfo(working_ctx);
    }
    if ((mask & SYNC_FLAG_GITHUB) != 0U) {
        fetchGithubStatusInfo(working_ctx);
    }
    if ((mask & SYNC_FLAG_HOLIDAY) != 0U) {
        fetchNationalHolidaysInfo(working_ctx);
    }
    if ((mask & SYNC_FLAG_TRIVIA) != 0U) {
        fetchTechAndGeekTrivia(working_ctx, seed_ms);
    }
}

#if defined(ARDUINO)
void WorldContextService::freeRtosWorldSyncTask(void* task_param) {
    WorldContextService* self = static_cast<WorldContextService*>(task_param);
    if (self != nullptr) {
        ExternalWorldContext local_ctx = self->staged_context;
        self->runPendingSyncTasks(local_ctx, self->pending_sync_mask, M5.millis());
        self->staged_context = local_ctx;
        self->has_completed_sync = true;
        self->is_sync_running = false;
    }
    vTaskDelete(nullptr);
}
#endif

void WorldContextService::syncExternalWorldData(uint32_t now_ms) {
    // 1. Coleta resultados se uma thread/task em background acabou de terminar
    if (has_completed_sync) {
#if defined(ARDUINO)
        world_context = staged_context;
        has_completed_sync = false;
#else
        {
            std::lock_guard<std::mutex> lock(context_mutex);
            world_context = staged_context;
            has_completed_sync = false;
        }
#endif
        M5_LOGI("[MUNDO-EXTERNO] Sincronizado | Clima: %.1fC (%d%%, Chuva=%s) | USD: R$%.2f (%+.2f%%) | BTC: %+.2f%% | GitHub: %s | HN: '%s' | Geek: '%s'",
                world_context.outdoor_temp_celsius,
                static_cast<int>(world_context.humidity_percent),
                world_context.is_raining ? "SIM" : "NAO",
                world_context.usd_brl_rate,
                world_context.usd_variation_pct,
                world_context.btc_variation_pct,
                world_context.is_github_down ? "FORA DO AR" : "OK",
                world_context.top_tech_headline.c_str(),
                world_context.random_geek_trivia.c_str());
    }

    if (is_sync_running) {
        return;
    }

    // Aguarda 2.5s após o boot antes do primeiro disparo para a janela inicializar suave a 60 FPS
    if (now_ms < 2500U) {
        return;
    }

    // 2. Avalia quais APIs atingiram o intervalo de atualização
    uint32_t mask_to_run = 0U;

    if (world_context.last_weather_sync_ms == 0U ||
        (now_ms - world_context.last_weather_sync_ms >= WEATHER_SYNC_INTERVAL_MS)) {
        mask_to_run |= SYNC_FLAG_WEATHER;
        world_context.last_weather_sync_ms = now_ms;
    }

    if (world_context.last_finance_sync_ms == 0U ||
        (now_ms - world_context.last_finance_sync_ms >= FINANCE_SYNC_INTERVAL_MS)) {
        mask_to_run |= SYNC_FLAG_FINANCE;
        world_context.last_finance_sync_ms = now_ms;
    }

    if (world_context.last_github_sync_ms == 0U ||
        (now_ms - world_context.last_github_sync_ms >= GITHUB_SYNC_INTERVAL_MS)) {
        mask_to_run |= SYNC_FLAG_GITHUB;
        world_context.last_github_sync_ms = now_ms;
    }

    if (world_context.last_holiday_sync_ms == 0U ||
        (now_ms - world_context.last_holiday_sync_ms >= HOLIDAY_SYNC_INTERVAL_MS)) {
        mask_to_run |= SYNC_FLAG_HOLIDAY;
        world_context.last_holiday_sync_ms = now_ms;
    }

    if (world_context.last_trivia_sync_ms == 0U ||
        (now_ms - world_context.last_trivia_sync_ms >= TRIVIA_SYNC_INTERVAL_MS)) {
        mask_to_run |= SYNC_FLAG_TRIVIA;
        world_context.last_trivia_sync_ms = now_ms;
    }

    if (mask_to_run == 0U) {
        return;
    }

    pending_sync_mask = mask_to_run;
    staged_context = world_context;
    is_sync_running = true;
    has_completed_sync = false;

#if defined(ARDUINO)
    BaseType_t created = xTaskCreatePinnedToCore(
        WorldContextService::freeRtosWorldSyncTask,
        "WorldSyncTask",
        8192,
        this,
        1,
        nullptr,
        0 // Core 0 dedicado para rede
    );
    if (created != pdPASS) {
        is_sync_running = false;
    }
#else
    if (sync_worker_thread.joinable()) {
        sync_worker_thread.join();
    }

    ExternalWorldContext initial_copy = world_context;
    uint32_t captured_mask = pending_sync_mask;
    uint32_t captured_seed = now_ms;

    sync_worker_thread = std::thread([this, initial_copy, captured_mask, captured_seed]() {
        ExternalWorldContext updated_copy = initial_copy;
        runPendingSyncTasks(updated_copy, captured_mask, captured_seed);
        {
            std::lock_guard<std::mutex> lock(context_mutex);
            staged_context = updated_copy;
            has_completed_sync = true;
            is_sync_running = false;
        }
    });
#endif
}

bool WorldContextService::checkWorldTriggers(uint32_t now_ms, std::string& out_trigger_reason) {
    if (!world_context.has_valid_sync) {
        return false;
    }

    // 1. Queda do GitHub: prioridade máxima para avisar a desculpa oficial de parar de codar
    if (world_context.is_github_down &&
        (last_github_alert_ms == 0U || (now_ms - last_github_alert_ms > WORLD_TRIGGER_COOLDOWN_MS))) {
        last_github_alert_ms = now_ms;
        out_trigger_reason = "GITHUB_OUTAGE";
        return true;
    }

    // 2. Alerta de Chuva (transição false -> true) ou Calor Extremo (> 30.0 °C)
    bool rain_started = (world_context.is_raining && !prev_raining_state);
    prev_raining_state = world_context.is_raining;

    bool extreme_heat = (world_context.outdoor_temp_celsius > EXTREME_HEAT_THRESHOLD_C);
    if ((rain_started || extreme_heat) &&
        (last_weather_alert_ms == 0U || (now_ms - last_weather_alert_ms > WORLD_TRIGGER_COOLDOWN_MS))) {
        last_weather_alert_ms = now_ms;
        out_trigger_reason = "WEATHER_ALERT";
        return true;
    }

    // 3. Oscilação Brusca do Dólar: abs(usd_variation_pct) >= 1.5%
    if (std::fabs(world_context.usd_variation_pct) >= DOLLAR_SPIKE_THRESHOLD_PCT &&
        (last_dollar_alert_ms == 0U || (now_ms - last_dollar_alert_ms > WORLD_TRIGGER_COOLDOWN_MS))) {
        last_dollar_alert_ms = now_ms;
        out_trigger_reason = "DOLLAR_SPIKE";
        return true;
    }

    return false;
}

std::string WorldContextService::buildExternalWorldSummary() const {
    char summary_buf[640];
    std::snprintf(
        summary_buf,
        sizeof(summary_buf),
        "Clima externo: %.1fC, umidade %d%%, %s, %s | "
        "Economia: Dolar R$ %.2f (%+.2f%% hoje), Bitcoin %+.2f%% | "
        "GitHub: %s (%s) | Feriado: %s | "
        "Top Tech HackerNews: \"%s\" | Trivia Geek: \"%s\"",
        world_context.outdoor_temp_celsius,
        static_cast<int>(world_context.humidity_percent),
        world_context.is_raining ? "CHOVENDO AGORA" : "sem chuva",
        world_context.is_day_time ? "periodo diurno" : "periodo noturno",
        world_context.usd_brl_rate,
        world_context.usd_variation_pct,
        world_context.btc_variation_pct,
        world_context.is_github_down ? "CAIU / INSTAVEL" : "operacional",
        world_context.github_status_desc.c_str(),
        world_context.upcoming_holiday_name.c_str(),
        world_context.top_tech_headline.c_str(),
        world_context.random_geek_trivia.c_str()
    );
    return std::string(summary_buf);
}

const ExternalWorldContext& WorldContextService::getWorldContext() const {
    return world_context;
}

bool WorldContextService::isSyncRunning() const {
    return is_sync_running;
}
