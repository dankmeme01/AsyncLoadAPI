#pragma once
#include <stddef.h>
#include <AsyncLoad/BufferCache.hpp>
#include "config.hpp"

namespace AsyncLoad {

AL_DLL void premultiplyAlpha(
    void* AL_RESTRICT destination,
    const void* AL_RESTRICT source,
    size_t byteCount
);
AL_DLL void premultiplyAlphaInplace(void* buffer, size_t byteCount);
AL_DLL void widenRGBtoRGBA(
    void* AL_RESTRICT destination,
    const void* AL_RESTRICT source,
    size_t pixelCount
);

struct AL_DLL RawImage {
    /// Raw byte vector containing the image pixels.
    CachedBuffer data;
    bool hasAlpha = false;
    uint32_t width = 0;
    uint32_t height = 0;

    size_t sizeBytes() const {
        return width * height * (3 + (size_t)hasAlpha);
    }

    /// If the image has an alpha channel, this will premultiply the alpha into the RGB channels.
    /// Otherwise, this converts the image into RGBA.
    void premultiply();

    /// Decodes an image in a format like PNG, WEBP, etc.
    /// Only PNG is guaranteed to be supported by default.
    /// If the ImagePlus mod is available, more formats are automatically available, and the decoding is more efficient overall.
    static geode::Result<RawImage> create(std::span<const uint8_t> data);
    /// Converts a CCImage into a RawImage, which will copy all the data.
    static geode::Result<RawImage> create(cocos2d::CCImage* image, bool takeOwneship = false);
};

}
