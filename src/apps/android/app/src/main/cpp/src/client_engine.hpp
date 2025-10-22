#pragma once

#include <jni.h>
#include <sys/system_properties.h>

#include <atomic>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <nlohmann/json.hpp>

#include "eillance/capture/video_config.hpp"
#include "eillance/logging/logging.h"
#include "eillance/rtc/client.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/h264.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/view_model/mercury_view_model.hpp"

#include "class_refs.hpp"
#include "android_persistent_storage.hpp"
#include "capture/video_capture_android.hpp"


namespace eillance {

class ClientEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};

  ANativeWindow* native_window_{nullptr};
  AMediaCodec* media_codec_{nullptr};
  std::atomic<bool> is_codec_configured_{false};

  std::shared_ptr<RtcClient> rtc_client_;
  std::unique_ptr<view_model::MercuryViewModel> view_model_;

  H264Parser h264_parser_{};

  DispatchQueue codec_dispatch_queue_{"Codec", 1};

public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject instance)
      : jvm_(jvm),
        native_bridge_(env->NewGlobalRef(instance))
  {
    auto storage = std::make_shared<storage::PersistentStorage>(
        std::make_unique<storage::AndroidPersistentStorageImpl>(jvm, instance)
    );
    auto video_capture = Capture(std::make_unique<VideoCaptureAndroid>());
    rtc_client_ = std::make_shared<RtcClient>(storage, std::move(video_capture));
    view_model_ = std::make_unique<view_model::MercuryViewModel>(rtc_client_);

    view_model_->SetOnStateChangedCallback([this]() {
      UpdateState();
    });

    EventQueue::Instance();

    std::string device_name;
    auto stored_name = storage->Load("device_name");
    if (stored_name.has_value() && !stored_name->empty()) {
      device_name.assign(reinterpret_cast<const char*>(stored_name->data()), stored_name->size());
    }

    if (device_name.empty()) {
      char model_string[PROP_VALUE_MAX + 1];
      __system_property_get("ro.product.model", model_string);
      device_name = std::string(model_string);
      const auto* data_ptr = reinterpret_cast<const std::byte*>(device_name.data());
      std::vector<std::byte> name_bytes(data_ptr, data_ptr + device_name.size());
      storage->Save("device_name", name_bytes);
    }
    rtc_client_->SetName(device_name);
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(const ClientEngine&) = delete;
  auto& operator=(ClientEngine&&) = delete;
  auto& operator=(const ClientEngine&) = delete;

  ~ClientEngine() {
    Terminate();

    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      LOGE << "Failed to get JNIEnv in destructor.";
      return;
    }

    if (native_bridge_ != nullptr && (env->IsSameObject(native_bridge_, nullptr) == JNI_FALSE)) {
      LOGD << "Deleting native_bridge_ GlobalRef.";
      env->DeleteGlobalRef(native_bridge_);
      native_bridge_ = nullptr;
    }
  }

  auto ViewModel() -> view_model::MercuryViewModel* {
    return view_model_.get();
  }

  void SetTargetSurface(JNIEnv* env, jobject surface) {
    if (native_window_ != nullptr) {
      ANativeWindow_release(native_window_);
    }
    native_window_ = ANativeWindow_fromSurface(env, surface);
    InitH264Parser();
  }

  void Start() {
    rtc_client_->Start();
    rtc_client_->SetFrameReceivedCallback([this](const rtc::binary& frame) {
      h264_parser_.ProcessFrame(frame);
      if (codec_dispatch_queue_.Size() > 10) {
        LOGW << "Decoding queue is full, dropping frames.";
        codec_dispatch_queue_.RemovePending();
      }
      codec_dispatch_queue_.Dispatch([this, frame]() {
        HandleInputBuffer(frame);
      });
    });
    LOGI << "Client engine is initialized and ready.";
  }

  void Stop() {
    is_codec_configured_ = false;
    if (media_codec_ != nullptr) {
      AMediaCodec_stop(media_codec_);
      AMediaCodec_delete(media_codec_);
      media_codec_ = nullptr;
    }
    if (native_window_ != nullptr) {
      ANativeWindow_release(native_window_);
      native_window_ = nullptr;
    }
    LOGI << "Decoder resources released.";
  }

  void Terminate() {
    Stop();
    rtc_client_->Terminate();
  }

  void Call(const std::string& peer_id) {
    rtc_client_->Call(peer_id);
  }

  void Hangup() {
    if (view_model_) {
      view_model_->Hangup();
    }
  }

  void ConfigureVideoCapture(const VideoConfig& config) {
    Config coreConfig;
    coreConfig.video_config = config;
    rtc_client_->Configure(coreConfig);
  }

private:
  JNIEnv* AttachCurrentThreadIfNeeded() {
    JNIEnv* env{};
    const jint res = jvm_->GetEnv((void**)&env, JNI_VERSION_1_6);
    switch (res) {
      case JNI_OK:
        return env;
      case JNI_EDETACHED:
        if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          return env;
        } else {
          return nullptr;
        }
      default:
        return nullptr;
    }
  }

  void ConfigureCodec(const VideoConfig& conf) {
    if (is_codec_configured_) {
      LOGI << "Codec already configured, skipping reconfiguration.";
      return;
    }

    AMediaFormat* format = AMediaFormat_new();
    if (format == nullptr) {
      LOGE << "Failed to create AMediaFormat.";
      return;
    }

    LOGI << "Configuring decoder with dimensions: " << conf.width << "x" << conf.height;

    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, conf.width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, conf.height);

    media_codec_ = AMediaCodec_createDecoderByType("video/avc");
    if (media_codec_ == nullptr) {
      LOGE << "Failed to create AMediaCodec.";
      AMediaFormat_delete(format);
      return;
    }

    if (AMediaCodec_configure(media_codec_, format, native_window_, nullptr, 0) != AMEDIA_OK) {
      LOGE << "AMediaCodec configuration failed.";
      AMediaCodec_delete(media_codec_);
      media_codec_ = nullptr;
    } else {
      AMediaCodec_start(media_codec_);
      is_codec_configured_ = true;
      LOGI << "MediaCodec successfully configured.";
    }

    AMediaFormat_delete(format);
  }

  void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
    if (!is_codec_configured_) {
      LOGW << "Codec not configured yet, dropping frame.";
      return;
    }

    constexpr auto timeout = 5000ULL;
    auto buf_index = AMediaCodec_dequeueInputBuffer(media_codec_, timeout);
    if (buf_index >= 0) {
      size_t buf_size = 0;
      auto *buf = AMediaCodec_getInputBuffer(media_codec_, buf_index, &buf_size);
      if (buf != nullptr) {
        const auto copy_size = std::min(frameBuffer.size(), buf_size);
        std::memcpy(buf, frameBuffer.data(), copy_size);
        AMediaCodec_queueInputBuffer(media_codec_, buf_index, 0, copy_size, 0, 0);
      }
    }

    AMediaCodecBufferInfo info;
    auto output_buffer_index = AMediaCodec_dequeueOutputBuffer(media_codec_, &info, 0);
    if (output_buffer_index >= 0) {
      AMediaCodec_releaseOutputBuffer(media_codec_, output_buffer_index, info.size > 0);
    }
  }

  void InitH264Parser() {
    h264_parser_.Reset();
    h264_parser_.SetOnConfigureCallback([this](const VideoConfig conf) {
      LOGI << "Configuration change callback triggered, configuring codec...";
      ConfigureCodec(conf);
    });
  }

  void UpdateState() {
    if (native_bridge_ == nullptr) return;
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStateUpdate", "(Ljava/lang/String;)V");
    if (callback_method != nullptr) {
      nlohmann::json state_json = view_model_->GetState();
      std::string state_str = state_json.dump();
      jstring j_state_str = env->NewStringUTF(state_str.c_str());
      env->CallVoidMethod(native_bridge_, callback_method, j_state_str);
      env->DeleteLocalRef(j_state_str);
    }
  }
};

} // namespace eillance
