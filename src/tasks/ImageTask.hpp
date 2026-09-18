#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>

namespace AsyncLoad {

struct ImageTask final : TypedTask<RawImage> {
    enum class State : uint8_t {
        /// We have the path and we are about to read the file from the disk.
        PreImageRead,
        /// We have the data of the image file, and we are about to decode it from a format like PNG or WEBP into raw pixels.
        ImageRead,
        /// Image has been fully decoded and a raw image is available. A texture may now be created with this.
        ImageReady,

        Failed,
    };

    asp::BoxedString m_path;
    CachedBufferChunk m_imageData; // bytes in an arbitrary image format
    State m_state;
    bool m_pathIsFull;

    ImageTask(ImageLoadParams&& params, std::shared_ptr<Control> ctl);

    // bool finished() const override;
    // void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;

    void fail(std::string message);

    std::string_view typeName() const override {
        return "ImageTask";
    }
};

}
