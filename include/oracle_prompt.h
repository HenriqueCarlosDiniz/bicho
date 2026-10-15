#ifndef ORACLE_PROMPT_H
#define ORACLE_PROMPT_H

#include <stdint.h>
#include <string>
#include <vector>

// =============================================================================
// Montagem do prompt do Oráculo (funções puras, sem hardware/rede).
// Usada pelo OracleBrain (firmware/emulador) e pela ferramenta de regressão tools/oracle_eval,
// para que a avaliação teste exatamente o mesmo prompt que roda no Bicho.
// =============================================================================

extern const char* const ORACLE_SYSTEM_PROMPT;

// Blocos de contexto que um gatilho pode receber (bitmask)
enum OracleContextScope : uint32_t {
    CTX_WORLD        = 1U << 0,  // Clima, cotações, feriado, manchete, trivia
    CTX_HOST         = 1U << 1,  // Telemetria do PC, janela ativa e postura
    CTX_HABITS       = 1U << 2,  // Matriz de hábitos / RPG
    CTX_PROFILE      = 1U << 3,  // Fatos duradouros do perfil
    CTX_DIARY        = 1U << 4,  // Hoje até agora / dia anterior / semana
    CTX_MEMORIES     = 1U << 5,  // Memórias episódicas recentes + micro-RAG
    CTX_CONVERSATION = 1U << 6   // Últimas trocas por voz
};

struct OracleTriggerProfile {
    uint32_t scopes;             // Combinação de OracleContextScope
    bool use_creative_lens;      // false em conversa e debriefing (atrapalha responder ao conteúdo)
    std::string focus;           // Diretriz específica do gatilho
};

// Perfil de contexto de cada gatilho (desconhecidos recebem um perfil genérico)
OracleTriggerProfile oracleTriggerProfile(const std::string& trigger_reason, bool vad_keyword_mode);

// Lentes criativas: escolha evitando repetir a anterior (seed aleatória vinda do chamador)
size_t oracleCreativeLensCount();
const char* oracleCreativeLens(size_t index);
size_t chooseCreativeLens(uint32_t seed, int previous_index);

// Briefing: escolhe até max_items itens do resumo do mundo ("A | B | C"), preferindo os que as falas
// recentes não usaram (evita o mesmo par clima + trivia a cada briefing). seed varia a escolha.
std::vector<std::string> pickBriefingItems(const std::string& world_summary,
                                           const std::vector<std::string>& recent_replies,
                                           size_t max_items, uint32_t seed);

// Primeiras palavras de uma fala (para a lista "não comece assim")
std::string replyOpening(const std::string& reply_text, size_t word_count);

struct OraclePromptInputs {
    std::string trigger_reason;
    std::string emotion_code;    // "EMOTION_HAPPY"...
    int32_t mood_score = 0;
    int32_t energy_level = 0;
    int32_t hunger_level = 0;
    uint32_t focus_minutes = 0;
    float noise_level_db = 0.0f;
    bool is_user_present = true;
    bool is_in_meeting = false;
    bool is_charging = false;
    bool vad_keyword_mode = true;
    bool has_audio = false;

    std::string world_summary;
    std::string host_posture_summary;
    std::string habit_summary;
    std::vector<std::string> profile_facts;
    std::string diary_json;                     // Objeto JSON pronto (DiaryService)
    std::vector<std::string> relevant_memories; // Micro-RAG
    std::vector<std::string> recent_memories;
    std::vector<std::string> conversation_lines;
    std::vector<std::string> recent_replies;    // Mais antigas primeiro

    int lens_index = -1;                        // -1 = sem lente
};

// Instrução de sistema da sessão Live (conversa falada): persona + regras + contexto em JSON
std::string buildLiveSystemInstruction(const std::string& context_json);

// JSON de telemetria enxuto para o gatilho (só os blocos do perfil)
std::string buildOracleTelemetryJson(const OraclePromptInputs& inputs);

// Corpo completo da requisição generateContent (system + telemetria + imagem/áudio opcionais)
std::string buildOracleRequestPayload(const std::string& telemetry_json,
                                      const std::string& jpeg_base64,
                                      const std::string& wav_base64);

#endif // ORACLE_PROMPT_H
