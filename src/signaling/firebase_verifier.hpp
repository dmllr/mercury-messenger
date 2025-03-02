#include <chrono>
#include <string>
#include <fstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <plog/Log.h>

class FirebaseVerifier {
  using Json = nlohmann::json;

public:
  FirebaseVerifier() {
    LoadServiceAccount();
  }

  std::optional<std::string> VerifyToken(const std::string& token) {
    try {
      // Use Google's token verification endpoint
      const std::string verify_url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup";

      // Make the verification request
      auto post = cpr::Post(
          cpr::Url{verify_url},
          cpr::Parameters{{"key", apiKey_}},
          cpr::Body{R"({"idToken":")" + token + R"("})"},
          cpr::Header{{"Content-Type", "application/json"}}
      );

      if (post.status_code != cpr::status::HTTP_OK) {
        LOGE << "Token verification failed: " << post.text;
        return std::nullopt;
      }

      // Parse the response
      Json response = Json::parse(post.text);

      // Check if the token is valid
      if (response["users"].empty()) {
        LOGE << "Token doesn't belong to any user";
        return std::nullopt;
      }

      // Return the user ID
      return response["users"][0]["localId"];

    } catch (const std::exception& e) {
      LOGE << "Token verification failed: " << e.what();
      return std::nullopt;
    }
  }

private:
  std::string apiKey_;

  void LoadServiceAccount() {
    // Try to get API key from environment variable first
    const char* env_api_key = std::getenv("FIREBASE_API_KEY"); // NOLINT(*-mt-unsafe)
    if (env_api_key != nullptr) {
      apiKey_ = env_api_key;
    } else {
      // Fallback to a separate config file
      const char* env_api_key_path = std::getenv("FIREBASE_API_KEY_PATH"); // NOLINT(*-mt-unsafe)
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
};
