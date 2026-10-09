#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// GameInput timestamps use one microsecond clock. Never subtract QPC from them.
inline std::optional<std::uint64_t> ElapsedMicroseconds(std::uint64_t beginning, std::uint64_t ending)
{
    if (ending < beginning) return std::nullopt;
    return ending - beginning;
}

// One producer and one consumer only. Full queues fail immediately rather than
// blocking a callback or delaying the latest input used for rendering.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line padding prevents false sharing.
#endif
template<class Value, std::size_t Capacity>
class SingleProducerQueue final
{
    static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0);
public:
    bool Push(const Value& value) noexcept
    {
        const auto write = writePosition.load(std::memory_order_relaxed);
        if (write - readPosition.load(std::memory_order_acquire) == Capacity) return false;
        values[write & (Capacity - 1)] = value;
        writePosition.store(write + 1, std::memory_order_release);
        return true;
    }
    bool Pop(Value& value) noexcept
    {
        const auto read = readPosition.load(std::memory_order_relaxed);
        if (read == writePosition.load(std::memory_order_acquire)) return false;
        value = values[read & (Capacity - 1)];
        readPosition.store(read + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<Value, Capacity> values{};
    alignas(64) std::atomic<std::uint64_t> writePosition{};
    alignas(64) std::atomic<std::uint64_t> readPosition{};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

struct StatisticSnapshot
{
    std::uint64_t count{};
    std::size_t percentileCount{};
    double last{};
    double minimum{};
    double mean{};
    double maximum{};
    double median{};
    double percentile95{};
    double percentile99{};
};

class Statistics final
{
public:
    static constexpr std::size_t WindowCapacity = 8192;
    void Add(std::uint64_t microseconds)
    {
        const double value = static_cast<double>(microseconds);
        last = value;
        if (count == 0) minimum = maximum = value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        ++count;
        mean += (value - mean) / static_cast<double>(count);
        if (window.size() < WindowCapacity) window.push_back(value);
        else window[(count - 1) % WindowCapacity] = value;
    }
    StatisticSnapshot Snapshot() const
    {
        if (count == 0) return {};
        auto sorted = window;
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&sorted](double probability) {
            const auto rank = static_cast<std::size_t>(std::ceil(probability * static_cast<double>(sorted.size())));
            return sorted[std::max<std::size_t>(1, rank) - 1];
        };
        return {count, window.size(), last, minimum, mean, maximum,
            percentile(0.5), percentile(0.95), percentile(0.99)};
    }
private:
    std::uint64_t count{};
    double last{}, minimum{}, mean{}, maximum{};
    std::vector<double> window;
};
