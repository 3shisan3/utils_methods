/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        threadpool.hpp
Version:     1.0
Author:      cjx
start date: 2024-12-31
Description: 跨平台的线程池，c++实现，提供相对完整的接口
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1            2025-01-01       cjx         create

*****************************************************************/

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================================
// 跨平台检测宏
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define THREADPOOL_WINDOWS
    #include <windows.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    #define THREADPOOL_POSIX
    #include <pthread.h>
    #include <sched.h>
    #include <unistd.h>
    #include <sys/resource.h>
    #ifdef __linux__
        #include <sys/syscall.h>
    #endif
#else
    #error "Unsupported platform"
#endif

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
// 兼容 C++17 的 move_only_function 实现
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
        return ptr->call(std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept
    {
        return ptr != nullptr;
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
// 任务结构
// ============================================================================

struct PrioritizedTask
{
    move_only_function<void()> task;
    priority_t priority;

    PrioritizedTask() = default;

    PrioritizedTask(move_only_function<void()> &&t, priority_t p = 0)
        : task(std::move(t)), priority(p) {}

    // 注意：priority_queue 默认使用 std::less，所以我们需要定义 < 运算符
    // 优先级高的任务应该被认为"更小"，这样 priority_queue 的 top() 返回优先级最高的
    bool operator<(const PrioritizedTask &other) const
    {
        return priority < other.priority;
    }

    operator move_only_function<void()>() &&
    {
        return std::move(task);
    }
};

// ============================================================================
// 配置结构体
// ============================================================================

struct ThreadPoolConfig
{
    size_t num_threads = 0;       // 0 = 自动检测
    bool enable_priority = true;  // 是否启用优先级队列
    size_t queue_max_size = 0;    // 0 = 无限制
    size_t thread_stack_size = 0; // 0 = 默认
    bool auto_join = true;        // 析构时自动等待

    // 线程优先级设置（可选）
    priority_t default_priority = 0;

    // 平台特定
    bool use_native_affinity = false; // 是否使用 CPU 亲和性
};

// ============================================================================
// 线程池主类
// ============================================================================

class ThreadPool
{
public:
    // 使用外部定义的配置类型
    using Config = ThreadPoolConfig;

    // ========================================================================
    // 构造/析构
    // ========================================================================

    explicit ThreadPool(const Config &cfg = Config());
    ~ThreadPool();

    // 禁止拷贝/移动
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    // ========================================================================
    // 任务提交
    // ========================================================================

    /**
     * @brief 提交任务到线程池
     * @param f 可调用对象
     * @param args 参数
     * @param priority 任务优先级
     * @return future 获取返回值
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args, priority_t priority = 0)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {

        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);

            if (m_stop)
            {
                throw std::runtime_error("ThreadPool: submit on stopped pool");
            }

            auto wrapper = [task]()
            { (*task)(); };

            if (m_enable_priority)
            {
                m_priority_tasks.emplace(std::move(wrapper), priority);
            }
            else
            {
                m_normal_tasks.push(std::move(wrapper));
            }
        }

        m_condition.notify_one();
        return result;
    }

    /**
     * @brief 提交 void 返回类型的任务
     */
    template <typename F, typename... Args>
    void detach(F &&f, Args &&...args, priority_t priority = 0)
    {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);

            if (m_stop)
                return;

            if (m_enable_priority)
            {
                m_priority_tasks.emplace(std::move(task), priority);
            }
            else
            {
                m_normal_tasks.push(std::move(task));
            }
        }
        m_condition.notify_one();
    }

    // ========================================================================
    // 批量操作
    // ========================================================================

    /**
     * @brief 并行循环
     */
    template <typename Index, typename Func>
    void parallel_for(Index first, Index last, Func &&func,
                      size_t num_blocks = 0, priority_t priority = 0)
    {
        if (last <= first)
            return;

        size_t n = num_blocks > 0 ? num_blocks : m_thread_count;
        size_t total = static_cast<size_t>(last - first);
        size_t block_size = total / n;
        size_t remainder = total % n;

        std::vector<std::future<void>> futures;
        futures.reserve(n);

        Index start = first;
        for (size_t i = 0; i < n; ++i)
        {
            Index end = start + static_cast<Index>(block_size);
            if (i < remainder)
                ++end;

            futures.push_back(submit([func, start, end]() {
                for (Index j = start; j < end; ++j)
                {
                    func(j);
                }
            }, priority));

            start = end;
        }

        for (auto &f : futures)
        {
            f.wait();
        }
    }

    /**
     * @brief 批量提交任务
     */
    template <typename Iterator>
    auto submit_bulk(Iterator first, Iterator last, priority_t priority = 0)
        -> std::vector<std::future<typename std::result_of<decltype (*first)()>::type>>
    {

        using return_type = typename std::result_of<decltype (*first)()>::type;
        std::vector<std::future<return_type>> results;
        results.reserve(std::distance(first, last));

        for (auto it = first; it != last; ++it)
        {
            results.push_back(submit(*it, priority));
        }

        return results;
    }

    // ========================================================================
    // 线程管理
    // ========================================================================

    /**
     * @brief 获取线程数
     */
    size_t thread_count() const noexcept { return m_thread_count; }

    /**
     * @brief 获取队列中等待的任务数
     */
    size_t queued_tasks() const
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_enable_priority)
        {
            return m_priority_tasks.size();
        }
        else
        {
            return m_normal_tasks.size();
        }
    }

    /**
     * @brief 获取正在运行的任务数
     */
    size_t running_tasks() const
    {
        return m_tasks_running.load(std::memory_order_acquire);
    }

    /**
     * @brief 增加线程
     */
    bool increase_threads(size_t count = 1);

    /**
     * @brief 减少线程
     */
    bool decrease_threads(size_t count = 1);

    /**
     * @brief 清空任务队列
     */
    void clear_queue()
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_enable_priority)
        {
            while (!m_priority_tasks.empty())
            {
                m_priority_tasks.pop();
            }
        }
        else
        {
            while (!m_normal_tasks.empty())
            {
                m_normal_tasks.pop();
            }
        }
    }

    /**
     * @brief 等待所有任务完成
     */
    void wait()
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_waiting = true;
        m_done_condition.wait(lock, [this] {
            return m_tasks_running == 0 && tasks_empty();
        });
        m_waiting = false;
    }

    /**
     * @brief 带超时的等待
     */
    template <typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period> &duration)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_waiting = true;
        bool result = m_done_condition.wait_for(lock, duration, [this] {
            return m_tasks_running == 0 && tasks_empty();
        });
        m_waiting = false;
        return result;
    }

    /**
     * @brief 停止线程池（不再接受新任务，完成现有任务后退出）
     */
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
    }

    /**
     * @brief 立即停止（不等待现有任务）
     */
    void stop_now()
    {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_stop = true;
            clear_queue();
        }
        m_condition.notify_all();
    }

    // ========================================================================
    // 线程信息
    // ========================================================================

    /**
     * @brief 获取当前线程的线程池索引（如果属于本池）
     */
    static std::optional<size_t> current_thread_index()
    {
        thread_local std::optional<size_t> index;
        return index;
    }

    /**
     * @brief 设置当前线程的优先级（平台相关）
     */
    static bool set_current_thread_priority(priority_t prio);

    /**
     * @brief 设置当前线程的 CPU 亲和性
     */
    static bool set_current_thread_affinity(const std::vector<bool> &cpus);

private:
    // ========================================================================
    // 内部实现
    // ========================================================================

    Config m_config;
    bool m_enable_priority = true;

    // 使用两个不同的队列
    std::priority_queue<PrioritizedTask> m_priority_tasks;
    std::queue<move_only_function<void()>> m_normal_tasks;

    std::vector<std::thread> m_threads;

    mutable std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_done_condition;

    std::atomic<size_t> m_tasks_running{0};
    std::atomic<bool> m_stop{false};
    bool m_waiting = false;
    size_t m_thread_count = 0;

    /**
     * @brief 检查队列是否为空
     */
    bool tasks_empty() const
    {
        if (m_enable_priority)
        {
            return m_priority_tasks.empty();
        }
        else
        {
            return m_normal_tasks.empty();
        }
    }

    /**
     * @brief 获取一个任务
     */
    bool pop_task(move_only_function<void()> &task);

    /**
     * @brief 工作线程主函数
     */
    void worker_thread(size_t index);

    /**
     * @brief 自动检测 CPU 核心数
     */
    static size_t hardware_concurrency();

    /**
     * @brief 创建线程
     */
    void create_threads(size_t count);

    /**
     * @brief 平台相关的线程优先级设置
     */
    static bool platform_set_thread_priority(std::thread::native_handle_type handle, priority_t prio);
    static bool platform_set_thread_affinity(std::thread::native_handle_type handle, const std::vector<bool> &cpus);
};

// ============================================================================
// 实现部分
// ============================================================================

inline ThreadPool::ThreadPool(const Config &cfg)
    : m_config(cfg), m_enable_priority(cfg.enable_priority)
{

    // 确定线程数量
    if (cfg.num_threads > 0)
    {
        m_thread_count = cfg.num_threads;
    }
    else
    {
        m_thread_count = hardware_concurrency();
        if (m_thread_count == 0)
            m_thread_count = 1;
    }

    // 创建线程
    create_threads(m_thread_count);
}

inline ThreadPool::~ThreadPool()
{
    stop();
    wait();

    // 通知所有线程退出
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    m_condition.notify_all();

    // 等待所有线程结束
    for (auto &t : m_threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

inline void ThreadPool::create_threads(size_t count)
{
    m_threads.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        m_threads.emplace_back([this, i] { worker_thread(i); });

        // 设置线程优先级
        if (m_config.default_priority != 0)
        {
            platform_set_thread_priority(m_threads.back().native_handle(),
                                         m_config.default_priority);
        }
    }
}

inline void ThreadPool::worker_thread(size_t index)
{
    thread_local std::optional<size_t> my_index = index;

    move_only_function<void()> task;

    while (true)
    {
        if (!pop_task(task))
        {
            break;
        }

        m_tasks_running++;

        try
        {
            task();
        }
        catch (const std::exception &)
        {
            // 忽略异常
        }
        catch (...)
        {
            // 忽略所有异常
        }

        m_tasks_running--;

        // 通知等待线程
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_waiting && m_tasks_running == 0 && tasks_empty())
            {
                m_done_condition.notify_one();
            }
        }
    }
}

inline bool ThreadPool::pop_task(move_only_function<void()> &task)
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);

    m_condition.wait(lock, [this] {
        return m_stop || !tasks_empty();
    });

    if (m_stop && tasks_empty())
    {
        return false;
    }

    if (m_enable_priority)
    {
        if (!m_priority_tasks.empty())
        {
            task = std::move(const_cast<PrioritizedTask &>(m_priority_tasks.top()).task);
            m_priority_tasks.pop();
        }
        else
        {
            return false;
        }
    }
    else
    {
        if (!m_normal_tasks.empty())
        {
            task = std::move(m_normal_tasks.front());
            m_normal_tasks.pop();
        }
        else
        {
            return false;
        }
    }

    return true;
}

inline bool ThreadPool::increase_threads(size_t count)
{
    if (count == 0)
        return true;

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    if (m_stop)
        return false;

    size_t new_count = m_thread_count + count;
    m_threads.reserve(new_count);

    for (size_t i = 0; i < count; ++i)
    {
        size_t idx = m_thread_count + i;
        m_threads.emplace_back([this, idx] { worker_thread(idx); });

        if (m_config.default_priority != 0)
        {
            platform_set_thread_priority(m_threads.back().native_handle(),
                                         m_config.default_priority);
        }
    }

    m_thread_count = new_count;
    return true;
}

inline bool ThreadPool::decrease_threads(size_t count)
{
    // 简化实现
    if (count == 0)
        return true;
    if (count >= m_thread_count)
    {
        count = m_thread_count - 1;
        if (count == 0)
            return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        detach([] {
                   // 空任务
               });
    }

    return true;
}

inline size_t ThreadPool::hardware_concurrency()
{
#ifdef THREADPOOL_WINDOWS
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#elif defined(THREADPOOL_POSIX)
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores > 0 ? static_cast<size_t>(cores) : 0;
#endif
}

inline bool ThreadPool::set_current_thread_priority(priority_t prio)
{
#ifdef THREADPOOL_WINDOWS
    return SetThreadPriority(GetCurrentThread(), static_cast<int>(prio)) != 0;
#elif defined(THREADPOOL_POSIX)
    // 在 macOS 上，需要包含 sys/resource.h
    int nice_val = static_cast<int>(prio) / 4;
    if (nice_val < -20)
        nice_val = -20;
    if (nice_val > 19)
        nice_val = 19;
    return setpriority(PRIO_PROCESS, 0, nice_val) == 0;
#endif
    return false;
}

inline bool ThreadPool::set_current_thread_affinity(const std::vector<bool> &cpus)
{
#ifdef THREADPOOL_WINDOWS
    DWORD_PTR mask = 0;
    for (size_t i = 0; i < cpus.size() && i < sizeof(DWORD_PTR) * 8; ++i)
    {
        if (cpus[i])
            mask |= (1ULL << i);
    }
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(THREADPOOL_POSIX) && defined(__linux__)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    for (size_t i = 0; i < cpus.size() && i < CPU_SETSIZE; ++i)
    {
        if (cpus[i])
            CPU_SET(i, &cpu_set);
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 0;
#else
    // macOS 不支持线程亲和性设置
    (void)cpus;
    return false;
#endif
}

inline bool ThreadPool::platform_set_thread_priority(std::thread::native_handle_type handle, priority_t prio)
{
#ifdef THREADPOOL_WINDOWS
    return SetThreadPriority(handle, static_cast<int>(prio)) != 0;
#elif defined(THREADPOOL_POSIX)
    int nice_val = static_cast<int>(prio) / 4;
    if (nice_val < -20)
        nice_val = -20;
    if (nice_val > 19)
        nice_val = 19;
    return setpriority(PRIO_PROCESS, 0, nice_val) == 0;
#endif
    return false;
}

inline bool ThreadPool::platform_set_thread_affinity(std::thread::native_handle_type handle, const std::vector<bool> &cpus)
{
#ifdef THREADPOOL_WINDOWS
    DWORD_PTR mask = 0;
    for (size_t i = 0; i < cpus.size() && i < sizeof(DWORD_PTR) * 8; ++i)
    {
        if (cpus[i])
            mask |= (1ULL << i);
    }
    return SetThreadAffinityMask(handle, mask) != 0;
#elif defined(THREADPOOL_POSIX) && defined(__linux__)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    for (size_t i = 0; i < cpus.size() && i < CPU_SETSIZE; ++i)
    {
        if (cpus[i])
            CPU_SET(i, &cpu_set);
    }
    return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpu_set) == 0;
#else
    (void)handle;
    (void)cpus;
    return false;
#endif
}

#endif // THREADPOOL_HPP