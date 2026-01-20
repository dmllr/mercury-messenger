#pragma once

#include <memory>

template<class T>
auto MakeWeakPtr(std::shared_ptr<T> ptr) -> std::weak_ptr<T> { return ptr; }
