/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        lru.h
Version:     1.0
Author:      cjx
start date:
Description: 缓存淘汰算法的管理类
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2023-8-28      cjx        create
2             2025-9-27      cjx        迭代reset接口，增加永久保活判断

*****************************************************************/

#ifndef LRU_H_
#define LRU_H_

#include <ctime>
#include <list>
#include <mutex>
#include <unordered_map>

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

public:
    /**
     * @brief 构造函数
     * @param maxSize [in] 结点最大数
     * @param elasticity [in] 弹性数量
     * @param maxTimeSpan [in] 最大时间间隔
     */
    explicit CLRU(size_t maxSize, size_t elasticity, time_t maxTimeSpan)
        : m_maxSize(maxSize), m_elasticity(elasticity), m_maxTimeSpan(maxTimeSpan)
    {
    }

    virtual ~CLRU() = default;

public:
    /**
     * 获取缓存数目
     * @return
     */
    size_t GetSize() const
    {
        Guard g(m_lock);
        return m_map.size();
    }

    /**
     * 缓存是否为空
     * @return
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
     * @return 值
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
     * @return 淘汰的结点数目
     */
    virtual void ExpireCapacity()
    {
        size_t maxAllowed = m_maxSize + m_elasticity;
        if (0 == m_maxSize || m_map.size() < maxAllowed)
        {
            return;
        }

        while (m_map.size() > m_maxSize)
        {
            m_map.erase(m_list.back().m_key);
            m_list.pop_back();
        }
    }

    /**
     * 检查LRU结点最近访问时间，淘汰超过限制的结点
     * @return 淘汰的结点数目
     */
    virtual void ExpireTime()
    {
        if (0 == m_maxTimeSpan)
        {
            return;
        }

        time_t now = std::time(nullptr);
        while (m_map.size() > 0)
        {
            if (now - m_list.back().m_lastTouch > m_maxTimeSpan)
            {
                m_map.erase(m_list.back().m_key);
                m_list.pop_back();
            }
            else
            {
                break;
            }
        }
    }

protected:
    mutable Lock m_lock; /**< 互斥锁 */

    Map m_map;        /**< 哈希表提供在双向链表中位置映射 */
    list_type m_list; /**< 双向列表用于保存key-value */

    size_t m_maxSize;     /**< 结点最大数 */
    size_t m_elasticity;  /**< 弹性数量 */
    time_t m_maxTimeSpan; /**< 最大时间间隔 */
};

#endif // LRU_H_
