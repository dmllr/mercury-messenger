#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>

#ifndef _WIN32
#include <pthread.h>
#endif

#include "hela/eillance/rtc/client.hpp"
#include "hela/eillance/crypto/crypto.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/string.hpp"

static std::atomic<bool> is_running = true;
static std::string active_peer_id;

void PrintPrompt() {
    if (!active_peer_id.empty()) {
        std::cout << "\r[" << active_peer_id << "] > ";
    } else {
        std::cout << "\r> ";
    }
    std::cout.flush();
}

int main(int argc, char** argv) {
    // Determine configuration name from command-line arguments
    std::string config_name = "Default";
    if (argc > 1) {
        config_name = argv[1];
    }

    // Initialize Identity
#ifdef _WIN32
    const char* home_dir_env = getenv("USERPROFILE");
#else
    const char* home_dir_env = getenv("HOME");
#endif
    if (!home_dir_env) {
        LOGE << "Could not determine home directory.";
        return 1;
    }
    auto identity_path = std::filesystem::path(home_dir_env) / ".mercury" / config_name / "identity.key";
    std::unique_ptr<hela::eillance::crypto::Identity> identity = hela::eillance::crypto::Identity::LoadFromFile(identity_path);

    if (!identity) {
        std::cout << "No identity found for config '" << config_name << "', creating a new one at: " << identity_path.string() << std::endl;
        identity = hela::eillance::crypto::Identity::CreateNew();
        if (!identity || !identity->Save(identity_path)) {
            LOGE << "Failed to create and save a new identity.";
            return 1;
        }
        LOGI << "New identity created and saved.";
    }

    auto my_id = identity->GetId();
    std::cout << "================================================================" << std::endl;
    std::cout << " Mercury CLI Messenger" << std::endl;
    std::cout << " Config: " << config_name << std::endl;
    std::cout << " Your ID: " << my_id << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "Available commands:" << std::endl;
    std::cout << "  call <peer_id>    - Connect to a peer" << std::endl;
    std::cout << "  send <message>    - Send a message to the connected peer" << std::endl;
    std::cout << "  hangup            - Disconnect from the current peer" << std::endl;
    std::cout << "  id                - Show your ID" << std::endl;
    std::cout << "  exit              - Quit the application" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;


    // Initialize RTC Client
    auto rtc_client = std::make_shared<hela::eillance::RtcClient>();
    rtc_client->SetId(my_id);

    rtc_client->SetChatMessageReceivedCallback([](const std::string& peer_id, const std::string& message) {
        std::cout << "\r[" << peer_id << " says]: " << message << std::endl;
        PrintPrompt();
    });

    rtc_client->SetStatusCallback([&](const std::string& peer_id, hela::eillance::EventSource source, int state) {
        if (source == hela::eillance::EventSource::peerConnectionState) {
            auto pc_state = static_cast<rtc::PeerConnection::State>(state);
            if (pc_state == rtc::PeerConnection::State::Connected) {
                std::cout << "\r--> Connection established with " << peer_id << std::endl;
                active_peer_id = peer_id;
                PrintPrompt();
            } else if (pc_state == rtc::PeerConnection::State::Disconnected ||
                       pc_state == rtc::PeerConnection::State::Failed ||
                       pc_state == rtc::PeerConnection::State::Closed) {
                std::cout << "\r--> Connection with " << peer_id << " lost." << std::endl;
                if (active_peer_id == peer_id) {
                    active_peer_id.clear();
                }
                PrintPrompt();
            }
        }
    });

    rtc_client->Start();

    // Command loop
    std::thread input_thread([&my_id, rtc_client]() {
        std::string line;
        while (is_running) {
            PrintPrompt();
            if (!std::getline(std::cin, line)) {
                if(is_running) is_running = false;
                break;
            }
            if (!is_running) break;

            std::istringstream iss(line);
            std::string command;
            iss >> command;

            // Post task to main thread to interact with rtc_client
            hela::eillance::DispatchQueue::MainThread().Dispatch([command, line, my_id, rtc_client]() {
                std::istringstream iss_inner(line);
                std::string cmd_inner;
                iss_inner >> cmd_inner;

                if (command == "call") {
                    std::string peer_id;
                    iss_inner >> peer_id;
                    if (!peer_id.empty()) {
                        std::cout << "--> Calling " << peer_id << "..." << std::endl;
                        active_peer_id = peer_id;
                        rtc_client->Call(peer_id);
                    } else {
                        std::cout << "Usage: call <peer_id>" << std::endl;
                    }
                } else if (command == "send") {
                    if (active_peer_id.empty()) {
                        std::cout << "--> Not connected to any peer. Use 'call <peer_id>' first." << std::endl;
                        return;
                    }
                    std::string message;
                    std::getline(iss_inner >> std::ws, message);
                    if (!message.empty()) {
                        rtc_client->SendChatMessage(active_peer_id, message);
                    } else {
                         std::cout << "Usage: send <message>" << std::endl;
                    }
                } else if (command == "hangup") {
                    if (!active_peer_id.empty()) {
                        std::cout << "--> Hanging up on " << active_peer_id << std::endl;
                        rtc_client->Hangup(active_peer_id);
                        active_peer_id.clear();
                    } else {
                        std::cout << "--> Not in a call." << std::endl;
                    }
                } else if (command == "id") {
                    std::cout << "Your ID: " << my_id << std::endl;
                }
                else if (command == "exit") {
                    is_running = false;
                }
            });
             if (command == "exit") break;
        }
    });

    while(is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if(input_thread.joinable()){
        // A bit of a hack to unblock std::getline
        #ifndef _WIN32
        pthread_cancel(input_thread.native_handle());
        #endif
        input_thread.join();
    }

    rtc_client->Terminate();
    std::cout << "\r--> Shutting down." << std::endl;

    return 0;
}
