#include <cstring>
#include <string>
#include <vector>

#include <jni.h>

#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "transport_impl_libwebrtc.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerConnectionStateChange(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);
    transport->ReportStateChange(peer_id, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingConnectionStateChange(
    JNIEnv* /*env*/,
    jobject,
    jlong native_ptr,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    auto service_state = eillance::ServiceConnectionState::unknown;
    // Mapping matches SignalingClient.State ordinal
    // CONNECTING = 0, OPEN = 1, CLOSED = 2, FAILED = 3, NO_NETWORK = 4
    switch (state) {
      case 0:  // CONNECTING
        service_state = eillance::ServiceConnectionState::establishing;
        break;
      case 1:  // OPEN
        service_state = eillance::ServiceConnectionState::online;
        break;
      case 2:  // CLOSED
        service_state = eillance::ServiceConnectionState::disconnected;
        break;
      case 3:  // FAILED
        service_state = eillance::ServiceConnectionState::failure;
        break;
      case 4:  // NO_NETWORK
        service_state = eillance::ServiceConnectionState::noNetwork;
        break;
      default:
        break;
    }
    if (service_state != eillance::ServiceConnectionState::unknown) {
      transport
          ->ReportStateChange(eillance::ContactID(), eillance::EventSource::system, static_cast<int>(service_state));
    }
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      auto const json_msg = nlohmann::json::parse(msg_chars);
      transport->Delegate()->ProcessPeerMessage(json_msg);
    } catch (std::exception const& e) {
      LGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSystemMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring type,
    jstring message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    char const* type_chars = env->GetStringUTFChars(type, nullptr);
    std::string const type_str(type_chars);
    env->ReleaseStringUTFChars(type, type_chars);

    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    std::string const msg_str(msg_chars);
    env->ReleaseStringUTFChars(message, msg_chars);

    try {
      auto const json_msg = nlohmann::json::parse(msg_str);
      transport->Delegate()->OnSystemMessage(type_str, json_msg);
    } catch (std::exception const& e) {
      LGE << "Failed to parse system message: " << e.what();
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring peer_id,
    jstring error
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    char const* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    char const* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->Delegate()->OnSignalingErrorForPeer(eillance::ContactID(peer_id_chars), error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jstring j_message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    char const* message_chars = env->GetStringUTFChars(j_message, nullptr);

    eillance::ContactID const peer_id(peer_id_chars);
    auto const* message_str(message_chars);

    auto len = std::strlen(message_str);
    std::vector<std::byte> msg_bytes(len);
    std::memcpy(msg_bytes.data(), message_str, len);

    auto peer = transport->GetPeer(peer_id);
    if (peer && peer->on_data_channel_message) {
      peer->on_data_channel_message(msg_bytes);
    } else {
      LGW << "Received DC message for peer " << peer_id_chars << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(j_message, message_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessageBinary(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jbyteArray j_data
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);

    jsize len = env->GetArrayLength(j_data);
    std::vector<std::byte> buffer(len);
    env->GetByteArrayRegion(j_data, 0, len, reinterpret_cast<jbyte*>(buffer.data()));

    auto peer = transport->GetPeer(peer_id);
    if (peer && peer->on_data_channel_message) {
      peer->on_data_channel_message(buffer);
    } else {
      LGW << "Received DC binary message for peer " << peer_id_chars << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelOpen(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);

    eillance::ContactID const peer_id(peer_id_chars);
    auto peer = transport->GetPeer(peer_id);

    if (peer) {
      LGI << "Data channel with " << peer_id_chars << " opened.";
      peer->data_channel_is_open = true;
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      nlohmann::json const key_exchange_message = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}
      };
      transport->SendPeerDataChannelMessage(peer_id, key_exchange_message);

      // Force UI to show Connected state when Data Channel opens, regardless of ICE state
      transport->ReportStateChange(
          peer_id,
          eillance::EventSource::peerConnectionState,
          static_cast<int>(eillance::transport::PeerConnectionState::connected)
      );
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

// NOLINTEND(readability-identifier-naming)
