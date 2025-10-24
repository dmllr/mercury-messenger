#pragma once

#include <jni.h>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include "eillance/transport/transport.hpp"
#include "eillance/logging/logging.h"
#include "../utils/jni_utils.hpp"

namespace eillance::transport {

class AndroidRtcTransport final : public Transport::TransportInterface {
  JavaVM* jvm_;
  jobject transport_java_ref_{nullptr};
  TransportDelegate* delegate_{nullptr};

  // This helper must be const to be callable from other const methods.
  auto Attach() const -> JNIEnv* { return eillance::jni::AttachCurrentThreadIfNeeded(jvm_); }

public:
  explicit AndroidRtcTransport(JavaVM* jvm, jobject context) : jvm_(jvm) {
    JNIEnv* env = Attach();
    if (!env) return;

    jclass transportClass = env->FindClass("hela/eillance/webrtc/WebRtcTransport");
    if (!transportClass) return;

    jmethodID constructor = env->GetMethodID(transportClass, "<init>", "(Landroid/content/Context;J)V");
    if (!constructor) return;

    jobject transport_local_ref = env->NewObject(transportClass, constructor, context, reinterpret_cast<jlong>(this));
    transport_java_ref_ = env->NewGlobalRef(transport_local_ref);
  }

  ~AndroidRtcTransport() override {
    JNIEnv* env = Attach();
    if (env && transport_java_ref_) {
      env->DeleteGlobalRef(transport_java_ref_);
    }
  }

  // --- Implementation of Transport::TransportInterface ---

  void setDelegate(TransportDelegate* delegate) override { delegate_ = delegate; }
  TransportDelegate* delegate() { return delegate_; }

  void setStatusCallback(const Transport::StatusCallbackFunction& callback) override {
    // Status is managed by the Kotlin layer and propagated through the ViewModel.
  }

  void setName(const std::string& device_name) override {
    // Not needed for the Android implementation.
  }

  void setId(const std::string& id) override {
    // Not needed for the Android implementation.
  }

  void start() override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) return;

    jclass transportClass = env->GetObjectClass(transport_java_ref_);
    jmethodID startMethod = env->GetMethodID(transportClass, "connect", "()V");
    env->CallVoidMethod(transport_java_ref_, startMethod);
  }

  void terminate() override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) return;

    jclass transportClass = env->GetObjectClass(transport_java_ref_);
    jmethodID terminateMethod = env->GetMethodID(transportClass, "terminate", "()V");
    env->CallVoidMethod(transport_java_ref_, terminateMethod);
  }

  void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) override {
    // Peer management is handled by the Kotlin implementation.
  }

  auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> override {
    return nullptr; // Peer objects are not stored on the C++ side for Android.
  }

  auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> override {
    return {};
  }

  auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) return eillance::transport::PeerConnectionState::Closed;

    jclass transportClass = env->GetObjectClass(transport_java_ref_);
    jmethodID getPeerConnectionStateMethod = env->GetMethodID(transportClass, "getPeerConnectionState", "(Ljava/lang/String;)I");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jint state_val = env->CallIntMethod(transport_java_ref_, getPeerConnectionStateMethod, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<eillance::transport::PeerConnectionState>(state_val);
  }

  void removePeer(const std::string& peer_id) override {
    // Peer management is handled by the Kotlin implementation.
  }

  void sendSignalingMessage(const Transport::Json& message) const override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) return;
    jclass transportClass = env->GetObjectClass(transport_java_ref_);
    jmethodID sendMethod = env->GetMethodID(transportClass, "sendSignalingMessage", "(Ljava/lang/String;)V");

    std::string msg_str = message.dump();
    jstring j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transport_java_ref_, sendMethod, j_msg);
    env->DeleteLocalRef(j_msg);
  }

  void sendPeerDataChannelMessage(const std::string& peer_id, const Transport::Json& message) const override {
    // Direct data channel messages from C++ are not used in the Android impl.
  }

  void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override {}

  void createDataPeerConnection(const std::string& peer_id) override {
    createPeerConnection(peer_id, "data");
  }

  void createMediaPeerConnection(const std::string& peer_id) override {
    createPeerConnection(peer_id, "media");
  }

  void createDataChannel(const std::string& peer_id, std::function<void(const std::string&)> on_message) override {}
  void closePeerConnection(const std::string& peer_id, const std::string& context) override {}
  void setLocalDescription(const std::string& peer_id, const std::string& context) override {}
  void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {}
  void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {}
  auto addVideo(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return nullptr; }
  auto addAudio(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return nullptr; }

private:
  void createPeerConnection(const std::string& peer_id, const std::string& context) {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) return;
    jclass transportClass = env->GetObjectClass(transport_java_ref_);
    jmethodID createPcMethod = env->GetMethodID(transportClass, "createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");

    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, createPcMethod, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }
};

} // namespace eillance::transport

// --- JNI Callbacks ---

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      nlohmann::json json_msg = nlohmann::json::parse(msg_chars);
      transport->delegate()->processPeerMessage(json_msg);
    } catch (const std::exception& e) {
      LOGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSystemMessage(JNIEnv* env, jobject, jlong native_ptr, jstring type, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* type_chars = env->GetStringUTFChars(type, nullptr);
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      nlohmann::json json_msg = nlohmann::json::parse(msg_chars);
      transport->delegate()->processSystemMessage(type_chars, json_msg);
    } catch (const std::exception& e) {
      LOGE << "Failed to parse system message: " << e.what();
    }
    env->ReleaseStringUTFChars(type, type_chars);
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerOnline(JNIEnv* env, jobject, jlong native_ptr, jstring peer_id) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    transport->delegate()->onPeerOnline(peer_id_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerOffline(JNIEnv* env, jobject, jlong native_ptr, jstring peer_id) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    transport->delegate()->onPeerOffline(peer_id_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(JNIEnv *env, jobject, jlong native_ptr, jstring peer_id, jstring error) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    const char* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->delegate()->onSignalingErrorForPeer(peer_id_chars, error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}
