// Stub HalideBuffer.h — minimal Halide::Runtime::Buffer placeholder.
// Allows files that include <HalideBuffer.h> to compile without the real Halide SDK.
#pragma once
#include <cstddef>
#include <cstdint>

namespace Halide {
namespace Runtime {

template<typename T>
class Buffer {
public:
    Buffer() = default;
    Buffer(int /*w*/, int /*h*/) {}
    Buffer(T* /*data*/, int /*w*/, int /*h*/, int /*stride*/ = 0) {}

    T* data() const { return nullptr; }
    int width()    const { return 0; }
    int height()   const { return 0; }
    int channels() const { return 0; }
    bool defined() const { return false; }
    void set_host_dirty(bool = true) {}
    void copy_to_host() {}
    int dimensions() const { return 0; }

    T& operator()(int, int) const { static T dummy{}; return dummy; }
    T& operator()(int, int, int) const { static T dummy{}; return dummy; }
};

} // namespace Runtime
} // namespace Halide
