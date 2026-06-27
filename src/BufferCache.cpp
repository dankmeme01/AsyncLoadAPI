#include <AsyncLoad/BufferCache.hpp>
#include <AsyncLoad/Util.hpp>
#include <arc/prelude.hpp>
#include <bit>

using namespace geode::prelude;

static constexpr size_t AGGRESSIVE_CLEANUP_THRESHOLD = 32 * 1024 * 1024;
static constexpr size_t PARTIAL_CLEANUP_THRESHOLD = 8 * 1024 * 1024;

namespace AsyncLoad {

CachedBuffer::~CachedBuffer() {
    if (!m_data) {
        // data is null, this means the buffer is intentionally being deleted
        return;
    }

    // we have data, this means the buffer was dropped by the user and should be returned to the pool
    BufferCache::get().put(CachedBuffer{ std::move(m_data), m_size });
}

BufferCache::BufferCache() {
    async::spawn([this] -> arc::Future<> {
        std::deque<std::pair<size_t, size_t>> measurements;
        std::vector<CachedBuffer> deallocateQueue;

        static constexpr uint32_t MEASUREMENT_INTERVAL = 5;
        static constexpr uint32_t RETENTION_INTERVAL = 180;

        auto largestOverLast = [&measurements](size_t seconds) -> size_t {
            size_t keepBelow = 0;
            size_t toCheck = std::min<size_t>(measurements.size(), seconds / MEASUREMENT_INTERVAL);
            for (size_t i = 0; i < toCheck; ++i) {
                keepBelow = std::max(keepBelow, measurements[measurements.size() - 1 - i].second);
            }
            return keepBelow;
        };

        auto totalRequestOverLast = [&measurements](size_t seconds) -> size_t {
            size_t total = 0;
            size_t toCheck = std::min<size_t>(measurements.size(), seconds / MEASUREMENT_INTERVAL);
            for (size_t i = 0; i < toCheck; ++i) {
                total += measurements[measurements.size() - 1 - i].first;
            }
            return total;
        };

        while (true) {
            co_await arc::sleep(asp::Duration::fromSecs(MEASUREMENT_INTERVAL));

            auto requests = m_recentRequests.exchange(0, std::memory_order::relaxed);
            auto largest = m_largestRequest.exchange(0, std::memory_order::relaxed);
            measurements.emplace_back(requests, largest);

            if (measurements.size() >= RETENTION_INTERVAL / MEASUREMENT_INTERVAL) { // keep 3 minutes of data
                measurements.pop_front();
            }

            // now, this is where we do periodic buffer cleanup.
            // we take the accumulated measurements over the last 3 minutes and do a few specific things depending on the used memory
            size_t usedMemory = m_totalUsage.load(std::memory_order::relaxed);

            // check if request rate is < 1/s
            bool almostIdle5s = requests < 5;
            bool almostIdle15s = totalRequestOverLast(15) < 15;

            size_t keepBelow = 0;
            if (usedMemory >= AGGRESSIVE_CLEANUP_THRESHOLD || almostIdle15s) {
                keepBelow = largest; // remove everything that hasnt been used in 5 seconds
            } else if (usedMemory >= PARTIAL_CLEANUP_THRESHOLD || almostIdle5s) {
                // remove everything that hasnt been used in 30 seconds
                keepBelow = largestOverLast(30);
            } else {
                // remove everything that hasnt been used in the full 3 minutes
                keepBelow = largestOverLast(RETENTION_INTERVAL);
            }

            if (keepBelow == 0) {
                continue; // nothing to remove
            }

            size_t freedMemory = 0;
            {
                auto cache = m_cache.lock();
                auto it = std::upper_bound(cache->begin(), cache->end(), keepBelow, [](size_t target, const CachedBuffer& buf) {
                    return target < buf.m_size;
                });
                if (it == cache->end()) {
                    continue; // nothing to remove
                }

                for (auto eraseIt = it; eraseIt != cache->end(); ++eraseIt) {
                    freedMemory += eraseIt->m_size;
                    // move the buffer into a local vector instead of deleting there
                    deallocateQueue.push_back(std::move(*eraseIt));
                }

                cache->erase(it, cache->end());
                m_totalUsage.fetch_sub(freedMemory, std::memory_order::relaxed);
            }

            // now when we are not holding any locks, we can deallocate all buffers in the vector
            for (auto& b : deallocateQueue) {
                b.m_data.reset();
            }
            deallocateQueue.clear();

            AL_TRACE(
                "BufferCache: cleaned up {} bytes of buffers, keeping buffers <= {} bytes; memory usage now: {} bytes",
                freedMemory, keepBelow, m_totalUsage.load(std::memory_order::relaxed)
            );
        }
    });
}

CachedBuffer BufferCache::get(size_t size) {
    size_t bucketSize = std::max<size_t>(std::bit_ceil(size), 4096);

    // update measurements
    m_recentRequests.fetch_add(1, std::memory_order::relaxed);
    auto curLargest = m_largestRequest.load(std::memory_order::relaxed);
    while (
        curLargest < bucketSize &&
        !m_largestRequest.compare_exchange_weak(curLargest, bucketSize, std::memory_order::relaxed)
    ) {
        // nothing
    }

    // try to get the buffer
    auto cache = m_cache.lock();

    auto it = std::upper_bound(cache->begin(), cache->end(), bucketSize, [](size_t target, const CachedBuffer& buf) {
        return target < buf.m_size;
    });

    if (it != cache->end()) {
        AL_DEBUG_ASSERT(it->m_size >= size);

        if (it->m_size <= bucketSize * 2) {
            auto buffer = std::move(*it);
            cache->erase(it);
            return buffer;
        }
    }
    cache.unlock();

    m_totalUsage.fetch_add(bucketSize, std::memory_order::relaxed);
    return CachedBuffer{bucketSize};
}

void BufferCache::put(CachedBuffer buffer) {
    auto cache = m_cache.lock();
    auto it = std::lower_bound(cache->begin(), cache->end(), buffer.m_size, [](const CachedBuffer& a, size_t b) {
        return a.m_size < b;
    });
    cache->insert(it, std::move(buffer));
}

void BufferCache::putNew(std::unique_ptr<uint8_t[]> data, size_t size) {
    this->put(CachedBuffer{ std::move(data), size });
    m_totalUsage.fetch_add(size, std::memory_order::relaxed);
}

CachedBuffer BufferCache::registerNew(std::unique_ptr<uint8_t[]> data, size_t size) {
    m_totalUsage.fetch_add(size, std::memory_order::relaxed);
    return CachedBuffer{ std::move(data), size };
}

BufferCache& BufferCache::get() {
    static BufferCache instance;
    return instance;
}

}