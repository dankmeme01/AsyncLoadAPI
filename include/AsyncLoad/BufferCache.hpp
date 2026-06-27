#pragma once
#include "assert.hpp"

namespace AsyncLoad {

struct CachedBuffer {
    uint8_t* data() const {
        return m_data.get();
    }
    size_t capacity() const {
        return m_size;
    }
    std::span<uint8_t> span() const {
        return std::span<uint8_t>(data(), capacity());
    }

    CachedBuffer() = default;
    CachedBuffer(CachedBuffer&&) = default;
    CachedBuffer& operator=(CachedBuffer&&) = default;
    ~CachedBuffer();

    /// no one else should be able to create buffers
#ifdef AsyncLoad_EXPORTS
    CachedBuffer(size_t size) : m_data(std::make_unique<uint8_t[]>(size)), m_size(size) {}
    CachedBuffer(std::unique_ptr<uint8_t[]> data, size_t size) : m_data(std::move(data)), m_size(size) {}
#endif

private:
    friend class BufferCache;
    std::unique_ptr<uint8_t[]> m_data;
    size_t m_size;
};

struct CachedBufferChunk {
    uint8_t* data() const {
        return m_buf.data();
    }
    size_t size() const {
        return m_size;
    }
    size_t capacity() const {
        return m_buf.capacity();
    }
    bool empty() const {
        return size() == 0;
    }
    std::span<uint8_t> span() const {
        return std::span<uint8_t>(data(), size());
    }
    std::span<uint8_t> allocationSpan() const {
        return std::span<uint8_t>(data(), capacity());
    }
    void setSize(size_t size) {
        AL_ASSERT(size <= m_buf.capacity());
        m_size = size;
    }
    CachedBuffer& buffer() {
        return m_buf;
    }

    CachedBufferChunk() = default;
    CachedBufferChunk(CachedBuffer buf, size_t size) : m_buf(std::move(buf)), m_size(size) {}
    CachedBufferChunk(CachedBufferChunk&&) = default;
    CachedBufferChunk& operator=(CachedBufferChunk&&) = default;

private:
    CachedBuffer m_buf;
    size_t m_size = 0;
};

class BufferCache {
public:
    BufferCache(const BufferCache&) = delete;
    BufferCache& operator=(const BufferCache&) = delete;
    BufferCache(BufferCache&&) = delete;
    BufferCache& operator=(BufferCache&&) = delete;

    static BufferCache& get();

    /// Gets a buffer that is at least `size` bytes in size. Once you are done with it, you can return it via `put`, or it will happen automatically.
    /// If the pool does not have a buffer of the appropriate size, one will be created.
    /// Note: the returned buffer should not be treated as a growable vector, it has a fixed capacity that might not be the passed size.
    /// If you do not want to separately store the size of written data, use `getSized`.
    CachedBuffer get(size_t size);

    /// Returns a CachedBufferChunk that can be treated as a regular vector<uint8_t> with a size.
    CachedBufferChunk getSized(size_t size);

    /// Returns a CachedBufferChunk with the given contents copied to it.
    CachedBufferChunk getSized(std::span<const uint8_t> data);
    CachedBufferChunk getSized(const uint8_t* data, size_t size);

    /// Returns a buffer to the cache. The buffer must have been obtained from `get()`.
    /// If you want to share a self-allocated buffer with the pool after not needing it anymore,
    /// use `putNew`.
    void put(CachedBuffer buffer);

    /// Puts a new buffer into the pool. This should be used instead of constructing a `CachedBuffer` and calling `put` manually,
    /// since it lets the pool know that the buffer is new and updates certain stats inside the pool.
    void putNew(std::unique_ptr<uint8_t[]> data, size_t size);

    /// Registers a buffer with the pool, similar to `putNew`, but returning the buffer directly to you instead of keeping it in the pool.
    /// This is useful for when you have you own buffer that you want to later give away to the pool, but you want to use it for a while first.
    CachedBuffer registerNew(std::unique_ptr<uint8_t[]> data, size_t size);

private:
    asp::Mutex<std::vector<CachedBuffer>> m_cache;
    std::atomic<size_t> m_recentRequests{0};
    std::atomic<size_t> m_largestRequest{0};
    std::atomic<size_t> m_totalUsage{0};

    BufferCache();
};

}
