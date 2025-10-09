/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        lru.h
Version:     2.0
Author:      cjx
start date:
Description: 缓存淘汰算法的管理类
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2023-8-28      cjx        create
2             2025-9-27      cjx        增加对外接口

*****************************************************************/

#ifndef LRU_H_
#define LRU_H_

#include <ctime>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// 空锁
class NullLock
{
public:
    void lock()
    {
    }

    void unlock()
    {
    }

    bool try_lock()
    {
        return true;
    }
};

template <typename K, typename V>
struct Node
{
public:
    K m_key;
    V m_value;
    time_t m_lastTouch;

    Node(K k, V v)
        : m_key(std::move(k)), m_value(std::move(v)), m_lastTouch(0)
    {
        time(&m_lastTouch);
    }

    void update()
    {
        time(&m_lastTouch);
    }
};

/**
 * @brief 缓存淘汰算法的管理类
 */
template <class K, class T, class Lock = NullLock, class N = Node<K, T>,
          class Map = std::unordered_map<K, typename std::list<N>::iterator>>
class CLRU
{
public:
    typedef Node<K, T> node_type;
    typedef std::list<N> list_type;
    typedef Map map_type;
    typedef Lock lock_type;
    using Guard = std::lock_guard<lock_type>;

    /**
     * @brief 缓存统计信息结构体
     */
    struct CacheStats {
        size_t current_size;        /**< 当前缓存大小 */
        size_t max_size;            /**< 最大缓存大小 */
        size_t elasticity;          /**< 弹性大小 */
        time_t max_time_span;       /**< 最大时间间隔 */
        time_t oldest_access_time;  /**< 最旧访问时间 */
        time_t newest_access_time;  /**< 最新访问时间 */
        size_t evicted_by_capacity; /**< 因容量淘汰的数量 */
        size_t evicted_by_time;     /**< 因超时淘汰的数量 */
    };

public:
    /**
     * @brief 构造函数
     * @param maxSize [in] 结点最大数
     * @param elasticity [in] 弹性数量
     * @param maxTimeSpan [in] 最大时间间隔
     */
    explicit CLRU(size_t maxSize, size_t elasticity, time_t maxTimeSpan)
        : m_maxSize(maxSize), m_elasticity(elasticity), m_maxTimeSpan(maxTimeSpan),
          m_evictedByCapacity(0), m_evictedByTime(0)
    {
    }

    virtual ~CLRU() = default;

public:
    /**
     * 获取缓存数目
     * @return 当前缓存大小
     */
    size_t GetSize() const
    {
        Guard g(m_lock);
        return m_map.size();
    }

    /**
     * 缓存是否为空
     * @return true: 为空; false: 不为空
     */
    bool IsEmpty() const
    {
        Guard g(m_lock);
        return m_map.empty();
    }

    /**
     * 清空缓存
     */
    void Clear()
    {
        Guard g(m_lock);
        m_map.clear();
        m_list.clear();
        m_evictedByCapacity = 0;
        m_evictedByTime = 0;
    }

public:
    /**
     * @brief 重置LRU缓存配置
     * @param maxSize 最大容量，0表示不限制
     * @param elasticity 弹性大小
     * @param maxTimeSpan 最大存活时间(秒)，0表示不限制
     */
    void Reset(size_t maxSize, size_t elasticity, time_t maxTimeSpan)
    {
        Guard g(m_lock);
        m_maxSize = maxSize;
        m_elasticity = elasticity;
        m_maxTimeSpan = maxTimeSpan;
        ExpireCapacity();
        ExpireTime();
    }

    /**
     * 插入一个键值对（key，value）到缓存中，
     * @param key [in] 键
     * @param value [in] 值
     * @return true: 插入成功; false: 插入失败
     */
    bool Insert(const K &key, const T &value)
    {
        Guard g(m_lock);
        const auto iter = m_map.find(key);
        if (iter != m_map.end())
        {
            iter->second->m_value = std::move(value);
            iter->second->update();
            m_list.splice(m_list.begin(), m_list, iter->second);
            return true;
        }

        m_list.emplace_front(key, std::move(value));
        m_map[key] = m_list.begin();
        Expire();
        return true;
    }

    /**
     * 缓存中是否存在给定键对应的结点
     * @param key [in] 键
     * @return true: 存在对应的键值; false: 不存在对应的键值
     */
    bool IsExist(const K &key) const
    {
        Guard g(m_lock);
        return m_map.find(key) != m_map.end();
    }

    /**
     * 删除缓存中包含给定键所指向的结点
     * @param key [in] 键
     * @return true: 删除成功; false: 删除失败
     */
    bool Erase(const K &key)
    {
        Guard g(m_lock);
        auto iter = m_map.find(key);
        if (m_map.end() == iter)
        {
            return false;
        }
        m_list.erase(iter->second);
        m_map.erase(iter);
        return true;
    }

    /**
     * 查找缓存中给定键对应的结点
     * @param key [in] 键
     * @return 包含查找结果和值的pair
     */
    std::pair<bool, T> Find(const K &key)
    {
        Guard g(m_lock);
        std::pair<bool, T> p;
        const auto iter = m_map.find(key);
        if (m_map.end() == iter)
        {
            p.first = false;
            return p;
        }
        iter->second->update();
        m_list.splice(m_list.begin(), m_list, iter->second);
        p.first = true;
        p.second = iter->second->m_value;
        return p;
    }

    /**
     * @brief 查看指定键对应的值，但不更新访问时间和位置
     * @param key [in] 键
     * @return 包含查找结果和值的pair
     */
    std::pair<bool, T> Peek(const K &key) const
    {
        Guard g(m_lock);
        std::pair<bool, T> p;
        const auto iter = m_map.find(key);
        if (m_map.end() == iter)
        {
            p.first = false;
            return p;
        }
        p.first = true;
        p.second = iter->second->m_value;
        return p;
    }

    /**
     * @brief 获取缓存中所有键的列表
     * @return 键的向量
     */
    std::vector<K> GetKeys() const
    {
        Guard g(m_lock);
        std::vector<K> keys;
        keys.reserve(m_map.size());
        for (const auto& pair : m_map)
        {
            keys.push_back(pair.first);
        }
        return keys;
    }

    /**
     * @brief 获取按访问时间从新到旧排序的键列表
     * @return 排序后的键向量
     */
    std::vector<K> GetKeysByAccessTime() const
    {
        Guard g(m_lock);
        std::vector<K> keys;
        keys.reserve(m_map.size());
        for (const auto& node : m_list)
        {
            keys.push_back(node.m_key);
        }
        return keys;
    }

    /**
     * @brief 获取最近访问的前N个键（从最新到最旧）
     * @param n [in] 要获取的键数量
     * @return 前N个键的向量
     */
    std::vector<K> GetTopNKeys(size_t n) const
    {
        Guard g(m_lock);
        std::vector<K> keys;
        size_t count = 0;
        for (const auto& node : m_list)
        {
            if (count >= n) break;
            keys.push_back(node.m_key);
            count++;
        }
        return keys;
    }

    /**
     * @brief 获取最新插入或访问的键值对
     * @return 包含最新键值对的optional，如果缓存为空则返回std::nullopt
     */
    std::optional<std::pair<K, T>> GetLatest() const
    {
        Guard g(m_lock);
        if (m_list.empty())
        {
            return std::nullopt;
        }
        const auto& front = m_list.front();
        return std::make_pair(front.m_key, front.m_value);
    }

    /**
     * @brief 获取缓存统计信息
     * @return 缓存统计信息结构体
     */
    CacheStats GetStats() const
    {
        Guard g(m_lock);
        CacheStats stats;
        stats.current_size = m_map.size();
        stats.max_size = m_maxSize;
        stats.elasticity = m_elasticity;
        stats.max_time_span = m_maxTimeSpan;
        stats.evicted_by_capacity = m_evictedByCapacity;
        stats.evicted_by_time = m_evictedByTime;
        
        if (!m_list.empty()) {
            stats.oldest_access_time = m_list.back().m_lastTouch;
            stats.newest_access_time = m_list.front().m_lastTouch;
        } else {
            stats.oldest_access_time = 0;
            stats.newest_access_time = 0;
        }
        
        return stats;
    }

    /**
     * @brief 批量获取多个键的值
     * @param keys [in] 要查找的键向量
     * @return 包含找到的键值对的unordered_map
     */
    std::unordered_map<K, T> BatchFind(const std::vector<K>& keys)
    {
        Guard g(m_lock);
        std::unordered_map<K, T> result;
        for (const auto& key : keys)
        {
            const auto iter = m_map.find(key);
            if (iter != m_map.end())
            {
                iter->second->update();
                m_list.splice(m_list.begin(), m_list, iter->second);
                result[key] = iter->second->m_value;
            }
        }
        return result;
    }

    /**
     * @brief 遍历缓存中的所有元素
     * @param func [in] 处理每个键值对的函数，返回false可中断遍历
     */
    template<typename Func>
    void ForEach(Func func) const
    {
        Guard g(m_lock);
        for (const auto& node : m_list)
        {
            if (!func(node.m_key, node.m_value))
            {
                break;
            }
        }
    }

    /**
     * @brief 遍历缓存中的所有元素（包含访问时间）
     * @param func [in] 处理每个键值对和访问时间的函数，返回false可中断遍历
     */
    template<typename Func>
    void ForEachWithTime(Func func) const
    {
        Guard g(m_lock);
        for (const auto& node : m_list)
        {
            if (!func(node.m_key, node.m_value, node.m_lastTouch))
            {
                break;
            }
        }
    }

protected:
    /**
     * 检查LRU结点的数量和最近访问时间，淘汰超过限制的结点
     */
    void Expire()
    {
        ExpireCapacity();
        ExpireTime();
    }

protected:
    /**
     * 检查LRU结点的数量，淘汰超过限制的结点
     */
    virtual void ExpireCapacity()
    {
        size_t maxAllowed = m_maxSize + m_elasticity;
        if (0 >= m_maxSize || m_map.size() < maxAllowed)
        {
            return;
        }

        size_t evicted = 0;
        while (m_map.size() > m_maxSize)
        {
            m_map.erase(m_list.back().m_key);
            m_list.pop_back();
            evicted++;
        }
        m_evictedByCapacity += evicted;
    }

    /**
     * 检查LRU结点最近访问时间，淘汰超过限制的结点
     */
    virtual void ExpireTime()
    {
        if (0 >= m_maxTimeSpan)
        {
            return;
        }

        time_t now = std::time(nullptr);
        size_t evicted = 0;
        while (!m_list.empty())
        {
            if (now - m_list.back().m_lastTouch > m_maxTimeSpan)
            {
                m_map.erase(m_list.back().m_key);
                m_list.pop_back();
                evicted++;
            }
            else
            {
                break;
            }
        }
        m_evictedByTime += evicted;
    }

protected:
    mutable Lock m_lock; /**< 互斥锁 */

    Map m_map;        /**< 哈希表提供在双向链表中位置映射 */
    list_type m_list; /**< 双向列表用于保存key-value */

    size_t m_maxSize;     /**< 结点最大数 */
    size_t m_elasticity;  /**< 弹性数量 */
    time_t m_maxTimeSpan; /**< 最大时间间隔 */

    size_t m_evictedByCapacity; /**< 因容量淘汰的数量 */
    size_t m_evictedByTime;     /**< 因超时淘汰的数量 */
};

#endif // LRU_H_