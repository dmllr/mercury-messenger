#pragma once

#include <memory>

template <class T> auto make_weak_ptr(std::shared_ptr<T> ptr) -> std::weak_ptr<T> { return ptr; }
