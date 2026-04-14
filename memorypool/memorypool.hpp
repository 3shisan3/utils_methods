/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        memorypool.hpp
Version:     1.0
Author:      cjx
start date: 2024-12-31
Description: 高性能内存池实现，支持固定大小分配、对齐分配
             提供 STL 分配器适配器和统计信息
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1            2026-04-14       cjx           create
*****************************************************************/

#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// ============================================================================
// 调试宏
// ============================================================================

#ifdef MEMORY_POOL_DEBUG
#define POOL_ASSERT(cond, msg) assert((cond) && (msg))
#else
#define POOL_ASSERT(cond, msg) ((void)0)
#endif

// ============================================================================
// 内存池配置
// ============================================================================

struct MemoryPoolConfig
{
    size_t block_size = 64;                       // 每个块的大小（字节）
    size_t blocks_per_chunk = 1024;               // 每个内存块的块数量
    size_t max_blocks = 0;                        // 最大块数量（0 = 无限制）
    size_t expand_chunks = 1;                     // 每次扩展的块数
    size_t shrink_threshold_chunks = 2;           // 收缩阈值（空闲块数超过此值时可收缩）
    bool use_lock = true;                         // 是否使用线程安全模式
    size_t alignment = alignof(std::max_align_t); // 对齐要求
    bool enable_stats = true;                     // 是否启用统计
    bool enable_debug_checks = false;
};

// ============================================================================
// 内存池统计信息
// ============================================================================

struct MemoryPoolStats
{
    size_t block_size = 0;            // 块大小
    size_t total_blocks = 0;          // 总块数
    size_t allocated_blocks = 0;      // 已分配块数
    size_t free_blocks = 0;           // 空闲块数
    size_t peak_allocated = 0;        // 峰值分配数
    size_t total_allocations = 0;     // 总分配次数
    size_t total_deallocations = 0;   // 总释放次数
    size_t expansions = 0;            // 扩展次数
    size_t shrinks = 0;               // 收缩次数
    size_t total_chunks = 0;          // 总块数
    double utilization_rate = 0.0;    // 利用率
    double fragmentation_estimate = 0.0; // 碎片率估算
};

// ============================================================================
// 固定大小内存池
// ============================================================================

template <size_t BlockSize>
class FixedMemoryPool
{
public:
    static_assert(BlockSize >= sizeof(void *),
                  "BlockSize must be at least sizeof(void*)");

    explicit FixedMemoryPool(const MemoryPoolConfig &config)
        : m_blocks_per_chunk(config.blocks_per_chunk)
        , m_max_blocks(config.max_blocks)
        , m_use_lock(config.use_lock)
        , m_shrink_threshold_chunks(config.shrink_threshold_chunks)
        , m_enable_stats(config.enable_stats)
        , m_enable_debug_checks(config.enable_debug_checks)
    {
        expand(config.blocks_per_chunk);
    }

    explicit FixedMemoryPool(size_t blocks_per_chunk = 1024,
                             size_t max_blocks = 0,
                             bool use_lock = true)
        : m_blocks_per_chunk(blocks_per_chunk)
        , m_max_blocks(max_blocks)
        , m_use_lock(use_lock)
    {
        expand(blocks_per_chunk);
    }

    ~FixedMemoryPool()
    {
        for (auto &chunk : m_chunks)
        {
            ::operator delete(chunk.memory);
        }
    }

    FixedMemoryPool(FixedMemoryPool &&other) noexcept
        : m_blocks_per_chunk(other.m_blocks_per_chunk)
        , m_max_blocks(other.m_max_blocks)
        , m_use_lock(other.m_use_lock)
        , m_shrink_threshold_chunks(other.m_shrink_threshold_chunks)
        , m_enable_stats(other.m_enable_stats)
        , m_enable_debug_checks(other.m_enable_debug_checks)
        , m_free_list(std::exchange(other.m_free_list, nullptr))
        , m_chunks(std::move(other.m_chunks))
    {
        m_allocated_count = other.m_allocated_count.exchange(0);
        m_free_count = other.m_free_count.exchange(0);
        m_peak_allocated = other.m_peak_allocated.exchange(0);
        m_total_allocations = other.m_total_allocations.exchange(0);
        m_total_deallocations = other.m_total_deallocations.exchange(0);
        m_expansions = other.m_expansions.exchange(0);
        m_shrinks = other.m_shrinks.exchange(0);
    }

    FixedMemoryPool &operator=(FixedMemoryPool &&other) noexcept
    {
        if (this != &other)
        {
            for (auto &chunk : m_chunks)
                ::operator delete(chunk.memory);

            m_blocks_per_chunk = other.m_blocks_per_chunk;
            m_max_blocks = other.m_max_blocks;
            m_use_lock = other.m_use_lock;
            m_shrink_threshold_chunks = other.m_shrink_threshold_chunks;
            m_enable_stats = other.m_enable_stats;
            m_enable_debug_checks = other.m_enable_debug_checks;
            m_free_list = std::exchange(other.m_free_list, nullptr);
            m_chunks = std::move(other.m_chunks);
            
            m_allocated_count = other.m_allocated_count.exchange(0);
            m_free_count = other.m_free_count.exchange(0);
            m_peak_allocated = other.m_peak_allocated.exchange(0);
            m_total_allocations = other.m_total_allocations.exchange(0);
            m_total_deallocations = other.m_total_deallocations.exchange(0);
            m_expansions = other.m_expansions.exchange(0);
            m_shrinks = other.m_shrinks.exchange(0);
        }
        return *this;
    }

    FixedMemoryPool(const FixedMemoryPool &) = delete;
    FixedMemoryPool &operator=(const FixedMemoryPool &) = delete;

    // ========================================================================
    // 核心接口
    // ========================================================================

    void *allocate()
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        if (m_free_list == nullptr)
        {
            if (!try_expand())
            {
                return nullptr;
            }
        }

        void *ptr = m_free_list;
        m_free_list = *reinterpret_cast<void **>(m_free_list);

        mark_allocated(ptr);

        m_allocated_count++;
        m_free_count--;
        m_total_allocations++;

        update_peak();

        return ptr;
    }

    void deallocate(void *ptr)
    {
        if (ptr == nullptr)
            return;

        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

#ifdef MEMORY_POOL_DEBUG
        POOL_ASSERT(is_from_pool_unsafe(ptr), "Pointer not from this pool");
        POOL_ASSERT(is_allocated_unsafe(ptr), "Double free detected");
#endif

        mark_free(ptr);

        *reinterpret_cast<void **>(ptr) = m_free_list;
        m_free_list = ptr;

        m_allocated_count--;
        m_free_count++;
        m_total_deallocations++;

        try_shrink();
    }

    template <typename T, typename... Args>
    T *construct(Args &&...args)
    {
        static_assert(sizeof(T) <= BlockSize,
                      "T size exceeds block size");
        static_assert(alignof(T) <= alignof(std::max_align_t),
                      "T alignment requirement not satisfied");

        void *ptr = allocate();
        if (ptr == nullptr)
            return nullptr;

        try
        {
            return new (ptr) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            deallocate(ptr);
            throw;
        }
    }

    template <typename T>
    void destroy(T *ptr)
    {
        if (ptr == nullptr)
            return;
        ptr->~T();
        deallocate(ptr);
    }

    // ========================================================================
    // 批量操作
    // ========================================================================

    std::vector<void *> allocate_bulk(size_t count)
    {
        std::vector<void *> ptrs;
        ptrs.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            void *ptr = allocate();
            if (ptr == nullptr)
            {
                for (void *p : ptrs)
                    deallocate(p);
                return {};
            }
            ptrs.push_back(ptr);
        }

        return ptrs;
    }

    void deallocate_bulk(const std::vector<void *> &ptrs)
    {
        for (void *ptr : ptrs)
        {
            deallocate(ptr);
        }
    }

    template <typename T>
    std::vector<T *> construct_bulk(size_t count)
    {
        static_assert(sizeof(T) <= BlockSize, "T size exceeds block size");

        std::vector<T *> ptrs;
        ptrs.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            T *ptr = construct<T>();
            if (ptr == nullptr)
            {
                for (T *p : ptrs)
                    destroy(p);
                return {};
            }
            ptrs.push_back(ptr);
        }

        return ptrs;
    }

    // ========================================================================
    // 内存管理
    // ========================================================================

    bool expand(size_t block_count)
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        return do_expand(block_count);
    }

    bool shrink(size_t target_free_blocks = 0)
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        return do_shrink(target_free_blocks);
    }

    void shrink_to_fit()
    {
        shrink(0);
    }

    void clear()
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        clear_impl();
    }

    void reset()
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        for (auto &chunk : m_chunks)
        {
            ::operator delete(chunk.memory);
        }
        m_chunks.clear();
        m_free_list = nullptr;
        m_allocated_count = 0;
        m_free_count = 0;

        expand(m_blocks_per_chunk);
    }

    // ========================================================================
    // 统计信息
    // ========================================================================

    [[nodiscard]] size_t block_size() const noexcept { return BlockSize; }
    [[nodiscard]] size_t total_blocks() const { return m_chunks.size() * m_blocks_per_chunk; }
    [[nodiscard]] size_t allocated_count() const { return m_allocated_count.load(); }
    [[nodiscard]] size_t free_count() const { return m_free_count.load(); }
    [[nodiscard]] size_t peak_allocated() const { return m_peak_allocated.load(); }
    [[nodiscard]] size_t total_allocations() const { return m_total_allocations.load(); }
    [[nodiscard]] size_t total_deallocations() const { return m_total_deallocations.load(); }
    [[nodiscard]] size_t expansions() const { return m_expansions.load(); }
    [[nodiscard]] size_t shrinks() const { return m_shrinks.load(); }
    [[nodiscard]] size_t total_chunks() const { return m_chunks.size(); }

    [[nodiscard]] double utilization_rate() const
    {
        size_t total = total_blocks();
        return total > 0 ? static_cast<double>(allocated_count()) / total : 0.0;
    }

    [[nodiscard]] double fragmentation_estimate() const
    {
        size_t total = total_blocks();
        return total > 0 ? static_cast<double>(free_count()) / total : 0.0;
    }

    [[nodiscard]] MemoryPoolStats get_stats() const
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();

        MemoryPoolStats stats;
        stats.block_size = BlockSize;
        stats.total_blocks = total_blocks();
        stats.allocated_blocks = m_allocated_count.load();
        stats.free_blocks = m_free_count.load();
        stats.peak_allocated = m_peak_allocated.load();
        stats.total_allocations = m_total_allocations.load();
        stats.total_deallocations = m_total_deallocations.load();
        stats.expansions = m_expansions.load();
        stats.shrinks = m_shrinks.load();
        stats.total_chunks = m_chunks.size();
        stats.utilization_rate = utilization_rate();
        stats.fragmentation_estimate = fragmentation_estimate();
        return stats;
    }

    // ========================================================================
    // 调试接口
    // ========================================================================

    bool is_from_pool(void *ptr) const
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();
        return is_from_pool_unsafe(ptr);
    }

    bool is_allocated(void *ptr) const
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (m_use_lock)
            lock.lock();
        return is_allocated_unsafe(ptr);
    }

private:
    struct Chunk
    {
        void *memory;
        std::vector<bool> allocated_map;
        size_t block_count;
    };

    // ------------------------------------------------------------------------
    // 指针验证
    // ------------------------------------------------------------------------

    bool is_from_pool_unsafe(void *ptr) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            char *start = static_cast<char *>(m_chunks[i].memory);
            char *end = start + m_chunks[i].block_count * BlockSize;
            char *p = static_cast<char *>(ptr);
            
            if (p >= start && p < end)
            {
                // 验证对齐
                size_t offset = static_cast<size_t>(p - start);
                if (offset % BlockSize != 0)
                    return false;
                    
                // 验证块索引有效
                size_t block_idx = offset / BlockSize;
                if (block_idx >= m_chunks[i].block_count)
                    return false;
                    
                return true;
            }
        }
        return false;
    }

    bool is_allocated_unsafe(void *ptr) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            char *start = static_cast<char *>(m_chunks[i].memory);
            char *end = start + m_chunks[i].block_count * BlockSize;
            char *p = static_cast<char *>(ptr);
            
            if (p >= start && p < end)
            {
                size_t offset = static_cast<size_t>(p - start);
                if (offset % BlockSize != 0)
                    return false;
                    
                size_t block_idx = offset / BlockSize;
                if (block_idx >= m_chunks[i].allocated_map.size())
                    return false;
                    
                return m_chunks[i].allocated_map[block_idx];
            }
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // 扩展和收缩
    // ------------------------------------------------------------------------

    bool try_expand()
    {
        if (m_max_blocks > 0 && total_blocks() >= m_max_blocks)
        {
            return false;
        }
        return do_expand(m_blocks_per_chunk);
    }

    bool do_expand(size_t block_count)
    {
        if (block_count == 0)
            return false;

        size_t total_size = block_count * BlockSize;
        void *new_memory = ::operator new(total_size, std::nothrow);
        if (new_memory == nullptr)
            return false;

        Chunk chunk;
        chunk.memory = new_memory;
        chunk.block_count = block_count;
        chunk.allocated_map.resize(block_count, false);
        m_chunks.push_back(chunk);
        m_expansions++;

        char *start = static_cast<char *>(new_memory);
        for (size_t i = 0; i < block_count; ++i)
        {
            char *ptr = start + i * BlockSize;
            *reinterpret_cast<void **>(ptr) = m_free_list;
            m_free_list = ptr;
        }

        m_free_count += block_count;
        return true;
    }

    void try_shrink()
    {
        size_t free = m_free_count.load();
        size_t threshold = m_shrink_threshold_chunks * m_blocks_per_chunk;

        if (free < threshold)
            return;

        if (m_max_blocks > 0 && total_blocks() <= m_blocks_per_chunk)
            return;

        if (m_chunks.size() <= 1)
            return;

        do_shrink(0);
    }

    bool do_shrink(size_t target_free_blocks)
    {
        // 找出完全空闲的 chunk
        std::vector<size_t> chunks_to_free;
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (is_chunk_completely_free(i))
            {
                chunks_to_free.push_back(i);
            }
        }

        if (chunks_to_free.empty())
            return false;

        // 至少保留一个 chunk
        if (chunks_to_free.size() == m_chunks.size())
        {
            chunks_to_free.pop_back();
        }

        size_t total_removed_blocks = 0;
        
        for (size_t idx : chunks_to_free)
        {
            // 从空闲链表中移除并获取实际移除的块数
            size_t removed = remove_chunk_blocks_from_free_list(idx);
            total_removed_blocks += removed;
            
            // 释放内存
            ::operator delete(m_chunks[idx].memory);
            m_shrinks++;
        }

        // 更新 m_free_count：减去实际移除的块数
        m_free_count -= total_removed_blocks;

        // 删除 chunk
        std::sort(chunks_to_free.begin(), chunks_to_free.end(), std::greater<size_t>());
        for (size_t idx : chunks_to_free)
        {
            m_chunks.erase(m_chunks.begin() + static_cast<ptrdiff_t>(idx));
        }

        return true;
    }

    void clear_impl()
    {
        // 找出所有完全空闲的 chunk
        std::vector<size_t> chunks_to_free;
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (is_chunk_completely_free(i))
            {
                chunks_to_free.push_back(i);
            }
        }

        if (chunks_to_free.empty())
            return;

        // 计算需要从空闲链表中移除的块
        size_t blocks_to_remove = 0;
        
        // 从全局空闲链表中移除这些 chunk 的所有块
        for (size_t idx : chunks_to_free)
        {
            size_t removed = remove_chunk_blocks_from_free_list(idx);
            blocks_to_remove += removed;
            
            // 释放内存
            ::operator delete(m_chunks[idx].memory);
            m_shrinks++;
        }

        // 更新 m_free_count：减去实际从空闲链表中移除的块数
        // 注意：只减去那些确实在空闲链表中的块，而不是整个 chunk 的块数
        m_free_count -= blocks_to_remove;

        // 从后往前删除 chunk，避免索引失效
        std::sort(chunks_to_free.begin(), chunks_to_free.end(), std::greater<size_t>());
        for (size_t idx : chunks_to_free)
        {
            m_chunks.erase(m_chunks.begin() + static_cast<ptrdiff_t>(idx));
        }
    }

    // ------------------------------------------------------------------------
    // 从空闲链表中移除指定 chunk 的所有空闲块，返回实际移除的块数
    // ------------------------------------------------------------------------

    size_t remove_chunk_blocks_from_free_list(size_t chunk_idx)
    {
        char *chunk_start = static_cast<char *>(m_chunks[chunk_idx].memory);
        char *chunk_end = chunk_start + m_chunks[chunk_idx].block_count * BlockSize;

        size_t removed_count = 0;
        void **prev = &m_free_list;
        void *curr = m_free_list;

        while (curr)
        {
            char *ptr = static_cast<char *>(curr);
            if (ptr >= chunk_start && ptr < chunk_end)
            {
                // 验证对齐
                size_t offset = static_cast<size_t>(ptr - chunk_start);
                if (offset % BlockSize == 0)
                {
                    // 这个块属于当前 chunk，从链表中移除
                    *prev = *reinterpret_cast<void **>(curr);
                    curr = *prev;
                    removed_count++;
                }
                else
                {
                    // 不对齐，跳过（理论上不应该发生）
                    prev = reinterpret_cast<void **>(curr);
                    curr = *reinterpret_cast<void **>(curr);
                }
            }
            else
            {
                prev = reinterpret_cast<void **>(curr);
                curr = *reinterpret_cast<void **>(curr);
            }
        }

        return removed_count;
    }

    // ------------------------------------------------------------------------
    // 辅助函数
    // ------------------------------------------------------------------------

    void update_peak()
    {
        if (!m_enable_stats)
            return;
            
        size_t current = m_allocated_count.load(std::memory_order_acquire);
        size_t peak = m_peak_allocated.load(std::memory_order_acquire);
        
        while (current > peak)
        {
            if (m_peak_allocated.compare_exchange_weak(peak, current,
                    std::memory_order_release, std::memory_order_acquire))
                break;
            current = m_allocated_count.load(std::memory_order_acquire);
        }
    }

    void mark_allocated(void *ptr) noexcept
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            char *start = static_cast<char *>(m_chunks[i].memory);
            char *end = start + m_chunks[i].block_count * BlockSize;
            char *p = static_cast<char *>(ptr);
            
            if (p >= start && p < end)
            {
                size_t offset = static_cast<size_t>(p - start);
                size_t block_idx = offset / BlockSize;
                if (block_idx < m_chunks[i].allocated_map.size())
                {
                    m_chunks[i].allocated_map[block_idx] = true;
                }
                return;
            }
        }
    }

    void mark_free(void *ptr) noexcept
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            char *start = static_cast<char *>(m_chunks[i].memory);
            char *end = start + m_chunks[i].block_count * BlockSize;
            char *p = static_cast<char *>(ptr);
            
            if (p >= start && p < end)
            {
                size_t offset = static_cast<size_t>(p - start);
                size_t block_idx = offset / BlockSize;
                if (block_idx < m_chunks[i].allocated_map.size())
                {
                    m_chunks[i].allocated_map[block_idx] = false;
                }
                return;
            }
        }
    }

    bool is_chunk_completely_free(size_t chunk_idx) const
    {
        // 方法1：通过 allocated_map 检查
        const auto &map = m_chunks[chunk_idx].allocated_map;
        for (bool allocated : map)
        {
            if (allocated)
                return false;
        }
        return true;
        
        // 也可以遍历空闲链表来验证，但 allocated_map 已经足够
    }

    void remove_chunk_from_free_list(size_t chunk_idx)
    {
        char *chunk_start = static_cast<char *>(m_chunks[chunk_idx].memory);
        char *chunk_end = chunk_start + m_chunks[chunk_idx].block_count * BlockSize;

        void **prev = &m_free_list;
        void *curr = m_free_list;

        while (curr)
        {
            char *ptr = static_cast<char *>(curr);
            if (ptr >= chunk_start && ptr < chunk_end)
            {
                *prev = *reinterpret_cast<void **>(curr);
                curr = *prev;
            }
            else
            {
                prev = reinterpret_cast<void **>(curr);
                curr = *reinterpret_cast<void **>(curr);
            }
        }
    }

private:
    size_t m_blocks_per_chunk;
    size_t m_max_blocks;
    bool m_use_lock;
    size_t m_shrink_threshold_chunks;
    bool m_enable_stats = true;
    bool m_enable_debug_checks = false;

    void *m_free_list = nullptr;
    std::vector<Chunk> m_chunks;

    std::atomic<size_t> m_allocated_count{0};
    std::atomic<size_t> m_free_count{0};
    std::atomic<size_t> m_peak_allocated{0};
    std::atomic<size_t> m_total_allocations{0};
    std::atomic<size_t> m_total_deallocations{0};
    std::atomic<size_t> m_expansions{0};
    std::atomic<size_t> m_shrinks{0};

    mutable std::mutex m_mutex;
};

// ============================================================================
// 对齐内存池
// ============================================================================

template <size_t BlockSize, size_t Alignment = alignof(std::max_align_t)>
class AlignedMemoryPool
{
public:
    static_assert(Alignment > 0 && (Alignment & (Alignment - 1)) == 0,
                  "Alignment must be a power of 2");
    static_assert(BlockSize >= Alignment,
                  "BlockSize must be at least Alignment");

    explicit AlignedMemoryPool(size_t blocks_per_chunk = 1024, size_t max_blocks = 0)
        : m_blocks_per_chunk(blocks_per_chunk)
        , m_max_blocks(max_blocks)
    {
        expand(blocks_per_chunk);
    }

    ~AlignedMemoryPool()
    {
        for (auto &chunk : m_chunks)
        {
            deallocate_aligned(chunk.memory);
        }
    }

    AlignedMemoryPool(AlignedMemoryPool &&other) noexcept
        : m_blocks_per_chunk(other.m_blocks_per_chunk)
        , m_max_blocks(other.m_max_blocks)
        , m_free_list(std::exchange(other.m_free_list, nullptr))
        , m_chunks(std::move(other.m_chunks))
    {
        m_allocated_count = other.m_allocated_count.exchange(0);
        m_free_count = other.m_free_count.exchange(0);
    }

    AlignedMemoryPool &operator=(AlignedMemoryPool &&other) noexcept
    {
        if (this != &other)
        {
            for (auto &chunk : m_chunks)
                deallocate_aligned(chunk.memory);

            m_blocks_per_chunk = other.m_blocks_per_chunk;
            m_max_blocks = other.m_max_blocks;
            m_free_list = std::exchange(other.m_free_list, nullptr);
            m_chunks = std::move(other.m_chunks);
            m_allocated_count = other.m_allocated_count.exchange(0);
            m_free_count = other.m_free_count.exchange(0);
        }
        return *this;
    }

    AlignedMemoryPool(const AlignedMemoryPool &) = delete;
    AlignedMemoryPool &operator=(const AlignedMemoryPool &) = delete;

    void *allocate()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_free_list == nullptr)
        {
            if (!expand(m_blocks_per_chunk))
            {
                return nullptr;
            }
        }

        void *ptr = m_free_list;
        m_free_list = *reinterpret_cast<void **>(m_free_list);
        m_allocated_count++;
        m_free_count--;
        return ptr;
    }

    void deallocate(void *ptr)
    {
        if (ptr == nullptr)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);
        *reinterpret_cast<void **>(ptr) = m_free_list;
        m_free_list = ptr;
        m_allocated_count--;
        m_free_count++;
    }

    template <typename T, typename... Args>
    T *construct(Args &&...args)
    {
        static_assert(sizeof(T) <= BlockSize, "T size exceeds block size");
        static_assert(alignof(T) <= Alignment, "T alignment exceeds pool alignment");

        void *ptr = allocate();
        if (ptr == nullptr)
            return nullptr;
        try
        {
            return new (ptr) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            deallocate(ptr);
            throw;
        }
    }

    template <typename T>
    void destroy(T *ptr)
    {
        if (ptr == nullptr)
            return;
        ptr->~T();
        deallocate(ptr);
    }

    size_t block_size() const noexcept { return BlockSize; }
    size_t alignment() const noexcept { return Alignment; }
    size_t total_blocks() const { return m_chunks.size() * m_blocks_per_chunk; }
    size_t allocated_count() const { return m_allocated_count; }
    size_t free_count() const { return m_free_count; }
    
    double utilization_rate() const
    {
        size_t total = total_blocks();
        return total > 0 ? static_cast<double>(allocated_count()) / total : 0.0;
    }

private:
    struct Chunk
    {
        void *memory;
        size_t block_count;
    };

    static void *allocate_aligned(size_t size)
    {
#if defined(_WIN32) && !defined(__MINGW32__)
        return _aligned_malloc(size, Alignment);
#elif __cplusplus >= 201703L
        return std::aligned_alloc(Alignment, size);
#else
        void *ptr = nullptr;
        if (posix_memalign(&ptr, Alignment, size) != 0)
            return nullptr;
        return ptr;
#endif
    }

    static void deallocate_aligned(void *ptr)
    {
        if (!ptr) return;
        
#if defined(_WIN32) && !defined(__MINGW32__)
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    bool expand(size_t block_count)
    {
        if (m_max_blocks > 0 && total_blocks() + block_count > m_max_blocks)
        {
            block_count = m_max_blocks - total_blocks();
            if (block_count == 0)
                return false;
        }

        size_t total_size = block_count * BlockSize;
        void *new_memory = allocate_aligned(total_size);
        if (new_memory == nullptr)
            return false;

        Chunk chunk;
        chunk.memory = new_memory;
        chunk.block_count = block_count;
        m_chunks.push_back(chunk);

        char *start = static_cast<char *>(new_memory);
        for (size_t i = 0; i < block_count; ++i)
        {
            char *ptr = start + i * BlockSize;
            *reinterpret_cast<void **>(ptr) = m_free_list;
            m_free_list = ptr;
        }

        m_free_count += block_count;
        return true;
    }

private:
    size_t m_blocks_per_chunk;
    size_t m_max_blocks;
    void *m_free_list = nullptr;
    std::vector<Chunk> m_chunks;
    std::atomic<size_t> m_allocated_count{0};
    std::atomic<size_t> m_free_count{0};
    std::mutex m_mutex;
};

// ============================================================================
// STL 分配器适配器
// ============================================================================

template <typename T, typename Pool>
class PoolAllocator
{
public:
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind
    {
        using other = PoolAllocator<U, Pool>;
    };

    PoolAllocator() : m_pool(std::make_shared<Pool>()) {}
    
    explicit PoolAllocator(std::shared_ptr<Pool> pool) : m_pool(pool) {}
    
    template <typename U>
    PoolAllocator(const PoolAllocator<U, Pool> &other) : m_pool(other.pool()) {}

    T *allocate(size_t n)
    {
        if (n == 1 && sizeof(T) <= m_pool->block_size())
        {
            return static_cast<T *>(m_pool->allocate());
        }
        return static_cast<T *>(::operator new(n * sizeof(T)));
    }

    void deallocate(T *ptr, size_t n)
    {
        if (n == 1 && sizeof(T) <= m_pool->block_size())
        {
            m_pool->deallocate(ptr);
        }
        else
        {
            ::operator delete(ptr);
        }
    }

    template <typename U, typename... Args>
    void construct(U *ptr, Args &&...args)
    {
        new (ptr) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U *ptr)
    {
        ptr->~U();
    }

    std::shared_ptr<Pool> pool() const { return m_pool; }

private:
    std::shared_ptr<Pool> m_pool;
};

template <typename T, typename Pool, typename U, typename Pool2>
bool operator==(const PoolAllocator<T, Pool> &a,
                const PoolAllocator<U, Pool2> &b)
{
    return a.pool() == b.pool();
}

template <typename T, typename Pool, typename U, typename Pool2>
bool operator!=(const PoolAllocator<T, Pool> &a,
                const PoolAllocator<U, Pool2> &b)
{
    return !(a == b);
}

// ============================================================================
// 便捷类型别名
// ============================================================================

template <typename T>
using ObjectPoolAllocator = PoolAllocator<T, FixedMemoryPool<sizeof(T)>>;

using DefaultMemoryPool = FixedMemoryPool<64>;
using SmallMemoryPool = FixedMemoryPool<32>;
using MediumMemoryPool = FixedMemoryPool<128>;
using LargeMemoryPool = FixedMemoryPool<256>;

using Aligned16MemoryPool = AlignedMemoryPool<64, 16>;
using Aligned32MemoryPool = AlignedMemoryPool<64, 32>;
using Aligned64MemoryPool = AlignedMemoryPool<64, 64>;

#endif // MEMORY_POOL_HPP