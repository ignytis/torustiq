#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/inputs/directory_input.h"

namespace fs = std::filesystem;

namespace torustiq {
namespace {

int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<std::vector<Message>*>(user_data)->push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

class DirectoryInputTest : public ::testing::Test {
  protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("torustiq_dir_test_" +
                std::to_string(std::hash<std::string>{}(
                    ::testing::UnitTest::GetInstance()->current_test_info()->name())));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_directory_input(&registrar), TORUSTIQ_OK);
        vtable_ = registry_.FindInput("directory");
        ASSERT_NE(vtable_, nullptr);
    }
    void TearDown() override {
        fs::remove_all(dir_);
    }

    void Write(const std::string& name, const std::string& content) {
        std::ofstream(dir_ / name, std::ios::binary) << content;
    }

    int32_t Run(std::vector<Message>& out, const std::string& path) {
        std::map<std::string, std::string> params = {{"path", path}};
        ConfigView config(params);
        void* handle = nullptr;
        EXPECT_EQ(vtable_->create(&config.get(), &handle), TORUSTIQ_OK);
        const int32_t status = vtable_->run(handle, Collect, &out);
        vtable_->destroy(handle);
        return status;
    }

    fs::path dir_;
    PluginRegistry registry_;
    const torustiq_input_vtable* vtable_ = nullptr;
};

TEST_F(DirectoryInputTest, EachFileIsARecordSortedByName) {
    Write("b.txt", "second");
    Write("a.txt", std::string("fi\0rst", 6));
    fs::create_directories(dir_ / "subdir"); // directories are skipped
    std::vector<Message> messages;
    ASSERT_EQ(Run(messages, dir_.string()), TORUSTIQ_OK);
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0].headers["filename"], "a.txt");
    EXPECT_EQ(messages[0].body.size(), 6u);
    EXPECT_EQ(messages[1].headers["filename"], "b.txt");
    EXPECT_EQ(std::string(messages[1].body.begin(), messages[1].body.end()), "second");
}

TEST_F(DirectoryInputTest, EmptyDirectoryAndEmptyFile) {
    std::vector<Message> messages;
    ASSERT_EQ(Run(messages, dir_.string()), TORUSTIQ_OK);
    EXPECT_TRUE(messages.empty());
    Write("empty", "");
    ASSERT_EQ(Run(messages, dir_.string()), TORUSTIQ_OK);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_TRUE(messages[0].body.empty());
}

TEST_F(DirectoryInputTest, MissingDirectoryIsAnError) {
    std::vector<Message> messages;
    EXPECT_EQ(Run(messages, (dir_ / "nope").string()), TORUSTIQ_ERROR);
}

TEST_F(DirectoryInputTest, MissingPathConfigFailsCreate) {
    void* handle = nullptr;
    torustiq_config empty{};
    EXPECT_EQ(vtable_->create(&empty, &handle), TORUSTIQ_ERROR);
}

} // namespace
} // namespace torustiq
