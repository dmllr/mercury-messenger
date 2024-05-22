#pragma once

#include <type_traits>

#include "hela/eillance/capture/video_config.hpp"

namespace hela::eillance {

// Trait to detect the presence of ConfigureVideo method
template <typename, typename = std::void_t<>>
struct HasConfigureVideo : std::false_type {};

template <typename T>
struct HasConfigureVideo<T, std::void_t<decltype(std::declval<T>().ConfigureVideo(std::declval<VideoConfig>()))>> : std::true_type {};

// Function to invoke ConfigureVideo if it exists
template <typename T>
__attribute__((unused))
std::enable_if_t<HasConfigureVideo<T>::value, void>
InvokeConfigureVideo(T& obj, const VideoConfig& config) {
  obj.ConfigureVideo(config);
}

template <typename T>
__attribute__((unused))
std::enable_if_t<!HasConfigureVideo<T>::value, void>
InvokeConfigureVideo(T&, const VideoConfig&) {
  // Do nothing if ConfigureVideo is not defined
}

}  // namespace hela::eillance