#include <AsyncLoad/Images.hpp>
#include <asp/simd.hpp>
#include <prevter.imageplus/include/events.hpp>

#ifdef GEODE_IS_WINDOWS
# include <immintrin.h>
// apparently, some people are able to run this game on cpus from 2010 that do not support ssse3
static bool g_ssse3 = asp::simd::getFeatures().ssse3;
#endif

using namespace geode::prelude;

namespace AsyncLoad {

static void premultiplyIntoScalar(const void* source, void* dest, size_t bytes) {
    size_t pixels = bytes / 4;
    auto src = static_cast<const uint8_t*>(source);
    auto dst = static_cast<uint8_t*>(dest);

    for (size_t i = 0; i < pixels; i++) {
        uint8_t r = src[0];
        uint8_t g = src[1];
        uint8_t b = src[2];
        uint8_t a = src[3];

        dst[0] = (r * a) / 255;
        dst[1] = (g * a) / 255;
        dst[2] = (b * a) / 255;
        dst[3] = a;

        src += 4;
        dst += 4;
    }
}

static void widenRGBtoRGBAScalar(const void* AL_RESTRICT source, void* AL_RESTRICT dest, size_t pixels) {
    auto src = static_cast<const uint8_t*>(source);
    auto dst = static_cast<uint8_t*>(dest);

    for (size_t i = 0; i < pixels; i++) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;

        src += 3;
        dst += 4;
    }
}

#ifdef GEODE_IS_WINDOWS
static __attribute__((target("ssse3")))
void premultiplyIntoSSSE3(const void* source, void* dest, size_t bytes) {
    size_t const max_simd_pixel = bytes / sizeof(__m128i) * sizeof(__m128i);

    __m128i const mask_alphha_color_odd_255 = _mm_set1_epi32(static_cast<int>(0xff000000));
    __m128i const div_255 = _mm_set1_epi16(static_cast<short>(0x8081));

    __m128i const mask_shuffle_alpha = _mm_set_epi32(0x0f800f80, 0x0b800b80, 0x07800780, 0x03800380);
    __m128i const mask_shuffle_color_odd = _mm_set_epi32(static_cast<int>(0x80800d80), static_cast<int>(0x80800980), static_cast<int>(0x80800580), static_cast<int>(0x80800180));

    const __m128i* src = reinterpret_cast<const __m128i*>(source);
    __m128i* dst = reinterpret_cast<__m128i*>(dest);
    __m128i color, alpha, color_even, color_odd;

    for (size_t i = 0; i < max_simd_pixel; i += sizeof(__m128i)) {
        color = _mm_loadu_si128(src);

        alpha = _mm_shuffle_epi8(color, mask_shuffle_alpha);

        color_even = _mm_slli_epi16(color, 8);
        color_odd = _mm_shuffle_epi8(color, mask_shuffle_color_odd);
        color_odd = _mm_or_si128(color_odd, mask_alphha_color_odd_255);
//            color_odd = _mm_blendv_epi8(color, _mm_set_epi32(0xff000000, 0xff000000, 0xff000000, 0xff000000), _mm_set_epi32(0x80800080, 0x80800080, 0x80800080, 0x80800080));

        color_odd = _mm_mulhi_epu16(color_odd, alpha);
        color_even = _mm_mulhi_epu16(color_even, alpha);

        color_odd = _mm_srli_epi16(_mm_mulhi_epu16(color_odd, div_255), 7);
        color_even = _mm_srli_epi16(_mm_mulhi_epu16(color_even, div_255), 7);

        color = _mm_or_si128(color_even, _mm_slli_epi16(color_odd, 8));

        _mm_storeu_si128(dst, color);

        src++;
        dst++;
    }

    size_t remBytes = bytes - max_simd_pixel;
    premultiplyIntoScalar(
        static_cast<const uint8_t*>(source) + max_simd_pixel,
        static_cast<uint8_t*>(dest) + max_simd_pixel,
        remBytes
    );
}

static __attribute__((target("ssse3")))
void widenRGBtoRGBA_SSSE3(const void* AL_RESTRICT src_, void* AL_RESTRICT dst_, size_t numPixels) {
    auto src = static_cast<const uint8_t*>(src_);
    auto dst = static_cast<uint8_t*>(dst_);

    // with ssse3 instructions we can process up to 4 pixels at a time which is 12 input bytes,
    // however we are reading a 16-byte long lane, so we need there to be at least 4 bytes (2 pixels) left to read afterwards
    size_t simdPixels = (numPixels >= 6) ? ((numPixels - 2) / 4 * 4) : 0;

    __m128i shuffle_mask = _mm_set_epi8(
        0x80, 11, 10, 9,  // P3: A, B, G, R
        0x80, 8, 7, 6,      // P2: A, B, G, R
        0x80, 5, 4, 3,        // P1: A, B, G, R
        0x80, 2, 1, 0         // P0: A, B, G, R
    );

    __m128i alpha_filler = _mm_set_epi32(0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);

    size_t src_idx = 0;
    size_t dst_idx = 0;

    for (size_t i = 0; i < simdPixels; i += 4) {
        __m128i rgb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + src_idx));
        __m128i rgba = _mm_shuffle_epi8(rgb, shuffle_mask);
        rgba = _mm_or_si128(rgba, alpha_filler);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + dst_idx), rgba);
        src_idx += 12;
        dst_idx += 16;
    }

    size_t remainingPixels = numPixels - simdPixels;
    if (remainingPixels > 0) {
        widenRGBtoRGBAScalar(src + src_idx, dst + dst_idx, remainingPixels);
    }
}
#endif

void premultiplyInto(const void* source, void* dest, size_t bytes) {
#ifdef GEODE_IS_WINDOWS
    if (g_ssse3) {
        premultiplyIntoSSSE3(source, dest, bytes);
        return;
    }
#endif
    premultiplyIntoScalar(source, dest, bytes);
}

// insert restrict here but in reality premultiplyInto does not care in the current impl
void premultiplyAlpha(void* AL_RESTRICT destination, const void* AL_RESTRICT source, size_t byteCount) {
    premultiplyInto(source, destination, byteCount);
}

void premultiplyAlphaInplace(void* buffer, size_t byteCount) {
    premultiplyInto(buffer, buffer, byteCount);
}

void widenRGBtoRGBA(void* AL_RESTRICT destination, const void* AL_RESTRICT source, size_t pixelCount) {
#ifdef GEODE_IS_WINDOWS
    if (g_ssse3) {
        widenRGBtoRGBA_SSSE3(source, destination, pixelCount);
        return;
    }
#endif
    widenRGBtoRGBAScalar(source, destination, pixelCount);
}

void RawImage::premultiply() {
    if (premultiplied) {
        return;
    }

    if (hasAlpha) {
        premultiplyAlphaInplace(this->data.data(), this->sizeBytes());
        premultiplied = true;
        return;
    }

    auto newSize = this->width * this->height * 4;
    auto newData = BufferCache::get().get(newSize);
    widenRGBtoRGBA(newData.data(), this->data.data(), this->width * this->height);
    this->data = std::move(newData);
    this->hasAlpha = true;
    this->premultiplied = true;
}

Result<RawImage> RawImage::create(cocos2d::CCImage* image, bool takeOwneship) {
    uint64_t w = image->m_nWidth;
    uint64_t h = image->m_nHeight;
    bool alpha = image->m_bHasAlpha;
    bool premultiplied = image->m_bPreMulti;
    uint64_t byteSize = w * h * (3 + (uint64_t)alpha);

    CachedBuffer buf;
    if (takeOwneship) {
        auto ptr = std::unique_ptr<uint8_t[]>(image->m_pData);
        buf = BufferCache::get().registerNew(std::move(ptr), byteSize);
        image->m_pData = nullptr;
    } else {
        buf = BufferCache::get().get(byteSize);
        std::memcpy(buf.data(), image->m_pData, byteSize);
    }

    return Ok(RawImage {
        .data = std::move(buf),
        .width = (uint32_t)w,
        .height = (uint32_t)h,
        .hasAlpha = alpha,
        .premultiplied = premultiplied,
    });
}

static bool isCGBI(std::span<const uint8_t> data) {
    // Header of a CgBI image must match:
    // 89 50 4E 47 0D 0A 1A 0A (PNG signature)
    // ?? ?? ?? ?? (chunk length)
    // 43 67 42 49 (chunk type, "CgBI")
    // ...

    uint8_t pngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    uint8_t cgbiSignature[4] = { 0x43, 0x67, 0x42, 0x49 }; // 'CgBI'

    if (data.size() < 16) {
        return false;
    }

    if (std::memcmp(data.data(), pngSignature, 8) != 0) {
        return false;
    }

    if (std::memcmp(data.data() + 12, cgbiSignature, 4) != 0) {
        return false;
    }

    return true;
}

static bool useImagePlus(std::span<const uint8_t> data) {
    if (!imgp::isAvailable()) return false;

#ifdef __APPLE__
    if (isCGBI(data)) {
        // CgBI is a proprietary Apple format used for game resources on iOS,
        // it features premultiplied RGBA streams that cannot be decoded by libpng/libspng which are used in ImagePlus.
        // thus we must use CCImage for them.

        // TODO: in 2.209 (maybe sooner?), ImagePlus is planning to release CgBI support, so this can be removed.
        return false;
    }
#endif

    return true;
}

Result<RawImage> RawImage::create(std::span<const uint8_t> data) {
    if (!useImagePlus(data)) {
        // no imageplus, just use ccimage
        auto img = Ref<CCImage>::adopt(new CCImage());
        if (!img->initWithImageData((void*)data.data(), data.size())) {
            return Err("initWithImageData failed");
        }

        return RawImage::create(img, true);
    }

    // imageplus is available!!
    auto parsed = GEODE_UNWRAP(imgp::tryDecode(data.data(), data.size()));
    auto img = std::get_if<imgp::DecodedImage>(&parsed);

    if (!img) {
        return Err("animations are not supported in RawImage");
    }
    if (img->bit_depth != 8) {
        return Err("only 8-bit images are supported in RawImage");
    }

    auto byteSize = (uint64_t)img->width * img->height * (img->hasAlpha ? 4 : 3);
    auto buf = BufferCache::get().registerNew(std::move(img->data), byteSize);

    return Ok(RawImage {
        .data = std::move(buf),
        .width = img->width,
        .height = img->height,
        .hasAlpha = img->hasAlpha,
    });
}

}