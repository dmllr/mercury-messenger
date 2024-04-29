#pragma once

#include <array>
#include <algorithm>
#include <stdexcept>

template<typename T, size_t N>
class CircularBuffer {
public:
  CircularBuffer() : head_(0), size_(0) {}

  void push_back(const T& item) {
    if (full())
      pop_front();

    data_[(head_ + size_) % N] = item;
    if (size_ < N)
      ++size_;
  }

  void pop_front() {
    if (empty())
      throw std::out_of_range("Buffer is empty");

    head_ = (head_ + 1) % N;
    --size_;
  }

  bool empty() const {
    return size_ == 0;
  }

  bool full() const {
    return size_ == N;
  }

  size_t size() const {
    return size_;
  }

  const T& operator[](size_t index) const {
    if (index >= size_)
      throw std::out_of_range("Index out of range");
    return data_[(head_ + index) % N];
  }

  T& operator[](size_t index) {
    if (index >= size_)
      throw std::out_of_range("Index out of range");
    return data_[(head_ + index) % N];
  }

private:
  std::array<T, N> data_;
  size_t head_;
  size_t size_;
};
