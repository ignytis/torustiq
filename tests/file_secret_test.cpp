#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "core/plugin_registry.h"
#include "plugins/secrets/file_secret.h"

namespace fs = std::filesystem;

namespace torustiq {
namespace {

int32_t Collect(void* user_data, const torustiq_bytes* value) {
    static_cast<std::string*>(user_data)->assign(reinterpret_cast<const char*>(value->data),
                                                 value->len);
    return TORUSTIQ_OK;
}

class FileSecretTest : public testing::Test {
  protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("torustiq_file_secret_test_" +
                std::to_string(std::hash<std::string>{}(
                    testing::UnitTest::GetInstance()->current_test_info()->name())));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_file_secret(&registrar), TORUSTIQ_OK);
        vtable_ = registry_.FindSecret("file");
        ASSERT_NE(vtable_, nullptr);
    }
    void TearDown() override {
        fs::remove_all(dir_);
    }

    // Writes `content` and resolves it through the provider.
    int32_t ResolveFile(const std::string& content, std::string& out) {
        const fs::path path = dir_ / "secret.txt";
        std::ofstream(path, std::ios::binary) << content;
        return ResolvePath(path.string(), out);
    }

    int32_t ResolvePath(const std::string& path, std::string& out) {
        const torustiq_header item{"path", path.c_str()};
        const torustiq_config config{&item, 1};
        void* handle = nullptr;
        if (vtable_->create(&config, &handle) != TORUSTIQ_OK) {
            return TORUSTIQ_ERROR;
        }
        const int32_t status = vtable_->resolve(handle, Collect, &out);
        vtable_->destroy(handle);
        return status;
    }

    fs::path dir_;
    PluginRegistry registry_;
    const torustiq_secret_vtable* vtable_ = nullptr;
};

} // namespace

TEST_F(FileSecretTest, ReadsFileContent) {
    std::string value;
    ASSERT_EQ(ResolveFile("hunter2", value), TORUSTIQ_OK);
    EXPECT_EQ(value, "hunter2");
}

TEST_F(FileSecretTest, StripsOneTrailingNewline) {
    std::string value;
    ASSERT_EQ(ResolveFile("hunter2\n", value), TORUSTIQ_OK);
    EXPECT_EQ(value, "hunter2");
    ASSERT_EQ(ResolveFile("hunter2\r\n", value), TORUSTIQ_OK);
    EXPECT_EQ(value, "hunter2");
    ASSERT_EQ(ResolveFile("hunter2\n\n", value), TORUSTIQ_OK);
    EXPECT_EQ(value, "hunter2\n");
}

TEST_F(FileSecretTest, KeepsInnerWhitespaceAndEmptyFile) {
    std::string value;
    ASSERT_EQ(ResolveFile(" a b \n", value), TORUSTIQ_OK);
    EXPECT_EQ(value, " a b ");
    ASSERT_EQ(ResolveFile("", value), TORUSTIQ_OK);
    EXPECT_EQ(value, "");
}

TEST_F(FileSecretTest, MissingFileFails) {
    std::string value;
    EXPECT_EQ(ResolvePath((dir_ / "nope.txt").string(), value), TORUSTIQ_ERROR);
}

TEST_F(FileSecretTest, MissingPathParameterFailsToCreate) {
    const torustiq_config config{nullptr, 0};
    void* handle = nullptr;
    EXPECT_EQ(vtable_->create(&config, &handle), TORUSTIQ_ERROR);
}

} // namespace torustiq
