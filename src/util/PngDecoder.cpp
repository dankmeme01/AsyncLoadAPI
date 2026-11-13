#include "PngDecoder.hpp"
#include <spng.h>

using namespace geode::prelude;

namespace AsyncLoad {

Result<PngDecoder> PngDecoder::create(std::span<const uint8_t> data, PngDecodeMode mode) {
    auto ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32);
    if (!ctx) {
        return Err("Failed to create SPNG context");
    }

    PngDecoder out{ctx};
    if (spng_set_png_buffer(ctx, data.data(), data.size()) != 0) {
        return Err("Failed to set PNG buffer");
    }

    spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
        return Err("Failed to get PNG IHDR");
    }

    if (mode == PngDecodeMode::Streaming && ihdr.interlace_method != 0) {
        return Err("Interlaced PNGs are not supported for streaming");
    }

    out.m_width = ihdr.width;
    out.m_height = ihdr.height;

    size_t totalSize = 0;
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &totalSize) != 0) {
        return Err("Failed to get decoded image size");
    }

    out.m_totalSize = totalSize;

    if (mode == PngDecodeMode::Streaming) {
        if (spng_decode_image(ctx, nullptr, 0, SPNG_FMT_RGBA8, SPNG_DECODE_PROGRESSIVE | SPNG_DECODE_TRNS) != 0) {
            return Err("Failed to start decoding PNG");
        }
    }

    return Ok(std::move(out));
}

PngDecoder::PngDecoder(PngDecoder&& other) noexcept {
    *this = std::move(other);
}

PngDecoder& PngDecoder::operator=(PngDecoder&& other) noexcept {
    if (this != &other) {
        if (m_ctx) {
            spng_ctx_free(m_ctx);
        }
        m_ctx = std::exchange(other.m_ctx, nullptr);
        m_width = other.m_width;
        m_height = other.m_height;
        m_totalSize = other.m_totalSize;
        m_curRow = other.m_curRow;
    }
    return *this;
}

PngDecoder::~PngDecoder() {
    if (m_ctx) {
        spng_ctx_free(m_ctx);
    }
}

Result<> PngDecoder::decodeRow(uint8_t* out, size_t outSize) {
    size_t byteWidth = m_totalSize / m_height;
    if (outSize < byteWidth) {
        return Err("Output buffer is too small for a row");
    }

    int e = spng_decode_row(m_ctx, out, byteWidth);
    if (e != 0 && e != SPNG_EOI) {
        return Err(spng_strerror(e));
    }

    m_curRow++;

    return Ok();
}

Result<> PngDecoder::decode(uint8_t* out, size_t outSize) {
    if (outSize < m_totalSize) {
        return Err("Output buffer is too small for the entire image");
    }

    int e = spng_decode_image(m_ctx, out, outSize, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS);
    if (e != 0) {
        return Err(spng_strerror(e));
    }

    return Ok();
}

}