#pragma once
#include <stddef.h>
#include "util/BufferCache.hpp"
#include "util/config.hpp"

#include "util/EventDefine.hpp"

namespace AsyncLoad {

AL_EVENT_EXPORT(void, premultiplyAlpha, (
    void* AL_RESTRICT destination,
    const void* AL_RESTRICT source,
    size_t byteCount
), (destination, source, byteCount));

AL_EVENT_EXPORT(void, premultiplyAlphaInplace, (void* buffer, size_t byteCount), (buffer, byteCount));

AL_EVENT_EXPORT(void, widenRGBtoRGBA, (
    void* AL_RESTRICT destination,
    const void* AL_RESTRICT source,
    size_t pixelCount
), (destination, source, pixelCount));

struct AL_DLL RawImage {
    /// Raw byte vector containing the image pixels.
    CachedBuffer data;
    uint32_t width = 0;
    uint32_t height = 0;
    /// Whether the image has an alpha channel (RGB vs RGBA)
    bool hasAlpha = false;
    /// Whether the image has already been premultiplied. For example, on iOS loaded .png files are often premultiplied by default (CgBI)
    bool premultiplied = false;

    size_t sizeBytes() const {
        return width * height * (3 + (size_t)hasAlpha);
    }

    /// If the image has an alpha channel and has not been premultiplied, this will premultiply the alpha into the RGB channels.
    /// Otherwise, if this is an RGB image, this converts the image into RGBA with an alpha channel of 255 for all pixels.
    /// Does nothing when the image has an alpha channel and is already premultiplied.
    AL_EVENT_EXPORT_MEMBER(RawImage, void, premultiply, (), (this));

    /// Decodes an image in a format like PNG, WEBP, etc.
    /// Only PNG is guaranteed to be supported by default.
    /// If the ImagePlus mod is available, more formats are automatically available, and the decoding is more efficient overall.
    AL_EVENT_EXPORT_MEMBER_STATIC(geode::Result<RawImage>, create, (std::span<const uint8_t> data), (data));
    /// Converts a CCImage into a RawImage, which will copy all the data.
    AL_EVENT_EXPORT_MEMBER_STATIC(geode::Result<RawImage>, create, (cocos2d::CCImage* image, bool takeOwnership = false), (image, takeOwnership));
};

}
