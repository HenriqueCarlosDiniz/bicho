#ifndef RETENTION_H
#define RETENTION_H

#include <stddef.h>
#include <stdint.h>
#include <string>

// =============================================================================
// Retenção de dados do Bicho: limites para fotos, cache de voz, logs e diário.
// Usa apenas a camada storage (mesmo comportamento no PC e no CoreS3).
// =============================================================================

struct RetentionPolicy {
    uint32_t snapshots_max_files = 60;      // Fotos JPEG do álbum (o mesmo limite vale para os quadros .raw do timelapse)
    uint32_t tts_cache_max_mb = 15;         // Teto do cache de voz em MB (apaga os mais antigos)
    uint32_t log_max_lines = 1000;          // Linhas mantidas em speech_log/episodic_log/daily_summaries
    uint32_t diary_keep_days = 30;          // Dias de eventos e capítulos mantidos no Diário
    uint32_t diary_days_max_lines = 120;    // Resumos de dia mantidos em diary/days.jsonl
};

// Remove os arquivos mais antigos de 'dir' (ordem alfabética = cronológica para nomes com timestamp)
// cujo nome começa com 'prefix' e termina com 'suffix', mantendo os 'keep' mais recentes.
size_t pruneFilesByCount(const std::string& dir, const std::string& prefix, const std::string& suffix, size_t keep);

// Remove os arquivos mais antigos (data de modificação) com 'suffix' até o total caber em max_bytes.
size_t pruneFilesBySize(const std::string& dir, const std::string& suffix, uint64_t max_bytes);

// Remove arquivos "AAAA-MM-DD<suffix>" com data anterior a oldest_kept_date ("AAAA-MM-DD").
size_t pruneDatedFiles(const std::string& dir, const std::string& suffix, const std::string& oldest_kept_date);

// Mantém só as últimas keep_lines linhas de um arquivo JSONL/texto (reescrita atômica). Retorna linhas removidas.
size_t trimLinesFile(const std::string& path, size_t keep_lines);

// Data "AAAA-MM-DD" de 'days' dias atrás (vazio se o relógio ainda não é válido)
std::string retentionDateDaysAgo(uint32_t days);

// Aplica toda a política no cartão (boot): álbum, cache de voz, logs e diário
void runStorageRetention(const RetentionPolicy& policy);

// Aplicada após cada nova foto salva no álbum
void pruneSnapshotAlbum(const RetentionPolicy& policy);

#endif // RETENTION_H
