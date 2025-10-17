#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#undef min
#undef max

namespace hela::eillance {

template<typename T, std::size_t Capacity>
class CircularBuffer {
public:
  CircularBuffer() : head_(0), tail_(0), size_(0) {}

  [[nodiscard]] auto isEmpty() const {
    return size_ == 0;
  }

  [[nodiscard]] auto isFull() const {
    return size_ == Capacity;
  }

  [[nodiscard]] auto size() const {
    return size_;
  }

  void clear() {
    head_ = tail_ = size_ = 0;
  }

  void push(T value) {
    if (isFull()) {
      pop();
    }
    buffer_[tail_] = value;
    tail_ = (tail_ + 1) % Capacity;
    size_ = std::min(size_ + 1, Capacity);
  }

  T pop() {
    assert(!isEmpty() && "CircularBuffer is empty");
    T value = buffer_[head_];
    head_ = (head_ + 1) % Capacity;
    size_ = std::max(size_ - 1, static_cast<std::size_t>(0));
    return value;
  }

private:
  std::array<T, Capacity> buffer_;
  std::size_t head_;
  std::size_t tail_;
  std::size_t size_;
};

}  // namespace hela::eillance
