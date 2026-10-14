#include "storage.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#if defined(ARDUINO)
#include <SD.h>
#include <SPI.h>
#elif defined(_WIN32)
#include <direct.h>
#endif

namespace {
bool storage_ready = false;

bool makeOneDir(const std::string& path) {
#if defined(_WIN32) && !defined(ARDUINO)
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

bool storageBegin() {
    if (storage_ready) {
        return true;
    }
#if defined(ARDUINO)
    // Cartão microSD do CoreS3 (CS = GPIO 4, barramento SPI compartilhado com o display)
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000, STORAGE_MOUNT_POINT, 8)) {
        return false;
    }
#endif
    storage_ready = storageMakeDirs(STORAGE_ROOT);
    return storage_ready;
}

bool storageIsReady() {
    return storage_ready;
}

std::string storagePath(const std::string& relative_path) {
    if (relative_path.empty()) {
        return STORAGE_ROOT;
    }
    return std::string(STORAGE_ROOT) + (relative_path[0] == '/' ? "" : "/") + relative_path;
}

bool storageExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

long storageFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long>(st.st_size);
}

bool storageMakeDirs(const std::string& path) {
    if (path.empty() || isDirectory(path)) {
        return true;
    }
    // Cria cada nível (pula o ponto de montagem, que não é uma pasta criável)
    std::string mount = STORAGE_MOUNT_POINT;
    for (size_t pos = 1; pos <= path.size(); ++pos) {
        if (pos == path.size() || path[pos] == '/' || path[pos] == '\\') {
            std::string partial = path.substr(0, pos);
            if (partial.empty() || partial == mount || isDirectory(partial)) {
                continue;
            }
            makeOneDir(partial);
        }
    }
    return isDirectory(path);
}

bool storageRemove(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

std::vector<std::string> storageListDir(const std::string& dir_path) {
    std::vector<std::string> names;
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        return names;
    }
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
            names.emplace_back(entry->d_name);
        }
    }
    closedir(dir);
    return names;
}

bool storageReadFile(const std::string& path, std::vector<uint8_t>& out_bytes) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    out_bytes.clear();
    long size = storageFileSize(path);
    if (size > 0) {
        out_bytes.resize(static_cast<size_t>(size));
        size_t read = std::fread(out_bytes.data(), 1, out_bytes.size(), f);
        out_bytes.resize(read);
    }
    std::fclose(f);
    return true;
}

bool storageReadText(const std::string& path, std::string& out_text) {
    std::vector<uint8_t> bytes;
    if (!storageReadFile(path, bytes)) {
        return false;
    }
    out_text.assign(bytes.begin(), bytes.end());
    return true;
}

std::vector<std::string> storageReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::string text;
    if (!storageReadText(path, text)) {
        return lines;
    }
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}

bool storageWriteFile(const std::string& path, const void* data, size_t size) {
    const std::string tmp_path = path + ".tmp";
    FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    size_t written = (size > 0) ? std::fwrite(data, 1, size, f) : 0;
    bool ok = (written == size) && (std::fflush(f) == 0);
    std::fclose(f);
    if (!ok) {
        std::remove(tmp_path.c_str());
        return false;
    }
    // FATFS e Windows não renomeiam sobre um arquivo existente
    std::remove(path.c_str());
    return std::rename(tmp_path.c_str(), path.c_str()) == 0;
}

bool storageWriteText(const std::string& path, const std::string& text) {
    return storageWriteFile(path, text.data(), text.size());
}

bool storageAppendLine(const std::string& path, const std::string& line) {
    FILE* f = std::fopen(path.c_str(), "ab");
    if (f == nullptr) {
        return false;
    }
    bool ok = std::fwrite(line.data(), 1, line.size(), f) == line.size() && std::fputc('\n', f) != EOF;
    std::fclose(f);
    return ok;
}
