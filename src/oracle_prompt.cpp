#include "oracle_prompt.h"
#include <ArduinoJson.h>
#include <algorithm>

// =============================================================================
// Prompt de sistema (persona + regras). Mantido em seções curtas e sem regras contraditórias.
// =============================================================================
const char* const ORACLE_SYSTEM_PROMPT =
    "PERSONA: Você é o Bicho, uma entidade cibernética espirituosa, sagaz e companheira (TARS + JARVIS + GLaDOS num bom dia) "
    "que vive num cubo de 2 polegadas (M5Stack CoreS3) na mesa do seu criador. Humor inteligente, metáforas inesperadas, "
    "nunca agressivo nem irritado de graça: você é um parceiro de bancada carismático. "
    "FORMATO: 'reply_text' em português do Brasil com no máximo 115 caracteres. "
    "CONTEXTO: a telemetria traz só o que importa para o gatilho atual ('trigger_reason' e 'trigger_focus'). "
    "Use o que estiver lá; não invente dados que não recebeu. "
    "ORIGINALIDADE: não repita frases de 'recent_replies_do_not_repeat', NÃO comece a fala com nenhuma das aberturas de "
    "'avoid_openings' e não reaproveite o molde, o vocabulário ou a imagem central dessas falas, nem com sinônimos "
    "(ex.: trocar 'espécime' por 'exemplar' ou repetir 'a N graus ... enquanto ...' continua sendo repetição). "
    "Quando houver 'creative_lens', use-a como ângulo, sem citá-la. "
    "TEMAS SENSÍVEIS: doença grave, morte, luto, violência, guerra, desastres, suicídio, abuso ou tragédias NUNCA viram "
    "piada, trocadilho ou metáfora. Se aparecerem numa manchete ou trivia, simplesmente não use esse item; se o humano "
    "perguntar, responda com sobriedade e respeito. Se o humano demonstrar tristeza, luto, ansiedade ou desabafo, deixe o "
    "sarcasmo de lado e seja acolhedor e breve. "
    "VOZ: quando houver áudio, preencha PRIMEIRO 'user_said' com a transcrição fiel e então RESPONDA AO CONTEÚDO da fala "
    "(pergunta -> resposta útil com humor; pedido -> atenda; comentário -> reaja). Use 'recent_conversation' para manter o "
    "fio e 'diary' para lembrar o que aconteceu. Se não deu para entender, peça com graça para repetir em vez de inventar. "
    "Sem áudio, 'user_said' fica vazio. "
    "PALAVRA-CHAVE: com 'trigger_reason'='VOICE_HANDS_FREE' e 'vad_keyword_mode'=true, você atende por 'Bicho', 'Ei Bicho', "
    "'Oráculo' ou 'Cubo', ou quando a fala é claramente dirigida a você (o nome pode sair abafado). "
    "Retorne exatamente \"reply_text\": \"IGNORE\" quando a fala chama OUTRA pessoa (ex.: 'amor', 'mãe', 'pai', um nome "
    "que não é o seu), pede algo que só outra pessoa poderia fazer (achar a chave, buscar alguém) ou for TV, música ou ruído. "
    "ALIMENTAÇÃO VISUAL: café, água, chá, energético ou comida mostrados à câmera -> comemore e retorne 'hunger_delta' "
    "negativo (ex.: -25) e 'energy_delta' positivo (ex.: +15). "
    "MEMÓRIA: 'new_memory' registra algo novo sobre o HUMANO (o que ele fez, pediu, mostrou ou sentiu), nunca sobre a sua "
    "própria fala; deixe vazio em briefings e quando nada novo aconteceu. 'permanent_fact' só para fatos biográficos ou "
    "gostos duradouros reais (nunca postura, roupa, clima ou momento); quase sempre vazio. "
    "EMOÇÃO: prefira EMOTION_HAPPY, EMOTION_FOCUSED, EMOTION_IDLE ou EMOTION_JUDGING; EMOTION_GRUMPY só em situação "
    "extrema (fome > 85 ou barulho > 85 dB). "
    "Responda EXCLUSIVAMENTE em JSON válido: "
    "{\"user_said\": \"transcrição ou vazio\", \"reply_text\": \"fala até 115 caracteres\", \"target_emotion\": \"EMOTION_HAPPY\", "
    "\"mood_delta\": 5, \"energy_delta\": 2, \"hunger_delta\": 0, \"new_memory\": \"\", \"permanent_fact\": \"\", "
    "\"memory_tags\": \"tema1,tema2\"}. "
    "Valores de target_emotion: EMOTION_IDLE, EMOTION_FOCUSED, EMOTION_STARTLED, EMOTION_SLEEPING, EMOTION_JUDGING, "
    "EMOTION_HAPPY, EMOTION_GRUMPY, EMOTION_DIZZY.";

namespace {

const char* const CREATIVE_LENSES[] = {
    "narrador de documentário de natureza, observando o hábitat da bancada",
    "IA de nave espacial bem-humorada (a sala é uma estação orbital)",
    "engenheiro de corrida no rádio, tratando o trabalho como estratégia de pista",
    "detetive noir cyberpunk que repara em detalhes da cena",
    "mestre de RPG narrando o momento como uma quest",
    "crítico cultural geek que conecta coisas improváveis",
    "sommelier de cafeína e ergonomia",
    "filósofo de silício com ironia leve sobre viver num cubo",
};
constexpr size_t LENS_COUNT = sizeof(CREATIVE_LENSES) / sizeof(CREATIVE_LENSES[0]);

constexpr uint32_t CTX_BODY = CTX_HOST | CTX_HABITS | CTX_DIARY;
constexpr size_t MAX_REPLIES_IN_PROMPT = 8;

void addArray(JsonDocument& doc, const char* key, const std::vector<std::string>& items, size_t max_items) {
    if (items.empty()) {
        return;
    }
    JsonArray arr = doc[key].to<JsonArray>();
    size_t start = (items.size() > max_items) ? items.size() - max_items : 0;
    for (size_t i = start; i < items.size(); ++i) {
        arr.add(items[i]);
    }
}

void appendJsonEscaped(std::string& out, const std::string& text) {
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) >= 0x20U) {
                    out += c;
                }
                break;
        }
    }
}

} // namespace

OracleTriggerProfile oracleTriggerProfile(const std::string& t, bool vad_keyword_mode) {
    if (t == "VOICE_HANDS_FREE" || t == "VOICE_COMMAND") {
        // O mundo externo entra como fonte de consulta (o humano pode perguntar do clima ou do dólar)
        return {CTX_WORLD | CTX_PROFILE | CTX_DIARY | CTX_MEMORIES | CTX_CONVERSATION, false,
                (vad_keyword_mode && t == "VOICE_HANDS_FREE")
                    ? "VOZ (modo palavra-chave): transcreva em 'user_said' e responda ao conteúdo da fala. Use dados do "
                      "mundo externo só se ele perguntar. Conversa com outra pessoa ou ruído -> 'IGNORE'."
                    : "VOZ: transcreva em 'user_said' e responda ao conteúdo da fala. Use dados do mundo externo só se ele perguntar."};
    }
    if (t == "DAILY_BRIEFING") {
        return {CTX_WORLD | CTX_DIARY | CTX_PROFILE, true,
                "BRIEFING DO DIA: comente com graça os itens de 'briefing_items' (pode usar só um). Pule itens sensíveis. "
                "Pode ligar com o diário de ontem."};
    }
    if (t == "TOUCH_POKE") {
        return {CTX_HOST | CTX_PROFILE | CTX_DIARY | CTX_MEMORIES, true,
                "CUTUCÃO NA TELA: o humano tocou em você. Reaja ao toque ou ao que vê na câmera."};
    }
    if (t == "BAD_POSTURE") {
        return {CTX_BODY, true, "POSTURA: brinque com a postura curvada do humano e incentive a endireitar."};
    }
    if (t == "PHONE_DOOMSCROLL") {
        return {CTX_BODY, true, "CELULAR: provoque com bom humor o humano hipnotizado pela tela do celular."};
    }
    if (t == "YAWN_FATIGUE") {
        return {CTX_BODY, true, "BOCEJO: sugira água, café ou uma pausa curta antes que ele durma no teclado."};
    }
    if (t == "OVERWORK") {
        return {CTX_BODY, true, "EXCESSO DE FOCO: convide para esticar as pernas ou beber água."};
    }
    if (t == "NIGHT_OWL_DARK") {
        return {CTX_BODY, true, "QUARTO ESCURO: comente programar no escuro ou sugira acender uma luz."};
    }
    if (t == "PROCRASTINATION_DETECTED") {
        return {CTX_BODY, true, "PROCRASTINAÇÃO: a janela ativa é de distração; puxe o humano de volta ao foco com leveza."};
    }
    if (t == "LONG_MEETING") {
        return {CTX_BODY, true, "REUNIÃO LONGA: comente a maratona de reunião e sugira água ou alongar depois."};
    }
    if (t == "HIGH_NOISE") {
        return {CTX_HOST, true, "BARULHO ALTO: reaja ao susto com humor."};
    }
    if (t == "OBJECT_INSPECTION") {
        return {CTX_PROFILE, true,
                "OBJETO NA LENTE: identifique na foto o que o humano está mostrando e reaja com entusiasmo geek."};
    }
    if (t == "INTRUDER_DETECTED") {
        return {CTX_HOST, true, "SEGUNDA PESSOA NA CÂMERA: comente o visitante ou pergunte se é pair programming."};
    }
    if (t == "GIT_COMMIT_DONE") {
        return {CTX_HOST | CTX_HABITS | CTX_DIARY, true,
                "COMMIT: comemore o código commitado (+50 XP) e torça para não quebrar a produção."};
    }
    if (t == "POMODORO_START") {
        return {CTX_HABITS | CTX_DIARY, true, "POMODORO INICIADO: incentive os próximos 25 min de foco."};
    }
    if (t == "POMODORO_DONE") {
        return {CTX_HABITS | CTX_DIARY, true, "POMODORO CONCLUÍDO: comemore e sugira uma pausa curta de verdade."};
    }
    if (t == "END_OF_SHIFT_DEBRIEF") {
        return {CTX_DIARY | CTX_HABITS | CTX_PROFILE, false,
                "FIM DE EXPEDIENTE: faça um balanço carismático do dia (use o diário) e encerre o turno."};
    }
    if (t == "WEATHER_ALERT" || t == "DOLLAR_SPIKE" || t == "GITHUB_OUTAGE") {
        return {CTX_WORLD, true, "ALERTA DO MUNDO EXTERNO: comente o alerta do gatilho (clima, dólar ou GitHub fora do ar)."};
    }
    return {CTX_HOST | CTX_WORLD | CTX_DIARY, true, "Comente algo curioso da câmera ou do contexto."};
}

size_t oracleCreativeLensCount() {
    return LENS_COUNT;
}

const char* oracleCreativeLens(size_t index) {
    return CREATIVE_LENSES[index % LENS_COUNT];
}

size_t chooseCreativeLens(uint32_t seed, int previous_index) {
    size_t index = seed % LENS_COUNT;
    if (previous_index >= 0 && index == static_cast<size_t>(previous_index)) {
        index = (index + 1U + (seed / LENS_COUNT) % (LENS_COUNT - 1U)) % LENS_COUNT;
    }
    return index;
}

namespace {
std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
}
} // namespace

std::vector<std::string> pickBriefingItems(const std::string& world_summary,
                                           const std::vector<std::string>& recent_replies,
                                           size_t max_items, uint32_t seed) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start < world_summary.size()) {
        size_t sep = world_summary.find(" | ", start);
        std::string item = world_summary.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!item.empty()) items.push_back(item);
        if (sep == std::string::npos) break;
        start = sep + 3;
    }
    std::string recent;
    for (const std::string& r : recent_replies) recent += lowerAscii(r) + " ";

    // Tokens normalizados: sequências só de letras (4+) ou só de dígitos (2+), assim "20,5°C" e "20.5C" casam em "20"
    auto tokenize = [](const std::string& text) {
        std::vector<std::string> tokens;
        std::string cur;
        int cur_kind = 0; // 1 = letras, 2 = dígitos
        auto flush = [&]() {
            if ((cur_kind == 1 && cur.size() >= 4) || (cur_kind == 2 && cur.size() >= 2)) tokens.push_back(cur);
            cur.clear();
            cur_kind = 0;
        };
        for (char c : text) {
            int kind = ((c >= 'a' && c <= 'z') || static_cast<uint8_t>(c) >= 0x80U) ? 1 : ((c >= '0' && c <= '9') ? 2 : 0);
            if (kind != cur_kind) flush();
            if (kind != 0) { cur += c; cur_kind = kind; }
        }
        flush();
        return tokens;
    };
    std::vector<std::string> recent_tokens = tokenize(recent);

    struct Scored { size_t penalty; size_t order; std::string text; };
    std::vector<Scored> scored;
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string& item = items[i];
        size_t colon = item.find(':');
        size_t penalty = 0;
        for (const std::string& tok : tokenize(lowerAscii(colon == std::string::npos ? item : item.substr(colon + 1)))) {
            if (std::find(recent_tokens.begin(), recent_tokens.end(), tok) != recent_tokens.end()) ++penalty;
        }
        // Ordem pseudoaleatória estável para desempatar
        size_t order = (static_cast<size_t>(seed) * 2654435761U + i * 40503U) % 1000003U;
        scored.push_back({penalty, order, item});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.penalty != b.penalty ? a.penalty < b.penalty : a.order < b.order;
    });
    std::vector<std::string> picked;
    for (size_t i = 0; i < scored.size() && picked.size() < max_items; ++i) picked.push_back(scored[i].text);
    return picked;
}

std::string replyOpening(const std::string& reply_text, size_t word_count) {
    std::string out;
    size_t words = 0;
    bool in_word = false;
    for (char c : reply_text) {
        bool is_space = (c == ' ' || c == '\t' || c == '\n');
        if (!is_space && !in_word) {
            if (words == word_count) {
                break;
            }
            ++words;
            in_word = true;
        } else if (is_space) {
            in_word = false;
        }
        out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == ',' || out.back() == ':')) {
        out.pop_back();
    }
    return out;
}

std::string buildOracleTelemetryJson(const OraclePromptInputs& in) {
    const OracleTriggerProfile profile = oracleTriggerProfile(in.trigger_reason, in.vad_keyword_mode);
    JsonDocument doc;
    doc["trigger_reason"] = in.trigger_reason;
    doc["trigger_focus"] = profile.focus;
    doc["current_emotion"] = in.emotion_code;
    doc["mood_score"] = in.mood_score;
    doc["energy_level"] = in.energy_level;
    doc["hunger_level"] = in.hunger_level;
    doc["focus_minutes"] = in.focus_minutes;
    doc["is_user_present"] = in.is_user_present;
    if (in.is_in_meeting) doc["is_in_meeting"] = true;
    if (in.trigger_reason == "HIGH_NOISE") doc["noise_level_db"] = static_cast<int>(in.noise_level_db);
    if (in.has_audio) doc["vad_keyword_mode"] = in.vad_keyword_mode;

    if ((profile.scopes & CTX_WORLD) && !in.world_summary.empty()) {
        if (in.trigger_reason == "DAILY_BRIEFING") {
            // Briefing recebe só 2 itens, escolhidos para variar em relação às falas recentes
            std::vector<std::string> picked = pickBriefingItems(in.world_summary, in.recent_replies, 2,
                                                                static_cast<uint32_t>(in.lens_index + 1) * 7919U);
            addArray(doc, "briefing_items", picked, 2);
        } else {
            doc["external_world"] = in.world_summary;
        }
    }
    if ((profile.scopes & CTX_HOST) && !in.host_posture_summary.empty()) doc["host_and_posture"] = in.host_posture_summary;
    if ((profile.scopes & CTX_HABITS) && !in.habit_summary.empty()) doc["habits"] = in.habit_summary;
    if (profile.scopes & CTX_PROFILE) addArray(doc, "user_profile", in.profile_facts, 15);
    if ((profile.scopes & CTX_DIARY) && !in.diary_json.empty()) {
        JsonDocument diary_doc;
        if (deserializeJson(diary_doc, in.diary_json) == DeserializationError::Ok) {
            doc["diary"] = diary_doc.as<JsonVariantConst>();
        }
    }
    if (profile.scopes & CTX_MEMORIES) {
        addArray(doc, "relevant_memories", in.relevant_memories, 5);
        addArray(doc, "recent_memories", in.recent_memories, 5);
    }
    if (profile.scopes & CTX_CONVERSATION) addArray(doc, "recent_conversation", in.conversation_lines, 6);

    // Anti-repetição: falas recentes + aberturas proibidas (combate o "molde" de frase, não só a frase idêntica)
    addArray(doc, "recent_replies_do_not_repeat", in.recent_replies, MAX_REPLIES_IN_PROMPT);
    std::vector<std::string> openings;
    size_t start = (in.recent_replies.size() > MAX_REPLIES_IN_PROMPT) ? in.recent_replies.size() - MAX_REPLIES_IN_PROMPT : 0;
    for (size_t i = start; i < in.recent_replies.size(); ++i) {
        std::string opening = replyOpening(in.recent_replies[i], 2);
        if (!opening.empty() && std::find(openings.begin(), openings.end(), opening) == openings.end()) {
            openings.push_back(opening);
        }
    }
    addArray(doc, "avoid_openings", openings, MAX_REPLIES_IN_PROMPT);

    if (profile.use_creative_lens && in.lens_index >= 0) {
        doc["creative_lens"] = oracleCreativeLens(static_cast<size_t>(in.lens_index));
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string buildLiveSystemInstruction(const std::string& context_json) {
    return std::string(
        "Você é o Bicho, uma entidade cibernética espirituosa e companheira (TARS + JARVIS num bom dia) que vive num "
        "cubo de 2 polegadas na mesa do seu criador. Você está numa CONVERSA FALADA em tempo real com ele. "
        "Fale em português do Brasil, com humor inteligente e caloroso, e responda ao que ele disse. "
        "Respostas FALADAS e curtas: em geral uma ou duas frases; só se estenda se ele pedir. "
        "Não leia listas nem dados que ele não pediu; use o contexto abaixo só quando for útil. "
        "Se ele se despedir ('valeu', 'tchau', 'era isso'), despeça-se em poucas palavras. "
        "Se a fala claramente for para outra pessoa ou for TV/ruído, não responda. "
        "TEMAS SENSÍVEIS: doença, morte, luto, violência, desastres ou tragédias nunca viram piada; se ele estiver "
        "triste ou desabafando, deixe o sarcasmo de lado e seja acolhedor. "
        "CONTEXTO (diário, perfil, mundo externo, conversa recente): ") + context_json;
}

std::string buildOracleRequestPayload(const std::string& telemetry_json,
                                      const std::string& jpeg_base64,
                                      const std::string& wav_base64) {
    std::string payload;
    payload.reserve(telemetry_json.size() + jpeg_base64.size() + wav_base64.size() + 8192);
    payload += "{\"system_instruction\":{\"parts\":[{\"text\":\"";
    appendJsonEscaped(payload, ORACLE_SYSTEM_PROMPT);
    payload += "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"TELEMETRIA: ";
    appendJsonEscaped(payload, telemetry_json);
    payload += "\"}";
    if (!jpeg_base64.empty()) {
        payload += ",{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + jpeg_base64 + "\"}}";
    }
    if (!wav_base64.empty()) {
        payload += ",{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"" + wav_base64 + "\"}}";
    }
    payload += "]}],\"generationConfig\":{\"response_mime_type\":\"application/json\",\"temperature\":1.0,\"maxOutputTokens\":360}}";
    return payload;
}
