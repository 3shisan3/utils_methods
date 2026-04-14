/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        threadpool.hpp
Version:     2.0
Author:      cjx
start date: 2024-12-31
Description: 跨平台的线程池，C++17实现，提供相对完整的接口
             借鉴双队列无锁交换机制，优化调度性能
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1            2025-01-01       cjx         create
2            2025-04-14       cjx         线程增减逻辑，借鉴C版本双队列设计

*****************************************************************/

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __cpp_lib_jthread
#include <stop_token>
#endif

// ============================================================================
// 配置标志
// ============================================================================

enum class tp_flag : uint8_t
{
    none = 0,
    priority = 1 << 0,      // 启用任务优先级
    pause = 1 << 1,         // 启用暂停功能
    deadlock_check = 1 << 2 // 启用死锁检测
};

constexpr tp_flag operator|(tp_flag a, tp_flag b) noexcept
{
    return static_cast<tp_flag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr tp_flag operator&(tp_flag a, tp_flag b) noexcept
{
    return static_cast<tp_flag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool has_flag(tp_flag value, tp_flag flag) noexcept
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// 异常定义
// ============================================================================

class threadpool_error : public std::runtime_error
{
public:
    explicit threadpool_error(const std::string& msg) : std::runtime_error(msg) {}
};

class deadlock_detected : public threadpool_error
{
public:
    deadlock_detected() : threadpool_error("Deadlock detected in thread pool operation") {}
};

// ============================================================================
// 优先级定义
// ============================================================================

using priority_t = int8_t;

enum class pr : priority_t
{
    lowest = -128,
    low = -64,
    normal = 0,
    high = 64,
    highest = 127
};

// ============================================================================
// move_only_function 兼容 (C++17)
// ============================================================================

#ifdef __cpp_lib_move_only_function
template <typename T>
using move_only_function = std::move_only_function<T>;
#else
template <typename>
class move_only_function;

template <typename R, typename... Args>
class move_only_function<R(Args...)>
{
public:
    move_only_function() noexcept = default;
    move_only_function(move_only_function &&) noexcept = default;
    move_only_function &operator=(move_only_function &&) noexcept = default;

    move_only_function(const move_only_function &) = delete;
    move_only_function &operator=(const move_only_function &) = delete;

    ~move_only_function() = default;

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, move_only_function> &&
                  std::is_invocable_r_v<R, F &, Args...>>>
    move_only_function(F &&f)
        : ptr(std::make_unique<model<std::decay_t<F>>>(std::forward<F>(f))) {}

    R operator()(Args... args)
    {
        if (!ptr)
            throw std::bad_function_call();
        return ptr->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept
    {
        return ptr != nullptr;
    }

    void reset() noexcept
    {
        ptr.reset();
    }

private:
    struct concept
    {
        virtual ~concept() = default;
        virtual R call(Args... args) = 0;
    };

    template <typename F>
    struct model final : concept
    {
        explicit model(F &&f) : func(std::forward<F>(f)) {}
        R call(Args... args) override
        {
            if constexpr (std::is_void_v<R>)
            {
                std::invoke(func, std::forward<Args>(args)...);
            }
            else
            {
                return std::invoke(func, std::forward<Args>(args)...);
            }
        }
        F func;
    };

    std::unique_ptr<concept> ptr;
};
#endif

// ============================================================================
// 优先级任务包装
// ============================================================================

template <bool PriorityEnabled>
struct task_wrapper;

template <>
struct task_wrapper<true>
{
    move_only_function<void()> task;
    priority_t priority;

    task_wrapper() = default;
    task_wrapper(move_only_function<void()> &&t, priority_t p = 0)
        : task(std::move(t)), priority(p) {}

    // 优先级队列需要 operator<，高优先级先出队
    bool operator<(const task_wrapper &other) const
    {
        return priority < other.priority;
    }
};

template <>
struct task_wrapper<false>
{
    move_only_function<void()> task;

    task_wrapper() = default;
    task_wrapper(move_only_function<void()> &&t, priority_t = 0)
        : task(std::move(t)) {}
};

// ============================================================================
// 特殊任务类型（用于线程控制）
// ============================================================================

enum class special_task_type : uint8_t
{
    none = 0,
    exit_thread,    // 线程退出任务
    decrease_done   // 减少线程完成通知
};

// ============================================================================
// 任务与 Future 辅助类
// ============================================================================

template <typename R>
struct task_with_future
{
    template <typename F>
    explicit task_with_future(F &&func)
    {
        std::promise<R> prom;
        future = prom.get_future();
        task = [f = std::forward<F>(func), p = std::move(prom)]() mutable
        {
            try
            {
                if constexpr (std::is_void_v<R>)
                {
                    f();
                    p.set_value();
                }
                else
                {
                    p.set_value(f());
                }
            }
            catch (...)
            {
                p.set_exception(std::current_exception());
            }
        };
    }

    std::future<R> future;
    move_only_function<void()> task;
};

// ============================================================================
// multi_future 辅助类
// ============================================================================

template <typename T>
class multi_future : public std::vector<std::future<T>>
{
public:
    using std::vector<std::future<T>>::vector;

    [[nodiscard]] std::conditional_t<std::is_void_v<T>, void, std::vector<T>> get()
    {
        if constexpr (std::is_void_v<T>)
        {
            for (auto &f : *this)
                f.get();
        }
        else
        {
            std::vector<T> results;
            results.reserve(this->size());
            for (auto &f : *this)
                results.push_back(f.get());
            return results;
        }
    }

    [[nodiscard]] std::size_t ready_count() const
    {
        std::size_t count = 0;
        for (const auto &f : *this)
        {
            if (f.wait_for(std::chrono::seconds::zero()) == std::future_status::ready)
                ++count;
        }
        return count;
    }

    void wait() const
    {
        for (const auto &f : *this)
            f.wait();
    }

    template <typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period> &duration) const
    {
        auto start = std::chrono::steady_clock::now();
        for (const auto &f : *this)
        {
            auto remaining = duration - (std::chrono::steady_clock::now() - start);
            if (remaining <= std::chrono::duration<Rep, Period>::zero())
                return false;
            if (f.wait_for(remaining) == std::future_status::timeout)
                return false;
        }
        return true;
    }
};

// ============================================================================
// 块划分辅助类
// ============================================================================

template <typename T>
class block_range
{
public:
    block_range(T first, T last, std::size_t num_blocks) noexcept
        : m_first(first)
        , m_last(last)
        , m_num_blocks(0)
    {
        if (last > first)
        {
            std::size_t total = static_cast<std::size_t>(last - first);
            num_blocks = std::min(num_blocks, total);
            m_block_size = total / num_blocks;
            m_remainder = total % num_blocks;
            m_num_blocks = (m_block_size == 0) ? total : num_blocks;
        }
    }

    [[nodiscard]] T start(std::size_t block) const noexcept
    {
        return m_first + static_cast<T>(block * m_block_size) +
               static_cast<T>(block < m_remainder ? block : m_remainder);
    }

    [[nodiscard]] T end(std::size_t block) const noexcept
    {
        return (block == m_num_blocks - 1) ? m_last : start(block + 1);
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_num_blocks; }

    [[nodiscard]] bool empty() const noexcept { return m_num_blocks == 0; }

private:
    T m_first, m_last;
    std::size_t m_block_size = 0;
    std::size_t m_remainder = 0;
    std::size_t m_num_blocks = 0;
};

// ============================================================================
// 当前线程信息
// ============================================================================

class this_thread_info
{
public:
    [[nodiscard]] static std::optional<std::size_t> get_index() noexcept
    {
        return s_index;
    }

    [[nodiscard]] static std::optional<void *> get_pool() noexcept
    {
        return s_pool;
    }

private:
    template <tp_flag>
    friend class ThreadPool;

    static inline thread_local std::optional<std::size_t> s_index = std::nullopt;
    static inline thread_local std::optional<void *> s_pool = std::nullopt;
};

// ============================================================================
// 双队列任务管理器（借鉴 C 版本的 __taskqueue_swap 设计）
// ============================================================================

template <bool PriorityEnabled>
class dual_task_queue
{
    using task_type = task_wrapper<PriorityEnabled>;
    using queue_type = std::conditional_t<
        PriorityEnabled,
        std::priority_queue<task_type>,
        std::queue<task_type>>;

public:
    dual_task_queue() = default;

    // 生产者端：放入任务
    void put(task_type &&task)
    {
        std::lock_guard<std::mutex> lock(m_put_mutex);
        put_impl(std::move(task));
    }

    // 生产者端：放入任务到头部（用于特殊任务）
    void put_front(task_type &&task)
    {
        std::lock_guard<std::mutex> lock(m_put_mutex);
        put_front_impl(std::move(task));
    }

    // 消费者端：获取任务（批量交换后从消费者队列获取）
    std::optional<task_type> get()
    {
        std::lock_guard<std::mutex> lock(m_get_mutex);
        
        // 如果消费者队列为空，尝试与生产者队列交换
        if (consumer_empty())
        {
            swap_queues();
        }
        
        if (consumer_empty())
            return std::nullopt;
            
        return take_from_consumer();
    }

    // 非阻塞获取（不等待生产者）
    std::optional<task_type> try_get()
    {
        std::lock_guard<std::mutex> lock(m_get_mutex);
        
        if (consumer_empty())
        {
            // 尝试交换，但不阻塞等待
            if (!try_swap_queues())
                return std::nullopt;
        }
        
        if (consumer_empty())
            return std::nullopt;
            
        return take_from_consumer();
    }

    // 获取队列大小（近似值）
    size_t approximate_size() const
    {
        std::lock_guard<std::mutex> lock1(m_put_mutex);
        std::lock_guard<std::mutex> lock2(m_get_mutex);
        return producer_size() + consumer_size();
    }

    // 清空所有队列
    void clear()
    {
        std::lock_guard<std::mutex> lock1(m_put_mutex);
        std::lock_guard<std::mutex> lock2(m_get_mutex);
        clear_producer();
        clear_consumer();
    }

    // 通知生产者有新空间（用于条件变量）
    void notify_producer()
    {
        m_put_cv.notify_one();
    }

    void notify_all_producers()
    {
        m_put_cv.notify_all();
    }

    // 等待生产者队列有任务
    template <typename Predicate>
    void wait_for_task(std::unique_lock<std::mutex> &lock, Predicate pred)
    {
        m_get_cv.wait(lock, pred);
    }

    template <typename Rep, typename Period, typename Predicate>
    bool wait_for_task(std::unique_lock<std::mutex> &lock,
                       const std::chrono::duration<Rep, Period> &timeout,
                       Predicate pred)
    {
        return m_get_cv.wait_for(lock, timeout, pred);
    }

    void notify_consumer()
    {
        m_get_cv.notify_one();
    }

    void notify_all_consumers()
    {
        m_get_cv.notify_all();
    }

    // 获取锁（用于外部条件变量同步）
    std::mutex &put_mutex() { return m_put_mutex; }
    std::mutex &get_mutex() { return m_get_mutex; }

private:
    // 生产者端实现
    void put_impl(task_type &&task)
    {
        if constexpr (PriorityEnabled)
        {
            m_producer_queue.push(std::move(task));
        }
        else
        {
            m_producer_queue.push(std::move(task));
        }
        ++m_producer_size;
    }

    void put_front_impl(task_type &&task)
    {
        // 对于优先级队列，无法直接放入头部
        // 这里简单处理：放入并依赖优先级
        put_impl(std::move(task));
    }

    // 交换队列（核心优化：O(1) 交换而非逐个移动）
    void swap_queues()
    {
        std::lock_guard<std::mutex> lock(m_put_mutex);
        
        // 交换生产者队列和消费者队列
        std::swap(m_consumer_queue, m_producer_queue);
        std::swap(m_consumer_size, m_producer_size);
        
        // 通知等待的生产者
        if (m_producer_size == 0)
            m_put_cv.notify_all();
    }

    bool try_swap_queues()
    {
        std::unique_lock<std::mutex> lock(m_put_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
            
        std::swap(m_consumer_queue, m_producer_queue);
        std::swap(m_consumer_size, m_producer_size);
        
        if (m_producer_size == 0)
            m_put_cv.notify_all();
            
        return true;
    }

    std::optional<task_type> take_from_consumer()
    {
        if (consumer_empty())
            return std::nullopt;
            
        task_type task = std::move(const_cast<task_type&>(top_consumer()));
        pop_consumer();
        --m_consumer_size;
        return task;
    }

    // 队列操作辅助函数
    bool consumer_empty() const { return m_consumer_size == 0; }
    size_t consumer_size() const { return m_consumer_size; }
    size_t producer_size() const { return m_producer_size; }

    const task_type& top_consumer() const
    {
        if constexpr (PriorityEnabled)
            return m_consumer_queue.top();
        else
            return m_consumer_queue.front();
    }

    void pop_consumer()
    {
        if constexpr (PriorityEnabled)
            m_consumer_queue.pop();
        else
            m_consumer_queue.pop();
    }

    void clear_producer()
    {
        if constexpr (PriorityEnabled)
            m_producer_queue = std::priority_queue<task_type>();
        else
            m_producer_queue = std::queue<task_type>();
        m_producer_size = 0;
    }

    void clear_consumer()
    {
        if constexpr (PriorityEnabled)
            m_consumer_queue = std::priority_queue<task_type>();
        else
            m_consumer_queue = std::queue<task_type>();
        m_consumer_size = 0;
    }

    // 成员变量
    mutable std::mutex m_put_mutex;
    mutable std::mutex m_get_mutex;
    std::condition_variable m_put_cv;
    std::condition_variable m_get_cv;
    
    queue_type m_producer_queue;
    queue_type m_consumer_queue;
    
    size_t m_producer_size = 0;
    size_t m_consumer_size = 0;
};

// ============================================================================
// 主线程池类
// ============================================================================

template <tp_flag Flags = tp_flag::none>
class ThreadPool
{
public:
    static constexpr bool priority_enabled = has_flag(Flags, tp_flag::priority);
    static constexpr bool pause_enabled = has_flag(Flags, tp_flag::pause);
    static constexpr bool deadlock_check_enabled = has_flag(Flags, tp_flag::deadlock_check);

    // ========================================================================
    // 构造与析构
    // ========================================================================

    ThreadPool() : ThreadPool(0, [](std::size_t) {}) {}

    explicit ThreadPool(std::size_t num_threads)
        : ThreadPool(num_threads, [](std::size_t) {}) {}

    template <typename F>
    explicit ThreadPool(F &&init)
        : ThreadPool(0, std::forward<F>(init)) {}

    template <typename F>
    ThreadPool(std::size_t num_threads, F &&init)
    {
        create_threads(num_threads, std::forward<F>(init));
    }

    ~ThreadPool() noexcept
    {
        wait();
        stop_threads();
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    // ========================================================================
    // 任务提交 - detach (不等待结果)
    // ========================================================================

    template <typename F>
    void detach(F &&task, priority_t priority = 0)
    {
        check_deadlock("detach");
        enqueue_task(std::forward<F>(task), priority);
    }

    template <typename T1, typename T2, typename F>
    void detach_blocks(T1 first, T2 last, F &&block,
                       std::size_t num_blocks = 0, priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        enqueue_blocks<T>(static_cast<T>(first), static_cast<T>(last),
                          std::forward<F>(block), num_blocks, priority,
                          std::false_type{});
    }

    template <typename T1, typename T2, typename F>
    void detach_loop(T1 first, T2 last, F &&loop,
                     std::size_t num_blocks = 0, priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        auto wrapper = [loop = std::forward<F>(loop)](T start, T end)
        {
            for (T i = start; i < end; ++i)
                loop(i);
        };
        detach_blocks(first, last, std::move(wrapper), num_blocks, priority);
    }

    template <typename T1, typename T2, typename F>
    void detach_sequence(T1 first, T2 last, F &&seq, priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        for (T i = static_cast<T>(first); i < static_cast<T>(last); ++i)
        {
            detach([seq, i]() { seq(i); }, priority);
        }
    }

    template <typename Iterator>
    void detach_bulk(Iterator first, Iterator last, priority_t priority = 0)
    {
        for (auto it = first; it != last; ++it)
        {
            detach(*it, priority);
        }
    }

    // ========================================================================
    // 任务提交 - submit (等待结果)
    // ========================================================================

    template <typename F>
    [[nodiscard]] auto submit(F &&task, priority_t priority = 0)
    {
        check_deadlock("submit");
        using R = std::invoke_result_t<std::decay_t<F>>;
        task_with_future<R> twf(std::forward<F>(task));
        std::future<R> future = std::move(twf.future);
        enqueue_task(std::move(twf.task), priority);
        return future;
    }

    template <typename T1, typename T2, typename F, typename R = std::invoke_result_t<F, T1, T2>>
    [[nodiscard]] multi_future<R> submit_blocks(T1 first, T2 last, F &&block,
                                                std::size_t num_blocks = 0,
                                                priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        return enqueue_blocks<T>(static_cast<T>(first), static_cast<T>(last),
                                 std::forward<F>(block), num_blocks, priority,
                                 std::true_type{});
    }

    template <typename T1, typename T2, typename F>
    [[nodiscard]] multi_future<void> submit_loop(T1 first, T2 last, F &&loop,
                                                 std::size_t num_blocks = 0,
                                                 priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        auto wrapper = [loop = std::forward<F>(loop)](T start, T end)
        {
            for (T i = start; i < end; ++i)
                loop(i);
        };
        return submit_blocks(first, last, std::move(wrapper), num_blocks, priority);
    }

    template <typename T1, typename T2, typename F, typename R = std::invoke_result_t<F, T1>>
    [[nodiscard]] multi_future<R> submit_sequence(T1 first, T2 last, F &&seq,
                                                  priority_t priority = 0)
    {
        using T = std::common_type_t<T1, T2>;
        std::vector<std::future<R>> futures;
        futures.reserve(static_cast<std::size_t>(last - first));
        for (T i = static_cast<T>(first); i < static_cast<T>(last); ++i)
        {
            futures.push_back(submit([seq, i]() { return seq(i); }, priority));
        }
        return multi_future<R>(std::move(futures));
    }

    template <typename Iterator>
    [[nodiscard]] auto submit_bulk(Iterator first, Iterator last, priority_t priority = 0)
    {
        using F = decltype(*first);
        using R = std::invoke_result_t<std::decay_t<F>>;
        std::vector<std::future<R>> futures;
        futures.reserve(static_cast<std::size_t>(std::distance(first, last)));
        for (auto it = first; it != last; ++it)
        {
            futures.push_back(submit(*it, priority));
        }
        return multi_future<R>(std::move(futures));
    }

    // ========================================================================
    // 线程管理
    // ========================================================================

    [[nodiscard]] std::size_t thread_count() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_threads.size();
    }

    [[nodiscard]] std::size_t active_threads() const noexcept
    {
        return m_active_threads.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t queued_tasks() const
    {
        return m_task_queue.approximate_size();
    }

    void purge()
    {
        m_task_queue.clear();
    }

    void wait()
    {
        check_deadlock("wait");
        std::unique_lock<std::mutex> lock(m_mutex);
        m_done_cv.wait(lock, [this] {
            if constexpr (pause_enabled)
                return m_active_threads == 0 && (m_paused || m_task_queue.approximate_size() == 0);
            else
                return m_active_threads == 0 && m_task_queue.approximate_size() == 0;
        });
    }

    template <typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period> &duration)
    {
        check_deadlock("wait_for");
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_done_cv.wait_for(lock, duration, [this] {
            if constexpr (pause_enabled)
                return m_active_threads == 0 && (m_paused || m_task_queue.approximate_size() == 0);
            else
                return m_active_threads == 0 && m_task_queue.approximate_size() == 0;
        });
    }

    // ========================================================================
    // 动态调整线程数（借鉴 C 版本的设计）
    // ========================================================================

    bool increase_threads(std::size_t count = 1)
    {
        if (count == 0)
            return true;
            
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 检查是否已停止
        if (m_stop.load(std::memory_order_acquire))
            return false;
            
        return add_threads_impl(count);
    }

    bool decrease_threads(std::size_t count = 1)
    {
        if (count == 0)
            return true;
            
        std::unique_lock<std::mutex> lock(m_mutex);
        
        std::size_t current = m_threads.size();
        if (count >= current)
            return false;
            
        // 借鉴 C 版本：向队列头部插入特殊退出任务
        m_pending_exits += count;
        
        for (std::size_t i = 0; i < count; ++i)
        {
            // 创建特殊退出任务
            task_wrapper<priority_enabled> exit_task;
            exit_task.task = [this]() {
                // 退出任务：线程将检测到并退出
            };
            // 标记为特殊任务
            m_task_queue.put_front(std::move(exit_task));
        }
        
        m_task_queue.notify_all_consumers();
        
        // 等待线程退出
        m_exit_cv.wait(lock, [this, count] {
            return m_exited_count >= count;
        });
        
        m_exited_count -= count;
        m_pending_exits -= count;
        
        // 清理已退出的线程对象
        cleanup_exited_threads();
        
        return true;
    }

    void stop()
    {
        m_stop.store(true, std::memory_order_release);
        m_task_queue.notify_all_consumers();
    }

    // ========================================================================
    // 暂停控制 (仅当 pause_enabled)
    // ========================================================================

    template <bool P = pause_enabled, typename = std::enable_if_t<P>>
    void pause()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = true;
    }

    template <bool P = pause_enabled, typename = std::enable_if_t<P>>
    void unpause()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_paused = false;
        }
        m_task_queue.notify_all_consumers();
    }

    template <bool P = pause_enabled, typename = std::enable_if_t<P>>
    [[nodiscard]] bool is_paused() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_paused;
    }

    // ========================================================================
    // 重置
    // ========================================================================

    void reset()
    {
        reset(0, [](std::size_t) {});
    }

    void reset(std::size_t num_threads)
    {
        reset(num_threads, [](std::size_t) {});
    }

    template <typename F>
    void reset(F &&init)
    {
        reset(0, std::forward<F>(init));
    }

    template <typename F>
    void reset(std::size_t num_threads, F &&init)
    {
        wait();
        stop_threads();
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_threads.clear();
            m_stop.store(false, std::memory_order_release);
            m_pending_exits = 0;
            m_exited_count = 0;
            if constexpr (pause_enabled)
                m_paused = false;
        }
        
        create_threads(num_threads, std::forward<F>(init));
    }

private:
    // ========================================================================
    // 内部实现
    // ========================================================================

    using task_queue_t = dual_task_queue<priority_enabled>;

    void check_deadlock(const char* operation) const
    {
        if constexpr (deadlock_check_enabled)
        {
            if (this_thread_info::get_pool() == static_cast<const void*>(this))
            {
                throw deadlock_detected();
            }
        }
    }

    template <typename F>
    void enqueue_task(F &&task, priority_t priority)
    {
        if (m_stop.load(std::memory_order_acquire))
            return;
            
        m_task_queue.put(task_wrapper<priority_enabled>(
            move_only_function<void()>(std::forward<F>(task)), priority));
        m_task_queue.notify_consumer();
    }

    template <typename T, typename F, typename R, bool Submit>
    auto enqueue_blocks(T first, T last, F &&block,
                        std::size_t num_blocks, priority_t priority,
                        std::integral_constant<bool, Submit>)
    {
        using result_t = std::conditional_t<Submit, multi_future<R>, void>;
        
        if (last <= first)
            return result_t();
            
        std::size_t nblocks = num_blocks ? num_blocks : thread_count();
        block_range<T> range(first, last, nblocks);
        nblocks = range.size();
        
        if (nblocks == 0)
            return result_t();

        std::vector<std::conditional_t<Submit, std::future<R>, move_only_function<void()>>> items;
        items.reserve(nblocks);

        auto block_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(block));

        for (std::size_t i = 0; i < nblocks; ++i)
        {
            auto start = range.start(i);
            auto end = range.end(i);
            if constexpr (Submit)
            {
                items.push_back(submit([block_ptr, start, end]() -> R
                                       { return (*block_ptr)(start, end); }, priority));
            }
            else
            {
                items.emplace_back([block_ptr, start, end]()
                                   { (*block_ptr)(start, end); });
                enqueue_task(std::move(items.back()), priority);
            }
        }

        if constexpr (Submit)
        {
            return multi_future<R>(std::move(items));
        }
        return result_t();
    }

    template <typename F>
    void create_threads(std::size_t num_threads, F &&init)
    {
        if constexpr (std::is_invocable_v<F, std::size_t>)
        {
            m_init_func = std::forward<F>(init);
        }
        else
        {
            m_init_func = [init = std::forward<F>(init)](std::size_t)
            { init(); };
        }

        std::size_t n = determine_thread_count(num_threads);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threads.reserve(m_threads.size() + n);
        
        for (std::size_t i = 0; i < n; ++i)
        {
            std::size_t idx = m_threads.size();
#ifdef __cpp_lib_jthread
            m_threads.emplace_back([this, idx](const std::stop_token &st)
                                   { worker(st, idx); });
#else
            m_threads.emplace_back([this, idx]
                                   { worker(idx); });
#endif
        }
    }

    bool add_threads_impl(std::size_t count)
    {
        m_threads.reserve(m_threads.size() + count);
        
        for (std::size_t i = 0; i < count; ++i)
        {
            std::size_t idx = m_threads.size();
#ifdef __cpp_lib_jthread
            m_threads.emplace_back([this, idx](const std::stop_token &st)
                                   { worker(st, idx); });
#else
            m_threads.emplace_back([this, idx]
                                   { worker(idx); });
#endif
        }
        
        return true;
    }

    void cleanup_exited_threads()
    {
        // 移除已 join 的线程
        m_threads.erase(
            std::remove_if(m_threads.begin(), m_threads.end(),
                           [](auto& t) {
#ifdef __cpp_lib_jthread
                               return !t.joinable();
#else
                               if (t.joinable())
                                   return false;
                               t.join();
                               return true;
#endif
                           }),
            m_threads.end());
    }

    void stop_threads()
    {
        m_stop.store(true, std::memory_order_release);
        m_task_queue.notify_all_consumers();
        
        // 等待所有线程退出
        for (auto& t : m_threads)
        {
            if (t.joinable())
                t.join();
        }
    }

#ifndef __cpp_lib_jthread
    void destroy_threads()
    {
        m_stop.store(true, std::memory_order_release);
        m_task_queue.notify_all_consumers();
        
        for (auto &t : m_threads)
        {
            if (t.joinable())
                t.join();
        }
    }
#endif

    [[nodiscard]] static std::size_t determine_thread_count(std::size_t requested)
    {
        if (requested > 0)
            return requested;
        std::size_t hw = std::thread::hardware_concurrency();
        return hw > 0 ? hw : 1;
    }

    void worker(
#ifdef __cpp_lib_jthread
        const std::stop_token &stop_token,
#endif
        std::size_t index)
    {
        // 设置线程本地信息
        this_thread_info::s_pool = static_cast<void*>(this);
        this_thread_info::s_index = index;
        
        // 执行初始化函数
        try
        {
            m_init_func(index);
        }
        catch (...)
        {
            // 初始化异常，线程退出
            this_thread_info::s_index = std::nullopt;
            this_thread_info::s_pool = std::nullopt;
            return;
        }

        while (true)
        {
            // 检查是否需要退出
#ifdef __cpp_lib_jthread
            if (stop_token.stop_requested() || m_stop.load(std::memory_order_acquire))
#else
            if (m_stop.load(std::memory_order_acquire))
#endif
            {
                break;
            }
            
            // 检查是否有待处理的退出请求
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_pending_exits > 0)
                {
                    --m_pending_exits;
                    ++m_exited_count;
                    m_exit_cv.notify_one();
                    break;
                }
            }

            // 等待任务
            std::optional<task_wrapper<priority_enabled>> opt_task;
            
            {
                std::unique_lock<std::mutex> lock(m_task_queue.get_mutex());
                
                // 等待条件
                auto has_task = [this] {
                    if constexpr (pause_enabled)
                    {
                        return m_stop.load(std::memory_order_acquire) || 
                               (!m_paused && m_task_queue.approximate_size() > 0) ||
                               m_pending_exits > 0;
                    }
                    else
                    {
                        return m_stop.load(std::memory_order_acquire) || 
                               m_task_queue.approximate_size() > 0 ||
                               m_pending_exits > 0;
                    }
                };
                
                m_task_queue.wait_for_task(lock, has_task);
                
                // 再次检查退出条件
#ifdef __cpp_lib_jthread
                if (stop_token.stop_requested() || m_stop.load(std::memory_order_acquire))
#else
                if (m_stop.load(std::memory_order_acquire))
#endif
                {
                    break;
                }
                
                // 检查暂停状态
                if constexpr (pause_enabled)
                {
                    if (m_paused)
                        continue;
                }
                
                // 尝试获取任务
                opt_task = m_task_queue.try_get();
            }

            if (opt_task)
            {
                // 增加活跃线程计数
                m_active_threads.fetch_add(1, std::memory_order_release);
                
                // 执行任务
#ifdef __cpp_exceptions
                try
                {
#endif
                    opt_task->task();
#ifdef __cpp_exceptions
                }
                catch (...)
                {
                    // 异常已在 task_with_future 中处理
                    // 这里吞没是为了防止线程崩溃
                }
#endif
                
                // 减少活跃线程计数
                m_active_threads.fetch_sub(1, std::memory_order_release);
                
                // 通知等待者
                if (m_active_threads.load(std::memory_order_acquire) == 0)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_done_cv.notify_all();
                }
            }
        }

        // 清理工作
        if (m_cleanup_func)
        {
            try
            {
                m_cleanup_func(index);
            }
            catch (...)
            {
                // 忽略清理异常
            }
        }
        
        this_thread_info::s_index = std::nullopt;
        this_thread_info::s_pool = std::nullopt;
    }

    // ========================================================================
    // 成员变量
    // ========================================================================

    mutable std::mutex m_mutex;
    std::condition_variable m_done_cv;
    std::condition_variable m_exit_cv;

    task_queue_t m_task_queue;
    
    std::atomic<size_t> m_active_threads{0};
    std::atomic<bool> m_stop{false};
    
    std::vector<
#ifdef __cpp_lib_jthread
        std::jthread
#else
        std::thread
#endif
    > m_threads;
    
    // 动态线程管理
    std::atomic<size_t> m_pending_exits{0};
    std::atomic<size_t> m_exited_count{0};

    move_only_function<void(std::size_t)> m_init_func = [](std::size_t) {};
    move_only_function<void(std::size_t)> m_cleanup_func = [](std::size_t) {};

    std::conditional_t<pause_enabled, bool, std::monostate> m_paused = {};
};

// ============================================================================
// 便捷类型别名
// ============================================================================

using DefaultThreadPool = ThreadPool<tp_flag::none>;
using PriorityThreadPool = ThreadPool<tp_flag::priority>;
using PauseThreadPool = ThreadPool<tp_flag::pause>;
using SafeThreadPool = ThreadPool<tp_flag::priority | tp_flag::pause | tp_flag::deadlock_check>;

#endif // THREADPOOL_HPP