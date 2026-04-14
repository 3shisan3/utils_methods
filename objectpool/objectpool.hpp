/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        objectpool.hpp
Version:     1.0
Author:      cjx
start date: 2024-12-31
Description: 通用对象池实现，支持连接池、线程池等场景
             提供 RAII 包装器、统计信息和灵活配置
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1            2026-04-14       cjx           create
*****************************************************************/

#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

// ============================================================================
// 调试宏
// ============================================================================

#ifdef OBJECT_POOL_DEBUG
#include <cassert>
#define POOL_ASSERT(cond, msg) assert((cond) && (msg))
#else
#define POOL_ASSERT(cond, msg) ((void)0)
#endif

// ============================================================================
// 对象池配置
// ============================================================================

template <typename T>
struct ObjectPoolConfig
{
    /// 工厂函数：创建新对象（不应抛出异常）
    std::function<T *()> factory;
    
    /// 重置函数：对象归还时调用，恢复初始状态
    std::function<void(T *)> resetter;
    
    /// 验证函数：归还时验证对象是否仍然有效
    std::function<bool(T *)> validator;
    
    /// 删除器：自定义对象销毁方式（默认使用 delete）
    std::function<void(T *)> deleter = [](T *p) { delete p; };
    
    /// 初始池大小
    size_t initial_size = 10;
    
    /// 最大池大小（0 = 无限制）
    size_t max_size = 0;
    
    /// 最大等待队列长度（0 = 无限制）
    size_t max_waiters = 0;
    
    /// 最大空闲时间（0 = 永不过期）
    std::chrono::milliseconds max_idle_time = std::chrono::milliseconds(0);
    
    /// 是否启用统计信息
    bool enable_stats = true;
    
    /// 是否启用自动清理线程
    bool enable_auto_cleanup = false;
    
    /// 自动清理间隔（默认是 max_idle_time 的一半）
    std::chrono::milliseconds cleanup_interval = std::chrono::milliseconds(0);
    
    /// 泄漏检测回调（析构时如有未归还对象则调用）
    std::function<void(size_t)> leak_callback = nullptr;
};

// ============================================================================
// 对象池统计信息
// ============================================================================

struct ObjectPoolStats
{
    size_t created = 0;                 // 累计创建的对象数
    size_t borrowed = 0;                // 当前借出的对象数
    size_t available = 0;               // 当前可用对象数
    size_t peak_borrowed = 0;           // 峰值借出数
    size_t total_borrows = 0;           // 总借用次数
    size_t total_returns = 0;           // 总归还次数
    size_t total_destroyed = 0;         // 总销毁数
    size_t total_timeouts = 0;          // 总超时次数
    size_t total_creates_on_borrow = 0; // borrow 时创建的对象数
    size_t total_create_failures = 0;   // 工厂函数失败次数
    size_t current_waiters = 0;         // 当前等待的线程数
    size_t peak_waiters = 0;            // 峰值等待线程数
    double hit_rate = 0.0;              // 命中率
    double avg_idle_time_ms = 0.0;      // 平均空闲时间（毫秒）
};

// ============================================================================
// 池化对象包装器（RAII）
// ============================================================================

template <typename T>
class PooledObject
{
public:
    PooledObject() = default;

    PooledObject(T *obj, std::function<void(T *)> deleter)
        : m_obj(obj), m_deleter(std::move(deleter)) {}

    ~PooledObject()
    {
        reset();
    }

    // 移动语义
    PooledObject(PooledObject &&other) noexcept
        : m_obj(std::exchange(other.m_obj, nullptr))
        , m_deleter(std::move(other.m_deleter)) {}

    PooledObject &operator=(PooledObject &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_obj = std::exchange(other.m_obj, nullptr);
            m_deleter = std::move(other.m_deleter);
        }
        return *this;
    }

    // 禁止拷贝
    PooledObject(const PooledObject &) = delete;
    PooledObject &operator=(const PooledObject &) = delete;

    // 指针操作
    T *operator->() { return m_obj; }
    const T *operator->() const { return m_obj; }
    T &operator*() { return *m_obj; }
    const T &operator*() const { return *m_obj; }

    T *get() { return m_obj; }
    const T *get() const { return m_obj; }

    explicit operator bool() const { return m_obj != nullptr; }

    // 手动归还对象
    void reset()
    {
        if (m_obj && m_deleter)
        {
            m_deleter(m_obj);
            m_obj = nullptr;
        }
    }

    // 释放所有权（不归还池）
    T *release()
    {
        return std::exchange(m_obj, nullptr);
    }

private:
    T *m_obj = nullptr;
    std::function<void(T *)> m_deleter;
};

// ============================================================================
// 通用对象池
// ============================================================================

template <typename T>
class ObjectPool
{
public:
    using Config = ObjectPoolConfig<T>;

    // ========================================================================
    // 构造与析构
    // ========================================================================

    explicit ObjectPool(const Config &cfg)
        : m_factory(cfg.factory)
        , m_resetter(cfg.resetter)
        , m_validator(cfg.validator)
        , m_deleter(cfg.deleter)
        , m_max_size(cfg.max_size)
        , m_max_waiters(cfg.max_waiters)
        , m_max_idle_time(cfg.max_idle_time)
        , m_enable_stats(cfg.enable_stats)
        , m_leak_callback(cfg.leak_callback)
    {
        if (!m_factory)
        {
            throw std::invalid_argument("ObjectPool: factory function is required");
        }
        if (!m_deleter)
        {
            throw std::invalid_argument("ObjectPool: deleter function is required");
        }

        // 预创建对象
        preallocate_impl(cfg.initial_size);

        // 启动自动清理线程
        if (cfg.enable_auto_cleanup && m_max_idle_time > std::chrono::milliseconds(0))
        {
            start_cleanup_thread(cfg.cleanup_interval);
        }
    }

    // 创建默认配置的池（使用 new/delete）
    static ObjectPool<T> create_default(size_t initial_size = 10, size_t max_size = 0)
    {
        Config cfg;
        cfg.factory = []() { return new T(); };
        cfg.deleter = [](T *p) { delete p; };
        cfg.initial_size = initial_size;
        cfg.max_size = max_size;
        return ObjectPool<T>(cfg);
    }

    ~ObjectPool()
    {
        // 停止清理线程
        stop_cleanup_thread();

        std::unique_lock<std::mutex> lock(m_mutex);

        // 清理所有空闲对象
        while (!m_pool.empty())
        {
            m_deleter(m_pool.front().obj);
            m_pool.pop();
        }

        // 记录泄漏的对象
        size_t leaked = m_borrowed_count.load();
        if (leaked > 0 && m_leak_callback)
        {
            m_leak_callback(leaked);
        }

        // 重置统计
        m_created_count = 0;
        m_borrowed_count = 0;
        m_free_count = 0;
    }

    // 禁止拷贝
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;

    // 移动语义
    ObjectPool(ObjectPool &&other) noexcept
        : m_factory(std::move(other.m_factory))
        , m_resetter(std::move(other.m_resetter))
        , m_validator(std::move(other.m_validator))
        , m_deleter(std::move(other.m_deleter))
        , m_max_size(other.m_max_size)
        , m_max_waiters(other.m_max_waiters)
        , m_max_idle_time(other.m_max_idle_time)
        , m_enable_stats(other.m_enable_stats)
        , m_leak_callback(std::move(other.m_leak_callback))
        , m_created_count(other.m_created_count.exchange(0))
        , m_borrowed_count(other.m_borrowed_count.exchange(0))
        , m_free_count(other.m_free_count.exchange(0))
        , m_peak_borrowed(other.m_peak_borrowed.exchange(0))
        , m_total_borrows(other.m_total_borrows.exchange(0))
        , m_total_returns(other.m_total_returns.exchange(0))
        , m_total_destroyed(other.m_total_destroyed.exchange(0))
        , m_total_timeouts(other.m_total_timeouts.exchange(0))
        , m_total_creates_on_borrow(other.m_total_creates_on_borrow.exchange(0))
        , m_total_create_failures(other.m_total_create_failures.exchange(0))
        , m_total_idle_time_ms(other.m_total_idle_time_ms.exchange(0))
        , m_idle_sample_count(other.m_idle_sample_count.exchange(0))
        , m_current_waiters(other.m_current_waiters.exchange(0))
        , m_peak_waiters(other.m_peak_waiters.exchange(0))
    {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_pool = std::move(other.m_pool);
        
        m_cleanup_running = other.m_cleanup_running.exchange(false);
        if (other.m_cleanup_thread.joinable())
        {
            other.m_cleanup_thread.detach();
        }
    }

    ObjectPool &operator=(ObjectPool &&other) noexcept
    {
        if (this != &other)
        {
            stop_cleanup_thread();
            
            std::lock_guard<std::mutex> lock(m_mutex);
            std::lock_guard<std::mutex> other_lock(other.m_mutex);
            
            // 清理当前资源
            while (!m_pool.empty())
            {
                m_deleter(m_pool.front().obj);
                m_pool.pop();
            }
    
            // 移动资源
            m_factory = std::move(other.m_factory);
            m_resetter = std::move(other.m_resetter);
            m_validator = std::move(other.m_validator);
            m_deleter = std::move(other.m_deleter);
            m_max_size = other.m_max_size;
            m_max_waiters = other.m_max_waiters;
            m_max_idle_time = other.m_max_idle_time;
            m_enable_stats = other.m_enable_stats;
            m_leak_callback = std::move(other.m_leak_callback);
            m_pool = std::move(other.m_pool);
            
            // 移动原子变量
            m_created_count = other.m_created_count.exchange(0);
            m_borrowed_count = other.m_borrowed_count.exchange(0);
            m_free_count = other.m_free_count.exchange(0);
            m_peak_borrowed = other.m_peak_borrowed.exchange(0);
            m_total_borrows = other.m_total_borrows.exchange(0);
            m_total_returns = other.m_total_returns.exchange(0);
            m_total_destroyed = other.m_total_destroyed.exchange(0);
            m_total_timeouts = other.m_total_timeouts.exchange(0);
            m_total_creates_on_borrow = other.m_total_creates_on_borrow.exchange(0);
            m_total_create_failures = other.m_total_create_failures.exchange(0);
            m_total_idle_time_ms = other.m_total_idle_time_ms.exchange(0);
            m_idle_sample_count = other.m_idle_sample_count.exchange(0);
            m_current_waiters = other.m_current_waiters.exchange(0);
            m_peak_waiters = other.m_peak_waiters.exchange(0);
        }
        return *this;
    }

    // ========================================================================
    // 借用接口
    // ========================================================================

    // 阻塞借用（无限等待）
    T *borrow()
    {
        return borrow_impl(std::chrono::milliseconds::zero(), false);
    }

    // 带超时的借用
    template <typename Rep, typename Period>
    T *borrow_for(const std::chrono::duration<Rep, Period> &timeout)
    {
        return borrow_impl(
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout),
            true);
    }

    // 非阻塞借用（立即返回）
    std::optional<T *> try_borrow()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_pool.empty())
        {
            return take_from_pool_impl();
        }

        // 池空，尝试创建新对象
        if (m_max_size == 0 || m_created_count < m_max_size)
        {
            T *obj = create_object_safe();
            if (obj)
            {
                m_created_count++;
                m_borrowed_count++;
                m_total_borrows++;
                if (m_enable_stats)
                    m_total_creates_on_borrow++;
                update_peak();
                return obj;
            }
            else
            {
                if (m_enable_stats)
                    m_total_create_failures++;
            }
        }

        return std::nullopt;
    }

    // ========================================================================
    // RAII 包装器
    // ========================================================================

    // 自动归还的包装器（阻塞）
    PooledObject<T> borrow_auto()
    {
        T *obj = borrow();
        return PooledObject<T>(obj, [this](T *p) { return_object(p); });
    }

    // 自动归还的包装器（带超时）
    template <typename Rep, typename Period>
    PooledObject<T> borrow_auto_for(const std::chrono::duration<Rep, Period> &timeout)
    {
        T *obj = borrow_for(timeout);
        if (!obj)
            return PooledObject<T>();
        return PooledObject<T>(obj, [this](T *p) { return_object(p); });
    }

    // 返回 shared_ptr 包装器
    std::shared_ptr<T> borrow_shared()
    {
        T *obj = borrow();
        if (!obj)
            return nullptr;
        return std::shared_ptr<T>(obj, [this](T *p) { return_object(p); });
    }

    // ========================================================================
    // 归还接口
    // ========================================================================

    // 归还对象到池中
    bool return_object(T *obj)
    {
        if (obj == nullptr)
            return false;

        std::lock_guard<std::mutex> lock(m_mutex);

        // 验证对象有效性
        if (m_validator)
        {
            bool valid = false;
            try
            {
                valid = m_validator(obj);
            }
            catch (...)
            {
                valid = false;
            }
            
            if (!valid)
            {
                m_deleter(obj);
                m_created_count--;
                m_borrowed_count--;
                m_total_destroyed++;
                return true;
            }
        }

        // 重置对象状态
        if (m_resetter)
        {
            try
            {
                m_resetter(obj);
            }
            catch (...)
            {
                // 重置失败，删除对象
                m_deleter(obj);
                m_created_count--;
                m_borrowed_count--;
                m_total_destroyed++;
                return false;
            }
        }

        // 检查是否超过最大容量
        if (m_max_size > 0 && m_pool.size() >= m_max_size)
        {
            m_deleter(obj);
            m_created_count--;
            m_total_destroyed++;
        }
        else
        {
            m_pool.push({obj, std::chrono::steady_clock::now()});
            m_free_count++;
        }

        m_borrowed_count--;
        m_total_returns++;

        m_cv.notify_one();
        return true;
    }

    // ========================================================================
    // 批量操作
    // ========================================================================

    std::vector<T *> borrow_bulk(size_t count)
    {
        std::vector<T *> objs;
        objs.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            T *obj = borrow();
            if (!obj)
            {
                for (T *p : objs)
                    return_object(p);
                return {};
            }
            objs.push_back(obj);
        }

        return objs;
    }

    void return_bulk(const std::vector<T *> &objs)
    {
        for (T *obj : objs)
        {
            return_object(obj);
        }
    }

    // 批量借用（RAII 包装）
    std::vector<PooledObject<T>> borrow_bulk_auto(size_t count)
    {
        std::vector<PooledObject<T>> objs;
        objs.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            objs.emplace_back(borrow_auto());
            if (!objs.back())
            {
                return {};
            }
        }

        return objs;
    }

    // ========================================================================
    // 池管理
    // ========================================================================

    // 预分配对象
    bool preallocate(size_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return preallocate_impl(count);
    }

    // 预热对象（预分配并初始化）
    bool warm_up(size_t count, std::function<void(T *)> initializer = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        size_t created = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (m_max_size > 0 && m_created_count >= m_max_size)
                break;

            T *obj = create_object_safe();
            if (!obj)
                break;

            if (initializer)
            {
                try
                {
                    initializer(obj);
                }
                catch (...)
                {
                    m_deleter(obj);
                    throw;
                }
            }

            m_pool.push({obj, std::chrono::steady_clock::now()});
            m_created_count++;
            m_free_count++;
            created++;
        }

        return created > 0;
    }

    /// 收缩到指定大小
    void shrink_to(size_t target_size)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        shrink_to_impl(target_size);
    }

    /// 收缩到最小（移除所有空闲对象）
    void shrink_to_fit()
    {
        shrink_to(0);
    }

    /// 清空池（保留已借出的对象）
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        while (!m_pool.empty())
        {
            m_deleter(m_pool.front().obj);
            m_pool.pop();
        }
        m_free_count = 0;
        m_created_count = m_borrowed_count.load();
    }

    /// 回收空闲超时的对象
    size_t reap_idle_objects()
    {
        if (m_max_idle_time == std::chrono::milliseconds::zero())
            return 0;

        std::lock_guard<std::mutex> lock(m_mutex);
        return reap_idle_objects_impl();
    }

    // ========================================================================
    // 统计信息
    // ========================================================================

    [[nodiscard]] size_t available() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pool.size();
    }

    [[nodiscard]] size_t borrowed() const
    {
        return m_borrowed_count.load();
    }

    [[nodiscard]] size_t created() const
    {
        return m_created_count.load();
    }

    [[nodiscard]] size_t peak_borrowed() const
    {
        return m_peak_borrowed.load();
    }

    [[nodiscard]] double hit_rate() const
    {
        size_t total = m_total_borrows.load();
        if (total == 0)
            return 0.0;
        size_t creates = m_total_creates_on_borrow.load();
        return static_cast<double>(total - creates) / total;
    }

    [[nodiscard]] ObjectPoolStats get_stats() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        ObjectPoolStats stats;
        stats.created = m_created_count.load();
        stats.borrowed = m_borrowed_count.load();
        stats.available = m_pool.size();
        stats.peak_borrowed = m_peak_borrowed.load();
        stats.total_borrows = m_total_borrows.load();
        stats.total_returns = m_total_returns.load();
        stats.total_destroyed = m_total_destroyed.load();
        stats.total_timeouts = m_total_timeouts.load();
        stats.total_creates_on_borrow = m_total_creates_on_borrow.load();
        stats.total_create_failures = m_total_create_failures.load();
        stats.current_waiters = m_current_waiters.load();
        stats.peak_waiters = m_peak_waiters.load();
        stats.hit_rate = hit_rate();
        
        size_t samples = m_idle_sample_count.load();
        stats.avg_idle_time_ms = samples > 0 
            ? static_cast<double>(m_total_idle_time_ms.load()) / samples 
            : 0.0;
        
        return stats;
    }

    // ========================================================================
    // 配置更新
    // ========================================================================

    void set_resetter(std::function<void(T *)> resetter)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resetter = std::move(resetter);
    }

    void set_validator(std::function<bool(T *)> validator)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_validator = std::move(validator);
    }

    void set_max_idle_time(std::chrono::milliseconds max_idle_time)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_max_idle_time = max_idle_time;
    }

private:
    struct PooledObjectEntry
    {
        T *obj;
        std::chrono::steady_clock::time_point last_used;
    };

    // ------------------------------------------------------------------------
    // 安全的对象创建
    // ------------------------------------------------------------------------

    T *create_object_safe() noexcept
    {
        try
        {
            return m_factory();
        }
        catch (...)
        {
            return nullptr;
        }
    }

    // ------------------------------------------------------------------------
    // 借用实现
    // ------------------------------------------------------------------------

    T *borrow_impl(std::chrono::milliseconds timeout, bool use_timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 检查等待队列限制
        if (m_max_waiters > 0)
        {
            size_t current = m_current_waiters.load();
            if (current >= m_max_waiters)
            {
                if (m_enable_stats)
                    m_total_timeouts++;
                return nullptr;
            }
        }

        // 增加等待计数
        increment_waiters();

        // 条件变量谓词
        auto wait_condition = [this] {
            return !m_pool.empty() || (m_max_size == 0 || m_created_count < m_max_size);
        };

        // 检查是否应该立即失败（池空且已达最大容量）
        if (m_pool.empty() && m_max_size > 0 && m_created_count >= m_max_size)
        {
            if (use_timeout && timeout > std::chrono::milliseconds::zero())
            {
                decrement_waiters();
                if (m_enable_stats)
                    m_total_timeouts++;
                return nullptr;
            }
        }

        // 等待直到条件满足或超时
        bool timed_out = false;
        if (use_timeout && timeout > std::chrono::milliseconds::zero())
        {
            if (!m_cv.wait_for(lock, timeout, wait_condition))
            {
                timed_out = true;
            }
        }
        else
        {
            m_cv.wait(lock, wait_condition);
        }

        decrement_waiters();

        if (timed_out)
        {
            if (m_enable_stats)
                m_total_timeouts++;
            return nullptr;
        }

        // 从池中获取
        if (!m_pool.empty())
        {
            return take_from_pool_impl();
        }

        // 创建新对象（统一在此处处理统计）
        if (m_max_size == 0 || m_created_count < m_max_size)
        {
            T *obj = create_object_safe();
            if (obj)
            {
                m_created_count++;
                m_borrowed_count++;
                m_total_borrows++;
                if (m_enable_stats)
                    m_total_creates_on_borrow++;
                update_peak();
                return obj;
            }
            else
            {
                // 创建失败
                if (m_enable_stats)
                    m_total_create_failures++;
            }
        }

        return nullptr;
    }

    void increment_waiters()
    {
        if (!m_enable_stats)
            return;
        
        size_t current = m_current_waiters.fetch_add(1) + 1;
        size_t peak = m_peak_waiters.load();
        while (current > peak)
        {
            if (m_peak_waiters.compare_exchange_weak(peak, current))
                break;
            current = m_current_waiters.load();
        }
    }

    void decrement_waiters()
    {
        if (m_enable_stats)
            m_current_waiters--;
    }

    T *take_from_pool_impl()
    {
        auto entry = m_pool.front();
        m_pool.pop();
        T *obj = entry.obj;
        m_free_count--;

        if (m_enable_stats)
        {
            auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - entry.last_used);
            m_total_idle_time_ms += idle_time.count();
            m_idle_sample_count++;
        }

        m_borrowed_count++;
        m_total_borrows++;
        update_peak();

        return obj;
    }

    // ------------------------------------------------------------------------
    // 峰值更新
    // ------------------------------------------------------------------------

    void update_peak()
    {
        if (!m_enable_stats)
            return;
            
        size_t current = m_borrowed_count.load(std::memory_order_acquire);
        size_t peak = m_peak_borrowed.load(std::memory_order_acquire);
        
        while (current > peak)
        {
            if (m_peak_borrowed.compare_exchange_weak(peak, current,
                    std::memory_order_release, std::memory_order_acquire))
                break;
            // peak 已更新，重新读取 current（可能已被其他线程修改）
            current = m_borrowed_count.load(std::memory_order_acquire);
        }
    }

    // ------------------------------------------------------------------------
    // 池管理实现
    // ------------------------------------------------------------------------

    bool preallocate_impl(size_t count)
    {
        size_t created = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (m_max_size > 0 && m_created_count >= m_max_size)
                break;

            T *obj = create_object_safe();
            if (!obj)
            {
                if (m_enable_stats)
                    m_total_create_failures++;
                break;
            }

            m_pool.push({obj, std::chrono::steady_clock::now()});
            m_created_count++;
            m_free_count++;
            created++;
        }

        return created > 0;
    }

    void shrink_to_impl(size_t target_size)
    {
        if (m_pool.size() <= target_size)
            return;

        std::vector<PooledObjectEntry> entries;
        entries.reserve(m_pool.size());
        while (!m_pool.empty())
        {
            entries.push_back(m_pool.front());
            m_pool.pop();
        }

        // 使用 partial_sort 优化：只需要前 target_size 个最新元素
        if (target_size > 0 && target_size < entries.size())
        {
            std::nth_element(entries.begin(), 
                             entries.begin() + static_cast<ptrdiff_t>(target_size), 
                             entries.end(),
                             [](const auto &a, const auto &b) {
                                 return a.last_used > b.last_used;
                             });
        }
        else
        {
            std::sort(entries.begin(), entries.end(),
                      [](const auto &a, const auto &b) {
                          return a.last_used > b.last_used;
                      });
        }

        // 保留最新的 target_size 个
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i < target_size)
            {
                m_pool.push(entries[i]);
            }
            else
            {
                m_deleter(entries[i].obj);
                m_created_count--;
                m_free_count--;
                m_total_destroyed++;
            }
        }
    }

    size_t reap_idle_objects_impl()
    {
        auto now = std::chrono::steady_clock::now();
        size_t reaped = 0;

        std::queue<PooledObjectEntry> new_pool;
        while (!m_pool.empty())
        {
            auto entry = m_pool.front();
            m_pool.pop();

            auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - entry.last_used);
            
            if (idle_time < m_max_idle_time)
            {
                new_pool.push(entry);
            }
            else
            {
                m_deleter(entry.obj);
                m_created_count--;
                m_free_count--;
                m_total_destroyed++;
                reaped++;
            }
        }

        m_pool = std::move(new_pool);
        return reaped;
    }

    // ------------------------------------------------------------------------
    // 后台清理线程
    // ------------------------------------------------------------------------

    void start_cleanup_thread(std::chrono::milliseconds interval)
    {
        if (interval == std::chrono::milliseconds(0))
        {
            interval = m_max_idle_time / 2;
        }

        m_cleanup_running = true;
        m_cleanup_thread = std::thread([this, interval]() {
            while (m_cleanup_running.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(interval);
                if (m_cleanup_running.load(std::memory_order_acquire))
                {
                    reap_idle_objects();
                }
            }
        });
    }

    void stop_cleanup_thread()
    {
        m_cleanup_running.store(false, std::memory_order_release);
        if (m_cleanup_thread.joinable())
        {
            m_cleanup_thread.join();
        }
    }

private:
    // 配置
    std::function<T *()> m_factory;
    std::function<void(T *)> m_resetter;
    std::function<bool(T *)> m_validator;
    std::function<void(T *)> m_deleter;
    size_t m_max_size;
    size_t m_max_waiters;
    std::chrono::milliseconds m_max_idle_time;
    bool m_enable_stats;
    std::function<void(size_t)> m_leak_callback;

    // 池状态
    std::queue<PooledObjectEntry> m_pool;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    // 统计
    std::atomic<size_t> m_created_count{0};
    std::atomic<size_t> m_borrowed_count{0};
    std::atomic<size_t> m_free_count{0};
    std::atomic<size_t> m_peak_borrowed{0};
    std::atomic<size_t> m_total_borrows{0};
    std::atomic<size_t> m_total_returns{0};
    std::atomic<size_t> m_total_destroyed{0};
    std::atomic<size_t> m_total_timeouts{0};
    std::atomic<size_t> m_total_creates_on_borrow{0};
    std::atomic<size_t> m_total_create_failures{0};
    std::atomic<uint64_t> m_total_idle_time_ms{0};
    std::atomic<size_t> m_idle_sample_count{0};
    std::atomic<size_t> m_current_waiters{0};
    std::atomic<size_t> m_peak_waiters{0};

    // 后台清理
    std::atomic<bool> m_cleanup_running{false};
    std::thread m_cleanup_thread;
};

// ============================================================================
// 线程本地对象池
// ============================================================================

template <typename T>
class ThreadLocalObjectPool
{
public:
    explicit ThreadLocalObjectPool(size_t local_size = 8)
        : m_local_size(local_size) {}

    ~ThreadLocalObjectPool()
    {
        clear();
    }

    T *borrow()
    {
        auto &local_pool = get_local_pool();

        if (!local_pool.empty())
        {
            T *obj = local_pool.back();
            local_pool.pop_back();
            return obj;
        }

        return new T();
    }

    void return_object(T *obj)
    {
        if (obj == nullptr)
            return;

        auto &local_pool = get_local_pool();

        if (local_pool.size() < m_local_size)
        {
            if constexpr (std::is_default_constructible_v<T>)
            {
                *obj = T();
            }
            local_pool.push_back(obj);
        }
        else
        {
            delete obj;
        }
    }

    PooledObject<T> borrow_auto()
    {
        T *obj = borrow();
        return PooledObject<T>(obj, [this](T *p) { return_object(p); });
    }

    void clear()
    {
        auto &local_pool = get_local_pool();
        for (T *obj : local_pool)
            delete obj;
        local_pool.clear();
    }

    size_t local_size() const { return m_local_size; }
    void set_local_size(size_t size) { m_local_size = size; }

private:
    static std::vector<T *> &get_local_pool()
    {
        thread_local std::vector<T *> pool;
        return pool;
    }

    size_t m_local_size;
};

// ============================================================================
// 连接池特化示例
// ============================================================================

template <typename Connection>
class ConnectionPool : public ObjectPool<Connection>
{
public:
    using Base = ObjectPool<Connection>;

    struct ConnectionConfig
    {
        std::string host;
        int port = 0;
        std::string username;
        std::string password;
        std::string database;
        std::chrono::seconds connect_timeout{5};
        std::chrono::seconds idle_timeout{60};
        size_t max_connections = 20;
        size_t initial_connections = 5;
        size_t max_waiters = 100;
        bool enable_auto_cleanup = true;
        bool enable_retry = true;
        size_t max_retries = 3;
    };

    explicit ConnectionPool(const ConnectionConfig &cfg)
        : Base(create_config(cfg))
        , m_config(cfg) {}

    // 健康检查
    bool health_check()
    {
        auto conn = this->borrow_auto();
        if (!conn)
            return false;
        
        try
        {
            return conn->is_valid();
        }
        catch (...)
        {
            return false;
        }
    }

    const ConnectionConfig &config() const { return m_config; }

private:
    static typename Base::Config create_config(const ConnectionConfig &cfg)
    {
        typename Base::Config config;
        
        config.factory = [cfg]() -> Connection * {
            std::unique_ptr<Connection> conn(new Connection());
            
            for (size_t retry = 0; retry <= (cfg.enable_retry ? cfg.max_retries : 0); ++retry)
            {
                try
                {
                    if (conn->connect(cfg.host, cfg.port, 
                                      cfg.username, cfg.password, 
                                      cfg.database, cfg.connect_timeout))
                    {
                        return conn.release();
                    }
                }
                catch (...)
                {
                    if (retry == cfg.max_retries)
                        throw;
                }
                
                if (retry < cfg.max_retries)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (retry + 1)));
                }
            }
            
            return nullptr;
        };
        
        config.resetter = [](Connection *conn) {
            if (conn)
            {
                try
                {
                    conn->reset();
                }
                catch (...) {}
            }
        };
        
        config.validator = [](Connection *conn) {
            if (!conn)
                return false;
            try
            {
                return conn->is_valid();
            }
            catch (...)
            {
                return false;
            }
        };
        
        config.deleter = [](Connection *conn) {
            if (conn)
            {
                try
                {
                    conn->disconnect();
                }
                catch (...) {}
                delete conn;
            }
        };
        
        config.initial_size = cfg.initial_connections;
        config.max_size = cfg.max_connections;
        config.max_waiters = cfg.max_waiters;
        config.max_idle_time = cfg.idle_timeout;
        config.enable_auto_cleanup = cfg.enable_auto_cleanup;
        
        config.leak_callback = [](size_t leaked) {
            std::cerr << "WARNING: ConnectionPool destroyed with " 
                      << leaked << " connections still borrowed\n";
        };
        
        return config;
    }

    ConnectionConfig m_config;
};

#endif // OBJECT_POOL_HPP