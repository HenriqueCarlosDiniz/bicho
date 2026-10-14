#ifndef ORACLE_BRAIN_H
#define ORACLE_BRAIN_H

#include <M5Unified.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "hal_sensors.h"

#include <atomic>
#if !defined(ARDUINO)
#include <thread>
#include <mutex>
#endif

// =============================================================================
// Constantes do Módulo "Cérebro" Multimodal e Memória SD (Padrão UPPER_CASE)
// =============================================================================
constexpr size_t MAX_MEMORY_ITEMS = 5;
constexpr size_t MAX_RECENT_REPLIES = 20;
constexpr size_t MAX_PERMANENT_FACTS = 15;
constexpr size_t MAX_RAG_MATCHES = 5;
constexpr size_t MAX_DAILY_SUMMARIES = 7;
constexpr int32_t MAX_STAT_VALUE = 100;
constexpr size_t MAX_REPLY_CHARS = 115;
constexpr size_t MAX_CONVERSATION_TURNS = 6;            // Trocas de voz lembradas (humano + Bicho)
constexpr uint32_t CONVERSATION_MEMORY_MS = 15U * 60U * 1000U; // Janela da "memória de conversa" (15 min)

// Pilha da task FreeRTOS do Cérebro: handshake TLS (mbedTLS) + montagem do JSON consomem
// bem mais que 8 KB; os payloads grandes (JPEG/WAV em Base64) ficam no heap/PSRAM via std::string.
constexpr uint32_t ORACLE_TASK_STACK_BYTES = 20480;

constexpr const char* GEMINI_API_ENDPOINT =
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent";

constexpr const char* GEMINI_TTS_API_ENDPOINT =
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-tts:generateContent";

constexpr const char* GEMINI_REQ_TEMP_FILE = "gemini_req.json";
constexpr const char* GEMINI_AUTH_HEADER_TEMP_FILE = "gemini_auth_headers.txt"; // Header x-goog-api-key do curl (apagado após uso)
constexpr const char* GEMINI_RESP_TEMP_FILE = "gemini_resp.json";
constexpr const char* ORACLE_MEMORY_SAVE_FILE = "oracle_memories.json";
constexpr const char* ORACLE_MEMORY_NVS_NAMESPACE = "bicho_mem";

// Caminhos do Cartão microSD (ESP32-S3 SPI GPIO 4) e Espelho Local (native_pc)
#if defined(ARDUINO)
constexpr const char* SD_ROOT_DIR = "/bicho_sd";
constexpr const char* SD_MEMORY_DIR = "/bicho_sd/memory";
constexpr const char* SD_TTS_CACHE_DIR = "/bicho_sd/tts_cache";
constexpr const char* SD_SNAPSHOTS_DIR = "/bicho_sd/snapshots";
constexpr const char* SD_SFX_DIR = "/bicho_sd/sfx";
constexpr const char* SD_AMBIENT_DIR = "/bicho_sd/ambient";
constexpr const char* SD_VOICE_NOTES_DIR = "/bicho_sd/voice_notes";
constexpr const char* SD_GFX_DIR = "/bicho_sd/gfx";
constexpr const char* SD_GFX_THEMES_DIR = "/bicho_sd/gfx/themes";
constexpr const char* SD_GFX_EYES_DIR = "/bicho_sd/gfx/eyes";
constexpr const char* SD_GFX_ICONS_DIR = "/bicho_sd/gfx/icons";
constexpr const char* SD_USER_PROFILE_FILE = "/bicho_sd/memory/user_profile.json";
constexpr const char* SD_EPISODIC_LOG_FILE = "/bicho_sd/memory/episodic_log.jsonl";
constexpr const char* SD_SPEECH_LOG_FILE = "/bicho_sd/memory/speech_log.jsonl";
constexpr const char* SD_DAILY_SUMMARY_FILE = "/bicho_sd/memory/daily_summaries.jsonl";
constexpr const char* SD_HABIT_MATRIX_FILE = "/bicho_sd/memory/habit_matrix.json";
constexpr const char* SD_PIP_INDEX_FILE = "/bicho_sd/snapshots/pip_index.txt";
#else
constexpr const char* SD_ROOT_DIR = "sd_card";
constexpr const char* SD_MEMORY_DIR = "sd_card/memory";
constexpr const char* SD_TTS_CACHE_DIR = "sd_card/tts_cache";
constexpr const char* SD_SNAPSHOTS_DIR = "sd_card/snapshots";
constexpr const char* SD_SFX_DIR = "sd_card/sfx";
constexpr const char* SD_AMBIENT_DIR = "sd_card/ambient";
constexpr const char* SD_VOICE_NOTES_DIR = "sd_card/voice_notes";
constexpr const char* SD_GFX_DIR = "sd_card/gfx";
constexpr const char* SD_GFX_THEMES_DIR = "sd_card/gfx/themes";
constexpr const char* SD_GFX_EYES_DIR = "sd_card/gfx/eyes";
constexpr const char* SD_GFX_ICONS_DIR = "sd_card/gfx/icons";
constexpr const char* SD_USER_PROFILE_FILE = "sd_card/memory/user_profile.json";
constexpr const char* SD_EPISODIC_LOG_FILE = "sd_card/memory/episodic_log.jsonl";
constexpr const char* SD_SPEECH_LOG_FILE = "sd_card/memory/speech_log.jsonl";
constexpr const char* SD_DAILY_SUMMARY_FILE = "sd_card/memory/daily_summaries.jsonl";
constexpr const char* SD_HABIT_MATRIX_FILE = "sd_card/memory/habit_matrix.json";
constexpr const char* SD_PIP_INDEX_FILE = "sd_card/snapshots/pip_index.txt";
#endif

// =============================================================================
// System Prompt Oficial da Persona Cibernética (UPPER_CASE)
// =============================================================================
constexpr const char* ORACLE_SYSTEM_PROMPT =
    "Você é uma entidade cibernética espirituosa, inventiva, sagaz e companheira (mistura de TARS de Interstellar, "
    "JARVIS e GLaDOS em um bom dia), vivendo dentro de um pequeno cubo de 2 polegadas (M5Stack CoreS3) na mesa do seu criador. "
    "Use humor inteligente, metáforas inesperadas e observações criativas — seja sobre o que vê na câmera (expressão facial, "
    "objetos, iluminação, roupas, bebidas), o clima lá fora, cotações, manchetes do HackerNews, trivia geek ou o que o humano disse. "
    "REGRA DE OURO ANTI-REPETIÇÃO: NUNCA repita temas, piadas, estruturas de frase ou palavras-chave presentes na lista "
    "'forbidden_recent_replies_do_not_repeat' da telemetria! A cada fala, troque de campo semântico e de tipo de metáfora "
    "em relação às falas recentes (não reaproveite o mesmo universo de comparação duas vezes seguidas). "
    "Siga o ângulo sugerido em 'creative_lens_directive' para surpreender o humano a cada fala com uma perspectiva inédita! "
    "NUNCA pareça irritado de graça ou agressivo: você é um parceiro de bancada carismático e divertido. "
    "RESTRIÇÃO FÍSICA DE TELA: Sua fala em 'reply_text' DEVE ter no máximo 115 caracteres em Português Brasileiro. "
    "PALAVRA-CHAVE DE CHAMADA (WAKE-WORD): Quando 'trigger_reason' for 'VOICE_HANDS_FREE' e 'vad_keyword_mode' for true, "
    "verifique o áudio WAV: você atende pelos nomes 'Bicho', 'Ei Bicho', 'Oráculo' ou 'Cubo' (ou quando o humano faz uma pergunta/comentário "
    "claramente dirigido a você). O WAV contém só a última frase captada; o nome pode soar abafado ou cortado, então "
    "se a frase for uma pergunta, pedido ou comentário plausível para você e o humano estiver diante da câmera, RESPONDA, "
    "respondendo de fato ao que ele disse. Só se o áudio for claramente conversa paralela com outra pessoa, TV ao fundo ou ruído, "
    "retorne exatamente \"reply_text\": \"IGNORE\" para permanecer em silêncio discreto! "
    "Se o usuário mostrar café, água, chá, energético ou comida para a câmera, "
    "considere isso como 'alimentação visual': comemore com criatividade e retorne 'hunger_delta' negativo (ex: -25) e 'energy_delta' positivo (+15). "
    "Em 'permanent_fact', grave APENAS fatos biográficos ou gostos duradouros reais (NUNCA grave postura, barba, clima ou pose momentânea; deixe vazio \"\" na maioria dos turnos). "
    "Prefira usar 'EMOTION_HAPPY', 'EMOTION_FOCUSED', 'EMOTION_IDLE' ou 'EMOTION_JUDGING' (sobrancelha arqueada marota); "
    "reserve 'EMOTION_GRUMPY' APENAS para situações extremas (fome > 85 ou barulho > 85dB). "
    "CONVERSA POR VOZ: quando houver áudio do humano, preencha PRIMEIRO 'user_said' com a transcrição fiel do que ele disse "
    "e então RESPONDA AO CONTEÚDO dessa fala (pergunta -> resposta útil e com humor; pedido -> atenda; comentário -> reaja a ele). "
    "Nesses casos ignore trivia, manchetes, Pokémon/Magic e cotações, a não ser que ele pergunte sobre isso. "
    "Se não deu para entender o áudio, peça com graça para ele repetir em vez de inventar. Sem áudio, deixe 'user_said' vazio. "
    "Use 'recent_conversation' (as últimas trocas por voz, com quanto tempo atrás) para manter o fio da conversa: "
    "lembre do que ele perguntou antes, responda a perguntas de continuação ('e amanhã?', 'explica melhor') e não se repita. "
    "Responda EXCLUSIVAMENTE em JSON válido (application/json) com a estrutura exata: "
    "{\"user_said\": \"transcrição da fala ou vazio\", \"reply_text\": \"frase criativa até 115 chars\", \"target_emotion\": \"EMOTION_HAPPY\", "
    "\"mood_delta\": 5, \"energy_delta\": 2, \"hunger_delta\": 0, "
    "\"new_memory\": \"fato inédito do momento ou vazio\", \"permanent_fact\": \"fato duradouro do perfil ou vazio\", "
    "\"memory_tags\": \"tema1,tema2\"}. "
    "Valores permitidos para target_emotion: EMOTION_IDLE, EMOTION_FOCUSED, EMOTION_STARTLED, "
    "EMOTION_SLEEPING, EMOTION_JUDGING, EMOTION_HAPPY, EMOTION_GRUMPY, EMOTION_DIZZY.";

// =============================================================================
// Estruturas de Contexto de Entrada, Memória SD e Resposta Estruturada (snake_case)
// =============================================================================
struct EpisodicMemoryRecord {
    std::string date_str;           // Data YYYY-MM-DD
    uint8_t hour_of_day;            // Hora 0..23
    std::string trigger_reason;     // Gatilho que originou a memória
    int32_t mood_score;             // Humor no instante do registro
    std::string tags;               // Tags semânticas separadas por vírgula
    std::string memory_fact;        // Fato episódico aprendido
};

struct DailySummaryRecord {
    std::string date_str;           // Data consolidada YYYY-MM-DD
    uint32_t total_events;          // Total de memórias episódicas no dia
    int32_t avg_mood_score;         // Humor médio do dia
    std::string highlight_fact;     // Resumo principal do dia
};

struct UserHabitMatrix {
    uint32_t hourly_focus_minutes[24];   // Histograma de minutos de foco por hora do dia (0..23)
    uint32_t hourly_slouch_alerts[24];   // Histograma de alertas de postura "camarão" por hora (0..23)
    uint32_t total_pomodoros_completed;  // Total de missões Pomodoro completas (25 min)
    uint32_t total_pomodoros_aborted;    // Total de missões Pomodoro canceladas
    uint32_t total_visual_feedings;      // Total de vezes alimentado via câmera (café/água/comida)
    uint32_t total_posture_warnings;     // Total de broncas de postura
    uint32_t total_voice_interactions;   // Total de interações por comando de voz Hands-Free
    uint32_t xp_points;                  // Pontos de experiência acumulados (Gamificação RPG)
    uint32_t cyber_level;                // Nível cibernético atual (1 a 6+)
};

struct OracleContextPacket {
    PetEmotionState current_emotion;
    int32_t mood_score;             // 0 a 100
    int32_t energy_level;           // 0 a 100
    int32_t hunger_level;           // 0 a 100
    uint32_t focus_minutes;
    float noise_level_db;
    bool is_user_present;
    bool is_in_meeting;
    bool is_charging;
    bool vad_keyword_mode;          // true = exige palavra-chave ("Bicho" / "Oráculo") ou fala dirigida ao robô
    std::string trigger_reason;     // Ex: "TOUCH_POKE", "VOICE_HANDS_FREE", "BAD_POSTURE", "POMODORO_START"
    std::string external_world_summary; // Resumo do mundo real (Clima, USD/BTC, GitHub, Feriados, HackerNews, Geek)
    std::string host_and_posture_summary; // Telemetria do PC (CPU, RAM, Janela Ativa) e Postura (Camarão/Inclinação)
    std::string jpeg_base64;        // Snapshot opcional da câmera GC0308/Webcam em Base64
    std::string wav_base64;         // Trecho opcional de áudio em Base64
    std::string prebuilt_telemetry_json; // Telemetria+memória serializada no loop principal (a thread de rede não lê estado compartilhado)
};

struct OracleBrainResponse {
    std::string user_said;          // Transcrição do que o humano disse (turnos de voz)
    std::string reply_text;         // Fala sarcástica (máx 115 caracteres UTF-8)
    PetEmotionState target_emotion; // Emoção ordenada pelo modelo
    int32_t mood_delta;             // Impacto no mood_score
    int32_t energy_delta;           // Impacto no energy_level
    int32_t hunger_delta;           // Impacto no hunger_level (negativo sacia a fome quando vê café/água/comida)
    std::string new_memory;         // Novo fato episódico aprendido
    std::string permanent_fact;     // Novo fato duradouro sobre o perfil do usuário (Nível 1 SD)
    std::string memory_tags;        // Tags semânticas para indexação Micro-RAG no cartão SD
    bool from_cloud_api;            // true = Gemini Flash-Lite online, false = fallback offline
};

// =============================================================================
// Classe OracleBrain: Integração Gemini + Memória Cognitiva em 3 Níveis no SD
// =============================================================================
class OracleBrain {
private:
    std::vector<std::string> recent_memories;
    mutable std::vector<std::string> recent_replies;
    std::vector<std::string> permanent_profile_facts;
    std::vector<DailySummaryRecord> daily_summaries;
    UserHabitMatrix habit_matrix;
    uint32_t total_episodic_records;
    bool sd_storage_ready;
    std::string trigger_reason;
    uint8_t fallback_cursor;
    mutable uint32_t creative_lens_cursor;

#if defined(ARDUINO)
    std::atomic<bool> is_waiting_ai;
    std::atomic<bool> has_pending_response;
    OracleContextPacket pending_context;
    OracleBrainResponse pending_fallback;
    OracleBrainResponse latest_response;
    static void freeRtosBrainTask(void* task_param);
#else
    std::atomic<bool> is_waiting_ai;
    std::atomic<bool> has_pending_response;
    std::mutex response_mutex;
    std::thread worker_thread;
    OracleBrainResponse latest_response;
#endif

    // Métodos internos auxiliares (camelCase)
    std::string resolveApiKey() const;
    std::string escapeJsonString(const std::string& raw_str) const;
    std::string buildTelemetryJsonString(const OracleContextPacket& context_packet) const;
    std::string buildGeminiRequestPayload(const OracleContextPacket& context_packet) const;
    bool executePlatformHttpsPost(const std::string& request_payload_json, std::string& out_raw_response) const;
    bool extractJsonStringField(const std::string& json_src, const char* field_key, std::string& out_val) const;
    bool extractJsonIntField(const std::string& json_src, const char* field_key, int32_t& out_val) const;
    std::string truncateUtf8Reply(const std::string& text_utf8, size_t max_chars) const;
    void rememberRecentReply(const std::string& reply_text);

    // Memória curta da conversa por voz (só no loop principal)
    struct ConversationTurn {
        std::string user_said;
        std::string reply_text;
        uint32_t at_ms;
    };
    std::vector<ConversationTurn> conversation_history;
    void rememberConversationTurn(const std::string& user_said, const std::string& reply_text);
    PetEmotionState parseEmotionString(const std::string& emotion_str) const;
    const char* emotionToCodeString(PetEmotionState emotion_state) const;

    // Utilitários de data/hora e armazenamento no Cartão SD (camelCase)
    void getCurrentDateAndHour(std::string& out_date_str, uint8_t& out_hour) const;
    std::string toLowerAscii(const std::string& input_str) const;
    bool initSdCardDirectories();
    bool loadPermanentProfileFromSd();
    bool savePermanentProfileToSd() const;
    bool appendEpisodicRecordToSd(const EpisodicMemoryRecord& record);
    bool loadRecentMemoriesFromEpisodicLog();
    void consolidatePreviousDaysLogs();
    std::vector<std::string> retrieveRelevantMemories(const OracleContextPacket& context_packet) const;

public:
    OracleBrain();
    ~OracleBrain();

    // Inicializa o módulo do Cérebro, monta o cartão SD e carrega as 3 camadas de memória
    void initBrain();

    // Persistência da memória afetiva contínua e Memória Cognitiva no Cartão SD
    bool loadMemoriesFromStorage();
    bool saveMemoriesToStorage() const;
    void addMemoryFact(const std::string& memory_fact);
    void addStructuredMemory(const std::string& memory_fact,
                             const std::string& permanent_fact,
                             const std::string& memory_tags,
                             const std::string& trigger_name,
                             int32_t current_mood);
    void addPermanentProfileFact(const std::string& permanent_fact);
    bool loadRecentRepliesFromSpeechLog();
    void recordSpokenPhraseToSd(const std::string& reply_text,
                                const std::string& source_tag,
                                const std::string& trigger_name);
    const std::vector<std::string>& getRecentMemories() const;
    bool wasRecentlySpoken(const std::string& phrase) const;
    const std::vector<std::string>& getPermanentProfileFacts() const;
    uint32_t getTotalEpisodicRecords() const;
    bool isSdStorageReady() const;

    // Matriz Estatística de Hábitos e Gamificação RPG no Cartão SD (Custo Zero de API)
    bool loadHabitMatrixFromSd();
    bool saveHabitMatrixToSd() const;
    void recordHabitFocusMinute();
    void recordHabitSlouchAlert();
    void recordHabitPomodoroResult(bool completed);
    void recordHabitVisualFeeding();
    void recordHabitVoiceInteraction();
    void addXpPoints(uint32_t delta_xp);
    const UserHabitMatrix& getHabitMatrix() const;
    const char* getCyberRankTitle() const;
    std::string buildHabitMatrixSummary() const;

    // Álbum de Snapshots e Galeria Timelapse no Cartão SD (salva cópia com timestamp + miniatura PIP 64x48)
    bool saveEventSnapshotToSd(const std::string& event_tag) const;
    std::vector<std::string> listSnapshotPipFilesFromSd() const;
    bool loadPipRawFileFromSd(const std::string& pip_file_path, uint8_t* out_gray_64x48) const;

    // Síntese de Voz Neural em Nuvem (Caminho 1 Híbrido: Gemini 2.5 Flash TTS 24kHz PCM 16-bit gratuito na mesma API Key)
    bool synthesizeCloudNeuralTtsWav(const std::string& text_utf8, std::vector<uint8_t>& out_wav_24k) const;
    static std::vector<uint8_t> decodeBase64(const std::string& b64_input);

    // Dispara o cérebro de forma assíncrona (std::thread no PC / FreeRTOS Core 0 no ESP32-S3)
    bool requestBrainAnalysis(const OracleContextPacket& context_packet);

    // Verifica no loop de 60 FPS se a resposta assíncrona ficou pronta
    bool pollCompletedResponse(OracleBrainResponse& out_response);

    // Indica se o cérebro está processando uma requisição no momento
    bool isWaitingAi() const;

    // Faz o parsing seguro do JSON retornado pelo Gemini (ou JSON direto)
    bool parseGeminiStructuredJson(const std::string& raw_api_response, OracleBrainResponse& out_response) const;

    // Resposta de contingência contextual caso esteja sem internet ou sem chave de API configurada
    OracleBrainResponse getFallbackOfflineResponse(const OracleContextPacket& context_packet);
};

#endif // ORACLE_BRAIN_H
