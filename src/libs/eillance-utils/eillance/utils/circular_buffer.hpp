#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#undef min
#undef max

namespace eillance {

template <typename T, std::size_t Capacity>
class CircularBuffer {
 public:
  CircularBuffer() = default;

  [[nodiscard]] auto IsEmpty() const { return size_ == 0; }

  [[nodiscard]] auto IsFull() const { return size_ == Capacity; }

  [[nodiscard]] auto Size() const { return size_; }

  void Clear() { head_ = tail_ = size_ = 0; }

  void Push(T value) {
    if (IsFull()) {
      Pop();
    }
    buffer_[tail_] = value;
    tail_ = (tail_ + 1) % Capacity;
    size_ = std::min(size_ + 1, Capacity);
  }

  auto Pop() -> T {
    assert(!IsEmpty() && "CircularBuffer is empty");
    T value = buffer_[head_];
    head_ = (head_ + 1) % Capacity;
    size_ = std::max(size_ - 1, static_cast<std::size_t>(0));
    return value;
  }

 private:
  std::array<T, Capacity> buffer_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};
};

}  // namespace eillance
