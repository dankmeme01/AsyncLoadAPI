#pragma once
#include <Geode/Result.hpp>

struct spng_ctx;

namespace AsyncLoad {

enum class PngDecodeMode {
    /// Only handles metadata
    Metadata,
    /// Allows row-by-row decoding of non-interlaced images
    Streaming,
    /// Allows one-shot decoding of any png image
    Whole,
};

/// Streaming PNG decoder
class PngDecoder {
public:
    /// Creates a decoder. The data is expected to live as long as the decoder.
    static geode::Result<PngDecoder> create(std::span<const uint8_t> data, PngDecodeMode mode = PngDecodeMode::Whole);
    ~PngDecoder();

    PngDecoder(const PngDecoder&) = delete;
    PngDecoder& operator=(const PngDecoder&) = delete;
    PngDecoder(PngDecoder&&) noexcept;
    PngDecoder& operator=(PngDecoder&&) noexcept;

    uint32_t width() const {
        return m_width;
    }

    uint32_t height() const {
        return m_height;
    }

    uint64_t totalSize() const {
        return m_totalSize;
    }

    geode::Result<> decodeRow(uint8_t* out, size_t outSize);
    geode::Result<> decode(uint8_t* out, size_t outSize);

private:
    spng_ctx* m_ctx = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_totalSize = 0;

    uint32_t m_curRow = 0;

    PngDecoder(struct spng_ctx* ctx) : m_ctx(ctx) {}
};

}