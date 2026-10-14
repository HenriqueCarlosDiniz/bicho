#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// =============================================================================
// Camada única de armazenamento do Bicho (cartão microSD no CoreS3 / pasta sd_card/ no PC)
//
// No ESP32-S3 o SD é montado no VFS do ESP-IDF em STORAGE_MOUNT_POINT, então stdio/POSIX
// (fopen, stat, mkdir, opendir) funcionam igual ao PC. Todo acesso a arquivo do projeto passa
// por aqui com caminhos montados por storagePath(), e não há mais SD.open() ou "sd_card/..." solto.
// =============================================================================
#if defined(ARDUINO)
#define BICHO_STORAGE_ROOT "/sd/bicho_sd"                    // Raiz dos dados do Bicho no cartão (VFS)
constexpr const char* STORAGE_MOUNT_POINT = "/sd";          // Ponto de montagem do VFS (SD.begin)
#else
#define BICHO_STORAGE_ROOT "sd_card"                         // Espelho local do cartão no emulador
constexpr const char* STORAGE_MOUNT_POINT = "";
#endif
constexpr const char* STORAGE_ROOT = BICHO_STORAGE_ROOT;

// Pastas e arquivos do cartão (mesmos nomes nos dois alvos)
constexpr const char* SD_ROOT_DIR = BICHO_STORAGE_ROOT;
constexpr const char* SD_CONFIG_FILE_PATH = BICHO_STORAGE_ROOT "/config.json";
constexpr const char* SD_MEMORY_DIR = BICHO_STORAGE_ROOT "/memory";
constexpr const char* SD_TTS_CACHE_DIR = BICHO_STORAGE_ROOT "/tts_cache";
constexpr const char* SD_SNAPSHOTS_DIR = BICHO_STORAGE_ROOT "/snapshots";
constexpr const char* SD_SFX_DIR = BICHO_STORAGE_ROOT "/sfx";
constexpr const char* SD_AMBIENT_DIR = BICHO_STORAGE_ROOT "/ambient";
constexpr const char* SD_VOICE_NOTES_DIR = BICHO_STORAGE_ROOT "/voice_notes";
constexpr const char* SD_GFX_DIR = BICHO_STORAGE_ROOT "/gfx";
constexpr const char* SD_GFX_THEMES_DIR = BICHO_STORAGE_ROOT "/gfx/themes";
constexpr const char* SD_GFX_EYES_DIR = BICHO_STORAGE_ROOT "/gfx/eyes";
constexpr const char* SD_GFX_ICONS_DIR = BICHO_STORAGE_ROOT "/gfx/icons";
constexpr const char* SD_USER_PROFILE_FILE = BICHO_STORAGE_ROOT "/memory/user_profile.json";
constexpr const char* SD_EPISODIC_LOG_FILE = BICHO_STORAGE_ROOT "/memory/episodic_log.jsonl";
constexpr const char* SD_SPEECH_LOG_FILE = BICHO_STORAGE_ROOT "/memory/speech_log.jsonl";
constexpr const char* SD_DAILY_SUMMARY_FILE = BICHO_STORAGE_ROOT "/memory/daily_summaries.jsonl";
constexpr const char* SD_HABIT_MATRIX_FILE = BICHO_STORAGE_ROOT "/memory/habit_matrix.json";
constexpr const char* SD_PIP_INDEX_FILE = BICHO_STORAGE_ROOT "/snapshots/pip_index.txt";

// Última foto da câmera: no PC é escrita pelo webcam_tracker.py na raiz do projeto; no CoreS3, no cartão
#if defined(ARDUINO)
constexpr const char* CAM_SNAP_FILE_PATH = BICHO_STORAGE_ROOT "/cam_snap.jpg";
#else
constexpr const char* CAM_SNAP_FILE_PATH = "cam_snap.jpg";
#endif

// Monta o cartão (CoreS3) e cria a pasta raiz. Idempotente; retorna se o armazenamento está utilizável.
bool storageBegin();
bool storageIsReady();

// Caminho completo para um caminho relativo à raiz (ex.: "memory/speech_log.jsonl")
std::string storagePath(const std::string& relative_path);

bool storageExists(const std::string& path);
long storageFileSize(const std::string& path);                 // -1 se não existir
bool storageMakeDirs(const std::string& path);                 // Cria a pasta e as intermediárias
bool storageRemove(const std::string& path);
std::vector<std::string> storageListDir(const std::string& dir_path); // Nomes dos arquivos (sem "." e "..")

bool storageReadFile(const std::string& path, std::vector<uint8_t>& out_bytes);
bool storageReadText(const std::string& path, std::string& out_text);
std::vector<std::string> storageReadLines(const std::string& path); // Ignora linhas vazias e '\r' final

// Escrita atômica (arquivo temporário + rename): um desligamento no meio não corrompe o original
bool storageWriteFile(const std::string& path, const void* data, size_t size);
bool storageWriteText(const std::string& path, const std::string& text);
bool storageAppendLine(const std::string& path, const std::string& line);

#endif // STORAGE_H
