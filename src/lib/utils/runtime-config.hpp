#include <fstream>
#include <filesystem>
#include <iostream>
#include <functional>
#include <string_view>

class RuntimeConfiguration {
public:
    RuntimeConfiguration(): tmpDir(std::filesystem::temp_directory_path() / std::filesystem::unique_path()) {
        std::filesystem::create_directories(tmpDir);
    }

    ~RuntimeConfiguration() {
        std::filesystem::remove_all(tmpDir);
    }

    RuntimeConfiguration& configure(std::string_view fileName, std::string_view fileContents) {
        std::ofstream file(tmpDir / fileName.data());
        if (!file) {
            throw std::runtime_error("Failed to create file");
        }

        file << fileContents;
        return *this;
    }

    void execute(std::function<void(std::string_view)> fn) {
        fn(tmpDir.string());
    }

private:
    std::filesystem::path tmpDir;
};
