#include "retention.h"
#include "storage.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <vector>
#if defined(ARDUINO) || __has_include(<M5Unified.h>)
#include <M5Unified.h>
#define RETENTION_LOG(...) M5_LOGI(__VA_ARGS__)
#else
#define RETENTION_LOG(...) ((void)0)
#endif

namespace {

bool hasPrefixSuffix(const std::string& name, const std::string& prefix, const std::string& suffix) {
    return name.size() >= prefix.size() + suffix.size() &&
           name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

size_t pruneFilesByCount(const std::string& dir, const std::string& prefix, const std::string& suffix, size_t keep) {
    std::vector<std::string> names;
    for (const std::string& name : storageListDir(dir)) {
        if (hasPrefixSuffix(name, prefix, suffix)) {
            names.push_back(name);
        }
    }
    if (names.size() <= keep) {
        return 0;
    }
    std::sort(names.begin(), names.end());
    size_t removed = 0;
    for (size_t i = 0; i < names.size() - keep; ++i) {
        if (storageRemove(dir + "/" + names[i])) {
            ++removed;
        }
    }
    return removed;
}

size_t pruneFilesBySize(const std::string& dir, const std::string& suffix, uint64_t max_bytes) {
    struct Entry {
        std::string path;
        uint64_t size;
        std::time_t mtime;
    };
    std::vector<Entry> entries;
    uint64_t total = 0;
    for (const std::string& name : storageListDir(dir)) {
        if (!hasPrefixSuffix(name, "", suffix)) {
            continue;
        }
        std::string path = dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }
        entries.push_back({path, static_cast<uint64_t>(st.st_size), st.st_mtime});
        total += static_cast<uint64_t>(st.st_size);
    }
    if (total <= max_bytes) {
        return 0;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.mtime != b.mtime ? a.mtime < b.mtime : a.path < b.path;
    });
    size_t removed = 0;
    for (const Entry& e : entries) {
        if (total <= max_bytes) {
            break;
        }
        if (storageRemove(e.path)) {
            total -= e.size;
            ++removed;
        }
    }
    return removed;
}

size_t pruneDatedFiles(const std::string& dir, const std::string& suffix, const std::string& oldest_kept_date) {
    if (oldest_kept_date.size() != 10) {
        return 0; // Sem relógio válido não dá para julgar idade
    }
    size_t removed = 0;
    for (const std::string& name : storageListDir(dir)) {
        // Apenas nomes no formato AAAA-MM-DD<suffix>
        if (name.size() != 10 + suffix.size() || !hasPrefixSuffix(name, "", suffix) ||
            name[4] != '-' || name[7] != '-') {
            continue;
        }
        if (name.compare(0, 10, oldest_kept_date) < 0 && storageRemove(dir + "/" + name)) {
            ++removed;
        }
    }
    return removed;
}

size_t trimLinesFile(const std::string& path, size_t keep_lines) {
    std::vector<std::string> lines = storageReadLines(path);
    if (lines.size() <= keep_lines) {
        return 0;
    }
    size_t drop = lines.size() - keep_lines;
    std::string text;
    for (size_t i = drop; i < lines.size(); ++i) {
        text += lines[i];
        text += '\n';
    }
    return storageWriteText(path, text) ? drop : 0;
}

std::string retentionDateDaysAgo(uint32_t days) {
    std::time_t t = std::time(nullptr) - static_cast<std::time_t>(days) * 86400;
    std::tm tm_val = {};
#if defined(_WIN32) && !defined(ARDUINO)
    if (localtime_s(&tm_val, &t) != 0) return "";
#else
    if (localtime_r(&t, &tm_val) == nullptr) return "";
#endif
    if (tm_val.tm_year + 1900 < 2024) {
        return "";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday);
    return buf;
}

void pruneSnapshotAlbum(const RetentionPolicy& policy) {
    pruneFilesByCount(SD_SNAPSHOTS_DIR, "snap_", ".jpg", policy.snapshots_max_files);
    size_t removed_raw = pruneFilesByCount(SD_SNAPSHOTS_DIR, "pip_", ".raw", policy.snapshots_max_files);
    if (removed_raw > 0) {
        // O índice do timelapse só deve listar quadros que ainda existem
        std::vector<std::string> kept;
        for (const std::string& line : storageReadLines(SD_PIP_INDEX_FILE)) {
            if (storageExists(line)) {
                kept.push_back(line);
            }
        }
        std::string text;
        for (const std::string& line : kept) {
            text += line + "\n";
        }
        storageWriteText(SD_PIP_INDEX_FILE, text);
    }
}

void runStorageRetention(const RetentionPolicy& policy) {
    if (!storageIsReady()) {
        return;
    }
    size_t photos_before = storageListDir(SD_SNAPSHOTS_DIR).size();
    pruneSnapshotAlbum(policy);
    size_t photos_removed = photos_before - std::min(photos_before, storageListDir(SD_SNAPSHOTS_DIR).size());

    size_t tts_removed = pruneFilesBySize(SD_TTS_CACHE_DIR, ".wav",
                                          static_cast<uint64_t>(policy.tts_cache_max_mb) * 1024ULL * 1024ULL);

    size_t lines_removed = trimLinesFile(SD_SPEECH_LOG_FILE, policy.log_max_lines) +
                           trimLinesFile(SD_EPISODIC_LOG_FILE, policy.log_max_lines) +
                           trimLinesFile(SD_DAILY_SUMMARY_FILE, policy.log_max_lines);

    const std::string diary_cutoff = retentionDateDaysAgo(policy.diary_keep_days);
    const std::string memory_dir = SD_MEMORY_DIR;
    size_t diary_removed = pruneDatedFiles(memory_dir + "/events", ".jsonl", diary_cutoff) +
                           pruneDatedFiles(memory_dir + "/diary", ".jsonl", diary_cutoff);
    lines_removed += trimLinesFile(memory_dir + "/diary/days.jsonl", policy.diary_days_max_lines);

    RETENTION_LOG("[RETENCAO] Limpeza concluida | arquivos do album removidos=%u | cache de voz removidos=%u | "
                  "linhas de log removidas=%u | arquivos do diario removidos=%u",
                  static_cast<unsigned int>(photos_removed), static_cast<unsigned int>(tts_removed),
                  static_cast<unsigned int>(lines_removed), static_cast<unsigned int>(diary_removed));
}
