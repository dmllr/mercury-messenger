#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <curl/curl.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/args_parser.hpp"
#include "mercury/bot/bot.hpp"

namespace {

constexpr auto defaultHttpTimeoutSeconds = 60L;
constexpr std::string_view defaultOpenAiUrl = "https://mllr.i234.me:11480/v1/chat/completions";
constexpr std::string_view defaultModel = "local/llama3.2:3b";
constexpr std::string_view defaultSystemPrompt =
    "You are a human-like assistant. Keep answers concise and avoid unnecessary verbiage.";

std::sig_atomic_t volatile gSignalStatus = 0;  // NOLINT(*-avoid-non-const-global-variables)

void SignalHandler(int const signal) {
  gSignalStatus = signal;
}

struct LlmConfig {
  std::string api_url;
  std::string api_key;
  std::string model;
  std::string system_prompt;
};

class CurlGlobal final {
 public:
  CurlGlobal() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }

  ~CurlGlobal() {
    curl_global_cleanup();
  }

  CurlGlobal(CurlGlobal const&) = delete;
  auto operator=(CurlGlobal const&) -> CurlGlobal& = delete;
  CurlGlobal(CurlGlobal&&) = delete;
  auto operator=(CurlGlobal&&) -> CurlGlobal& = delete;
};

struct CurlSlistDeleter {
  void operator()(curl_slist* list) const noexcept {
    if (list != nullptr) {
      curl_slist_free_all(list);
    }
  }
};
using UniqueCurlSlist = std::unique_ptr<curl_slist, CurlSlistDeleter>;

struct CurlEasyDeleter {
  void operator()(CURL* handle) const noexcept {
    if (handle != nullptr) {
      curl_easy_cleanup(handle);
    }
  }
};
using UniqueCurlHandle = std::unique_ptr<CURL, CurlEasyDeleter>;

auto WriteCallback(char const* ptr, size_t const size, size_t nmemb, void* userdata) -> size_t {
  auto* response_body = static_cast<std::string*>(userdata);
  size_t const total_bytes = size * nmemb;
  response_body->append(ptr, total_bytes);
  return total_bytes;
}

auto EscapeJson(std::string_view input) -> std::string {
  std::string output;
  output.reserve(input.size() + 16);
  for (char const c : input) {
    switch (c) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::stringstream ss;
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
             << static_cast<int>(static_cast<unsigned char>(c));
          output += ss.str();
        } else {
          output += c;
        }
        break;
    }
  }
  return output;
}

auto UnescapeJson(std::string_view input) -> std::string {
  std::string output;
  output.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      ++i;
      switch (input[i]) {
        case '"': output += '"'; break;
        case '\\': output += '\\'; break;
        case '/': output += '/'; break;
        case 'b': output += '\b'; break;
        case 'f': output += '\f'; break;
        case 'n': output += '\n'; break;
        case 'r': output += '\r'; break;
        case 't': output += '\t'; break;
        default: output += input[i]; break;
      }
    } else {
      output += input[i];
    }
  }
  return output;
}

// Extracts the value of the "content" string key from an OpenAI/Ollama response JSON.
auto ExtractContentFromResponse(std::string_view json) -> std::optional<std::string> {
  constexpr std::string_view content_key = "\"content\"";
  size_t pos = json.find(content_key);
  while (pos != std::string_view::npos) {
    pos += content_key.size();
    while (pos < json.size() && ((std::isspace(static_cast<unsigned char>(json[pos])) != 0) || json[pos] == ':')) {
      ++pos;
    }
    if (pos < json.size() && json[pos] == '"') {
      ++pos;
      size_t const start = pos;
      bool escaped = false;
      while (pos < json.size()) {
        if (escaped) {
          escaped = false;
        } else if (json[pos] == '\\') {
          escaped = true;
        } else if (json[pos] == '"') {
          return UnescapeJson(json.substr(start, pos - start));
        }
        ++pos;
      }
      return std::nullopt;
    }
    pos = json.find(content_key, pos);
  }
  return std::nullopt;
}

auto QueryLlm(LlmConfig const& config, std::string_view user_message) -> std::optional<std::string> {
  UniqueCurlHandle const curl(curl_easy_init());
  if (!curl) {
    LGE << "Failed to allocate curl easy handle";
    return std::nullopt;
  }

  curl_slist* raw_headers = nullptr;
  raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
  if (!config.api_key.empty()) {
    std::string const auth_header = "Authorization: Bearer " + config.api_key;
    raw_headers = curl_slist_append(raw_headers, auth_header.c_str());
  }
  UniqueCurlSlist const headers(raw_headers);

  std::string payload = R"({"model":")" + EscapeJson(config.model) + R"(","stream":false,"messages":[)";
  if (!config.system_prompt.empty()) {
    payload += R"({"role":"system","content":")" + EscapeJson(config.system_prompt) + "\"},";
  }
  payload += R"({"role":"user","content":")" + EscapeJson(user_message) + "\"}]}";

  std::string response_body;
  curl_easy_setopt(curl.get(), CURLOPT_URL, config.api_url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, defaultHttpTimeoutSeconds);

  if (CURLcode const res = curl_easy_perform(curl.get()); res != CURLE_OK) {
    LGE << "LLM request failed: " << curl_easy_strerror(res);
    return std::nullopt;
  }

  std::int32_t response_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code < 200 || response_code >= 300) {
    LGE << "LLM HTTP error status " << response_code << ": " << response_body;
    return std::nullopt;
  }

  auto content = ExtractContentFromResponse(response_body);
  if (!content) {
    LGE << "Failed to parse content from response: " << response_body;
  }
  return content;
}

auto ResolveConfigValue(std::optional<std::string> const& cli_arg, char const* env_name,
    std::string_view const fallback = "") -> std::string {
  if (cli_arg && !cli_arg->empty()) {
    return *cli_arg;
  }
  if (char const* env_val = std::getenv(env_name); env_val != nullptr && *env_val != '\0') { // NOLINT(*-mt-unsafe)
    return {env_val};
  }
  return std::string(fallback);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  static plog::ColorConsoleAppender<plog::TxtFormatter> console_appender;
  Log::Initialize(&console_appender, plog::info);

  CurlGlobal const curl_raii;
  eillance::utils::ArgsParser const args(argc, argv);

  auto const identity_dir_str = args.Get("--identity-dir");
  if (!identity_dir_str) {
    LGE << "Missing required parameter: --identity-dir <path>";
    return 1;
  }

  std::filesystem::path const identity_path(*identity_dir_str);
  if (!std::filesystem::is_directory(identity_path)) {
    LGE << "Identity directory does not exist: " << identity_path;
    return 1;
  }

  LlmConfig const config{
      .api_url = ResolveConfigValue(args.Get("--api-url"), "LLM_API_URL", defaultOpenAiUrl),
      .api_key = ResolveConfigValue(args.Get("--api-key"), "OPENAI_API_KEY",
                                    ResolveConfigValue(std::nullopt, "LLM_API_KEY")),
      .model = ResolveConfigValue(args.Get("--model"), "LLM_MODEL", defaultModel),
      .system_prompt = ResolveConfigValue(args.Get("--system-prompt"), "LLM_SYSTEM_PROMPT", defaultSystemPrompt),
  };

  mercury::bot::Bot::Options options;
  options.identity_path = identity_path;
  options.auto_accept_contacts = true;
  options.auto_reject_calls = true;

  try {
    mercury::bot::Bot const bot(options);
    std::string const bot_id = bot.GetId();
    LGI << "LLM Bot initialized with ID: " << bot_id;

    bot.OnMessage([&bot, &config, bot_id](mercury::bot::Message const& message) -> void {
      if (message.from.id == bot_id) {
        return;
      }

      bot.MarkAsRead(message.chat_id, message.message_id);

      // Execute LLM call off the event loop thread to avoid blocking client messaging.
      std::thread([&bot, &config, chat_id = message.chat_id, text = message.text]() -> void {
        if (auto const reply = QueryLlm(config, text); reply && !reply->empty()) {
          bot.SendMessage(chat_id, *reply);
        }
      }).detach();
    });

    bot.Start();

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    LGI << "Bot is running. Press Ctrl+C to exit.";
    while (gSignalStatus == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LGI << "Shutdown signal received. Stopping bot...";
    bot.Stop();
  } catch (std::exception const& e) {
    LGE << "Fatal error: " << e.what();
    return 1;
  }

  return 0;
}
