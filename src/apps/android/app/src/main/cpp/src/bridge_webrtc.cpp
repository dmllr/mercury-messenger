#include <cstring>
#include <string>
#include <vector>

#include <jni.h>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "media_transport_impl_libwebrtc.hpp"

// NOLINTBEGIN(readability-identifier-naming)

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
    transport->ReportPeerStateChange(peer_id, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcMediaTransport_nativeOnSendMediaNegotiationMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidMediaTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      auto const json_msg = nlohmann::json::parse(msg_chars);
      eillance::EventQueue::Instance().Push(eillance::EventSendMediaNegotiationMessage{json_msg});
    } catch (std::exception const& e) {
      LGE << "Failed to parse media signaling message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

// NOLINTEND(readability-identifier-naming)
