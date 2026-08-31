#pragma once

#include <memory>

namespace ic2d {

class GpuBackdrop final {
public:
    GpuBackdrop();
    ~GpuBackdrop();

    GpuBackdrop(const GpuBackdrop&) = delete;
    GpuBackdrop& operator=(const GpuBackdrop&) = delete;

    [[nodiscard]] bool available() const noexcept;
    void draw(int width, int height) const;
    void release() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ic2d
