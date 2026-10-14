#include <unity.h>
#include <string>
#include <unistd.h>
#include <vector>
#include "retention.h"
#include "storage.h"

// Pasta temporária isolada: nunca aponta para sd_card/ (dados reais)
static const std::string TMP = "test_tmp_retention";

static void clearTmp() {
    for (const std::string& name : storageListDir(TMP)) {
        storageRemove(TMP + "/" + name);
    }
}

void setUp() {
    storageMakeDirs(TMP);
    clearTmp();
}

void tearDown() {
    clearTmp();
}

static void touch(const std::string& name, size_t size) {
    std::string data(size, 'x');
    storageWriteText(TMP + "/" + name, data);
}

void test_prune_by_count_keeps_newest_by_name() {
    touch("snap_20261010_090000_A.jpg", 10);
    touch("snap_20261011_090000_B.jpg", 10);
    touch("snap_20261012_090000_C.jpg", 10);
    touch("pip_20261010_090000_A.raw", 10);   // Outro prefixo: intocado
    touch("snap_notes.txt", 10);              // Outro sufixo: intocado
    TEST_ASSERT_EQUAL(1, static_cast<int>(pruneFilesByCount(TMP, "snap_", ".jpg", 2)));
    TEST_ASSERT_FALSE(storageExists(TMP + "/snap_20261010_090000_A.jpg"));
    TEST_ASSERT_TRUE(storageExists(TMP + "/snap_20261012_090000_C.jpg"));
    TEST_ASSERT_TRUE(storageExists(TMP + "/pip_20261010_090000_A.raw"));
    TEST_ASSERT_TRUE(storageExists(TMP + "/snap_notes.txt"));
    TEST_ASSERT_EQUAL(0, static_cast<int>(pruneFilesByCount(TMP, "snap_", ".jpg", 5)));
}

void test_prune_by_size_stops_when_under_limit() {
    touch("tts_a.wav", 400);
    touch("tts_b.wav", 400);
    touch("tts_c.wav", 400);
    touch("leia.txt", 5000); // Não é .wav: não conta nem é apagado
    size_t removed = pruneFilesBySize(TMP, ".wav", 900);
    TEST_ASSERT_EQUAL(1, static_cast<int>(removed));
    size_t wav_left = 0;
    for (const std::string& name : storageListDir(TMP)) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".wav") {
            ++wav_left;
        }
    }
    TEST_ASSERT_EQUAL(2, static_cast<int>(wav_left));
    TEST_ASSERT_TRUE(storageExists(TMP + "/leia.txt"));
    TEST_ASSERT_EQUAL(0, static_cast<int>(pruneFilesBySize(TMP, ".wav", 900)));
}

void test_prune_dated_files() {
    touch("2026-09-01.jsonl", 5);
    touch("2026-10-13.jsonl", 5);
    touch("2026-10-14.jsonl", 5);
    touch("days.jsonl", 5);            // Nome sem data: preservado
    TEST_ASSERT_EQUAL(1, static_cast<int>(pruneDatedFiles(TMP, ".jsonl", "2026-10-13")));
    TEST_ASSERT_FALSE(storageExists(TMP + "/2026-09-01.jsonl"));
    TEST_ASSERT_TRUE(storageExists(TMP + "/2026-10-13.jsonl"));
    TEST_ASSERT_TRUE(storageExists(TMP + "/days.jsonl"));
    // Sem relógio válido (data vazia) nada é apagado
    TEST_ASSERT_EQUAL(0, static_cast<int>(pruneDatedFiles(TMP, ".jsonl", "")));
}

void test_trim_lines_keeps_last() {
    std::string path = TMP + "/log.jsonl";
    for (int i = 0; i < 10; ++i) {
        storageAppendLine(path, "{\"n\":" + std::to_string(i) + "}");
    }
    TEST_ASSERT_EQUAL(7, static_cast<int>(trimLinesFile(path, 3)));
    std::vector<std::string> lines = storageReadLines(path);
    TEST_ASSERT_EQUAL(3, static_cast<int>(lines.size()));
    TEST_ASSERT_EQUAL_STRING("{\"n\":7}", lines[0].c_str());
    TEST_ASSERT_EQUAL(0, static_cast<int>(trimLinesFile(path, 3)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(trimLinesFile(TMP + "/nao_existe.jsonl", 3)));
}

void test_date_days_ago_format() {
    std::string today = retentionDateDaysAgo(0);
    std::string month_ago = retentionDateDaysAgo(30);
    TEST_ASSERT_EQUAL(10, static_cast<int>(today.size()));
    TEST_ASSERT_TRUE(month_ago < today);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_prune_by_count_keeps_newest_by_name);
    RUN_TEST(test_prune_by_size_stops_when_under_limit);
    RUN_TEST(test_prune_dated_files);
    RUN_TEST(test_trim_lines_keeps_last);
    RUN_TEST(test_date_days_ago_format);
    int result = UNITY_END();
    rmdir(TMP.c_str());
    return result;
}
