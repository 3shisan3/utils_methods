#include <iostream>
#include <string>

#include "LRU/lru.h"

int main()
{
    // 创建 CLRU 实例，设置最大大小为 3，弹性数量为 1，最大时间间隔为 60 秒
    CLRU<std::string, std::string> lruCache(3, 1, 60);
 
    // 插入一些键值对
    lruCache.Insert("key1", "value1");
    lruCache.Insert("key2", "value2");
    lruCache.Insert("key3", "value3");
 
    // 检查插入是否成功
    std::cout << "After inserting 3 items:" << std::endl;
    std::cout << "Size: " << lruCache.GetSize() << std::endl;
    std::cout << "Is 'key1' exist: " << (lruCache.IsExist("key1") ? "Yes" : "No") << std::endl;
    std::cout << "Find 'key2': " << (lruCache.Find("key2").first ? lruCache.Find("key2").second : "Not found") << std::endl;
 
    // 插入第四个键值对，应该触发过期机制
    lruCache.Insert("key4", "value4");
    std::cout << "After inserting 4th item:" << std::endl;
    std::cout << "Is 'key1' still exist: " << (lruCache.IsExist("key1") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key2' still exist: " << (lruCache.IsExist("key2") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key3' still exist: " << (lruCache.IsExist("key3") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key4' exist: " << (lruCache.IsExist("key4") ? "Yes" : "No") << std::endl;
 
    // 查找并更新一个键值对
    lruCache.Find("key2");
    std::cout << "After accessing 'key2':" << std::endl;
 
    // 插入第五个键值对，检查是否触发过期机制
    lruCache.Insert("key5", "value5");
    std::cout << "After inserting 5th item:" << std::endl;
    std::cout << "Is 'key2' still exist: " << (lruCache.IsExist("key2") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key3' still exist: " << (lruCache.IsExist("key3") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key4' still exist: " << (lruCache.IsExist("key4") ? "Yes" : "No") << std::endl;
    std::cout << "Is 'key5' exist: " << (lruCache.IsExist("key5") ? "Yes" : "No") << std::endl;
 
    // 删除一个键值对
    lruCache.Erase("key5");
    std::cout << "After erasing 'key5':" << std::endl;
    std::cout << "Is 'key5' exist: " << (lruCache.IsExist("key5") ? "Yes" : "No") << std::endl;
 
    // 测试过期时间
    // 假设当前时间可以修改以测试时间过期（实际中不建议这样做，这里为了测试）
    // 注意：这里需要修改 CLRU 类中的 m_lastTouch 和 m_maxTimeSpan 以测试时间过期
    // 这里只是示例，实际操作中需要更复杂的逻辑来模拟时间流逝
    // 但为了简单起见，我们假设已经过了足够长的时间使 key2 过期
    // 注意：由于我们使用的是 time_t 类型，直接修改 m_lastTouch 可能涉及复杂的时间处理
    // 这里我们仅通过逻辑描述如何测试，实际代码省略时间修改部分
 
    // 理论上，如果 key2 的时间超过了 m_maxTimeSpan，它应该被移除
    // 但由于直接修改时间比较复杂，这里我们仅描述逻辑
 
    // 清理缓存
    lruCache.Clear();
    std::cout << "After clearing cache:" << std::endl;
    std::cout << "Size: " << lruCache.GetSize() << std::endl;
 
    return 0;
}