#include <chrono>
#include <string>
#include <fstream>
#include <memory>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <plog/Log.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

class FirebaseVerifier {
  using Json = nlohmann::json;

public:
  FirebaseVerifier() {
    LoadServiceAccount();
    DownloadCaCertBundle();
  }

  FirebaseVerifier(FirebaseVerifier&&) = delete;
  FirebaseVerifier(const FirebaseVerifier&) = delete;
  FirebaseVerifier& operator=(FirebaseVerifier&&) = delete;
  FirebaseVerifier& operator=(const FirebaseVerifier&) = delete;

  ~FirebaseVerifier() {
    if (!caCertBundlePath_.empty()) {
      std::remove(caCertBundlePath_.c_str());
    }
  }

  std::optional<std::string> VerifyToken(const std::string& token) {
    try {
      const std::string verify_url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup";
      auto post = cpr::Post(
          cpr::Url{verify_url},
          cpr::Parameters{{"key", apiKey_}},
          cpr::Body{R"({"idToken":")" + token + R"("})"},
          cpr::Header{{"Content-Type", "application/json"}},
          cpr::SslOptions{cpr::Ssl(cpr::ssl::CaInfo{caCertBundlePath_})}
      );

      if (post.status_code != cpr::status::HTTP_OK) {
        LOGE << "Token verification failed: " << post.text << " | " << post.error.message;
        return std::nullopt;
      }

      Json response = Json::parse(post.text);
      if (response["users"].empty()) {
        LOGE << "Token doesn't belong to any user";
        return std::nullopt;
      }

      return response["users"][0]["localId"];
    } catch (const std::exception& e) {
      LOGE << "Token verification failed: " << e.what();
      return std::nullopt;
    }
  }

private:
  std::string apiKey_;
  std::string caCertBundlePath_;

  void LoadServiceAccount() {
    const auto* env_api_key = std::getenv("FIREBASE_API_KEY"); // NOLINT(*-mt-unsafe)
    if (env_api_key != nullptr) {
      apiKey_ = env_api_key;
    } else {
      const auto* env_api_key_path = std::getenv("FIREBASE_API_KEY_PATH"); // NOLINT(*-mt-unsafe)
      if (env_api_key_path != nullptr) {
        std::ifstream api_key_file(env_api_key_path);
        if (api_key_file.is_open()) {
          std::getline(api_key_file, apiKey_);
        }
      }
    }

    if (apiKey_.empty()) {
      LOGE << "ERROR: API key not found. Please set FIREBASE_API_KEY or FIREBASE_API_KEY_PATH environment variables";
    }
  }

  void DownloadCaCertBundle() {
    try {
      auto response = cpr::Get(
          cpr::Url{"https://curl.se/ca/cacert.pem"},
          cpr::VerifySsl{false}
      );

      if (response.status_code == cpr::status::HTTP_OK) {
        caCertBundlePath_ = GetTempFilePath("ca_bundle.pem");
        auto out_file = std::ofstream(caCertBundlePath_, std::ios::binary);
        out_file << response.text;
        LOGI << "Successfully downloaded CA certificate bundle to " << caCertBundlePath_;
      } else {
        LOGE << "Failed to download CA certificate bundle: " << response.status_code
             << " | " << response.error.message;
      }
    } catch (const std::exception& e) {
      LOGE << "Exception while downloading CA certificate bundle: " << e.what();
    }
  }

  static std::string GetTempFilePath(const std::string& filename) {
#if defined(_WIN32)
    auto path = std::array<char, MAX_PATH>();
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path.data()))) {
      return std::string(path.data()) + "\\" + filename;
    }
#elif defined(__linux__) || defined(__APPLE__)
    const char* tempDir = std::getenv("TMPDIR");
    if (!tempDir) {
      tempDir = "/tmp";
    }
    return std::string(tempDir) + "/" + filename;
#endif
    return filename;
  }
};
