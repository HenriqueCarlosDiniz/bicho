#include <unity.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "storage.h"
#include <unistd.h>

// Pasta temporária isolada (não toca em sd_card/)
static const std::string TMP_ROOT = "test_tmp_storage";

static void removeTree() {
    for (const std::string& sub : {std::string("a/b/c"), std::string("a/b"), std::string("a")}) {
        std::string dir = TMP_ROOT + "/" + sub;
        for (const std::string& name : storageListDir(dir)) {
            storageRemove(dir + "/" + name);
        }
        rmdir(dir.c_str());
    }
    for (const std::string& name : storageListDir(TMP_ROOT)) {
        storageRemove(TMP_ROOT + "/" + name);
    }
}

void setUp() {
    removeTree();
    storageMakeDirs(TMP_ROOT);
}

void tearDown() {
    removeTree();
}

void test_path_builds_from_root() {
    TEST_ASSERT_EQUAL_STRING("sd_card/memory/x.jsonl", storagePath("memory/x.jsonl").c_str());
    TEST_ASSERT_EQUAL_STRING("sd_card/memory/x.jsonl", storagePath("/memory/x.jsonl").c_str());
    TEST_ASSERT_EQUAL_STRING("sd_card", storagePath("").c_str());
    TEST_ASSERT_EQUAL_STRING("sd_card/config.json", SD_CONFIG_FILE_PATH);
}

void test_make_nested_dirs() {
    std::string nested = TMP_ROOT + "/a/b/c";
    TEST_ASSERT_TRUE(storageMakeDirs(nested));
    TEST_ASSERT_TRUE(storageExists(nested));
    TEST_ASSERT_TRUE(storageMakeDirs(nested)); // Idempotente
}

void test_write_read_and_atomic_overwrite() {
    std::string path = TMP_ROOT + "/file.bin";
    const uint8_t data1[] = {1, 2, 3, 0, 255};
    TEST_ASSERT_TRUE(storageWriteFile(path, data1, sizeof(data1)));
    TEST_ASSERT_EQUAL_INT32(5, storageFileSize(path));

    std::vector<uint8_t> back;
    TEST_ASSERT_TRUE(storageReadFile(path, back));
    TEST_ASSERT_EQUAL(5, static_cast<int>(back.size()));
    TEST_ASSERT_EQUAL_UINT8(0, back[3]);
    TEST_ASSERT_EQUAL_UINT8(255, back[4]);

    // Sobrescrita (rename sobre arquivo existente) e sem sobrar .tmp
    TEST_ASSERT_TRUE(storageWriteText(path, "novo"));
    std::string text;
    TEST_ASSERT_TRUE(storageReadText(path, text));
    TEST_ASSERT_EQUAL_STRING("novo", text.c_str());
    TEST_ASSERT_FALSE(storageExists(path + ".tmp"));

    // Arquivo vazio
    TEST_ASSERT_TRUE(storageWriteText(path, ""));
    TEST_ASSERT_EQUAL_INT32(0, storageFileSize(path));
}

void test_append_and_read_lines() {
    std::string path = TMP_ROOT + "/log.jsonl";
    TEST_ASSERT_TRUE(storageAppendLine(path, "{\"a\":1}"));
    TEST_ASSERT_TRUE(storageAppendLine(path, "{\"a\":2}"));
    std::vector<std::string> lines = storageReadLines(path);
    TEST_ASSERT_EQUAL(2, static_cast<int>(lines.size()));
    TEST_ASSERT_EQUAL_STRING("{\"a\":2}", lines[1].c_str());

    // CRLF e linhas vazias de arquivos editados no Windows
    TEST_ASSERT_TRUE(storageWriteText(path, "x\r\n\r\ny\r\n"));
    lines = storageReadLines(path);
    TEST_ASSERT_EQUAL(2, static_cast<int>(lines.size()));
    TEST_ASSERT_EQUAL_STRING("x", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("y", lines[1].c_str());
}

void test_missing_file_behaviour() {
    std::string path = TMP_ROOT + "/nao_existe.txt";
    std::string text = "antes";
    TEST_ASSERT_FALSE(storageExists(path));
    TEST_ASSERT_EQUAL_INT32(-1, storageFileSize(path));
    TEST_ASSERT_FALSE(storageReadText(path, text));
    TEST_ASSERT_TRUE(storageReadLines(path).empty());
    TEST_ASSERT_FALSE(storageRemove(path));
    TEST_ASSERT_TRUE(storageListDir(TMP_ROOT + "/nao_existe_dir").empty());
}

void test_list_and_remove() {
    TEST_ASSERT_TRUE(storageWriteText(TMP_ROOT + "/um.txt", "1"));
    TEST_ASSERT_TRUE(storageWriteText(TMP_ROOT + "/dois.txt", "2"));
    std::vector<std::string> names = storageListDir(TMP_ROOT);
    std::sort(names.begin(), names.end());
    TEST_ASSERT_EQUAL(2, static_cast<int>(names.size()));
    TEST_ASSERT_EQUAL_STRING("dois.txt", names[0].c_str());
    TEST_ASSERT_TRUE(storageRemove(TMP_ROOT + "/um.txt"));
    TEST_ASSERT_EQUAL(1, static_cast<int>(storageListDir(TMP_ROOT).size()));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_path_builds_from_root);
    RUN_TEST(test_make_nested_dirs);
    RUN_TEST(test_write_read_and_atomic_overwrite);
    RUN_TEST(test_append_and_read_lines);
    RUN_TEST(test_missing_file_behaviour);
    RUN_TEST(test_list_and_remove);
    int result = UNITY_END();
    rmdir(TMP_ROOT.c_str());
    return result;
}
