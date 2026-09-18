#pragma once
#include <atomic>

namespace AsyncLoad {

template <typename T>
struct Atomic : std::atomic<T> {
    Atomic() : std::atomic<T>() {}
    Atomic(T value) : std::atomic<T>(value) {}

    Atomic(const Atomic& other) : std::atomic<T>(other.load(std::memory_order::acquire)) {}

    Atomic& operator=(const Atomic& other) {
        if (this != &other) {
            this->store(other.load(std::memory_order::acquire), std::memory_order::release);
        }
        return *this;
    }

    Atomic(Atomic&& other) noexcept : std::atomic<T>(other.load(std::memory_order::acquire)) {}

    Atomic& operator=(Atomic&& other) noexcept {
        if (this != &other) {
            this->store(other.load(std::memory_order::acquire), std::memory_order::release);
        }
        return *this;
    }
};


}
