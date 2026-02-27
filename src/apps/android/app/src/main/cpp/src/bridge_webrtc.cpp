#include <cstring>
#include <string>
#include <vector>

#include <jni.h>

#include "data_transport_impl_libwebrtc.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "media_transport_impl_libwebrtc.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnPeerConnectionStateChange(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jint context_val,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);
    transport->ReportStateChange(peer_id, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcMediaTransport_nativeOnPeerConnectionStateChange(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jint context_val,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidMediaTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);
    transport->ReportStateChange(peer_id, eillance::EventSource::mediaConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnSignalingConnectionStateChange(
    JNIEnv* /*env*/,
    jobject,
    jlong native_ptr,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    auto service_state = eillance::ServiceConnectionState::unknown;
    switch (state) {
      case 0:
        service_state = eillance::ServiceConnectionState::establishing;
        break;
      case 1:
        service_state = eillance::ServiceConnectionState::online;
        break;
      case 2:
        service_state = eillance::ServiceConnectionState::disconnected;
        break;
      case 3:
        service_state = eillance::ServiceConnectionState::failure;
        break;
      case 4:
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

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnPeerMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      auto const json_msg = nlohmann::json::parse(msg_chars);
      eillance::EventQueue::Instance().Push(eillance::EventPeerMessage{json_msg});
    } catch (std::exception const& e) {
      LGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnSystemMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring type,
    jstring message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
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

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnSignalingError(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring peer_id,
    jstring error
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    char const* error_chars = env->GetStringUTFChars(error, nullptr);
    eillance::EventQueue::Instance().Push(eillance::EventSignalingError{eillance::ContactID(peer_id_chars), error_chars}
    );
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnDataChannelMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jstring j_message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    char const* message_chars = env->GetStringUTFChars(j_message, nullptr);

    eillance::ContactID const peer_id(peer_id_chars);
    auto const* message_str(message_chars);

    auto len = std::strlen(message_str);
    std::vector<std::byte> msg_bytes(len);
    std::memcpy(msg_bytes.data(), message_str, len);

    eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer_id, msg_bytes});

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(j_message, message_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnDataChannelMessageBinary(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jbyteArray j_data
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);

    jsize len = env->GetArrayLength(j_data);
    std::vector<std::byte> buffer(len);
    env->GetByteArrayRegion(j_data, 0, len, reinterpret_cast<jbyte*>(buffer.data()));

    eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer_id, buffer});

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcDataTransport_nativeOnDataChannelOpen(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidDataTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);
    eillance::EventQueue::Instance().Push(eillance::EventDataChannelOpen{peer_id});
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

// NOLINTEND(readability-identifier-naming)
