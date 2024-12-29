#include <iostream>
#include <vector>
#include <thread>
#include <future>
#include <numeric>

#include "threadpool/method_1/threadpool.h"

int main() {
    // 创建一个包含4个工作线程的线程池
    ThreadPool pool(4);

    // 定义一些任务
    auto task1 = []() -> int {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 42;
    };

    auto task2 = []() -> std::string {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return "Hello, ThreadPool!";
    };

    auto task3 = []() -> double {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto arr = std::vector<int>{1, 2, 3, 4, 5};
    return std::accumulate(arr.begin(), arr.end(), 0.0,
        [](double sum, int value) {
            return sum + static_cast<double>(value);
        });
    };

    auto task4 = [](int inVal) -> int {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return inVal;
    };

    // 将任务添加到线程池并获取它们的 future 对象
    std::future<int> result1 = pool.enqueue(task1);
    std::future<std::string> result2 = pool.enqueue(task2);
    std::future<double> result3 = pool.enqueue(task3);
    auto result4 = pool.enqueue(task4, 33);

    // 等待任务完成并输出结果
    std::cout << "Task 1 result: " << result1.get() << std::endl;
    std::cout << "Task 2 result: " << result2.get() << std::endl;
    std::cout << "Task 3 result: " << result3.get() << std::endl;
    std::cout << "Task 4 result: " << result4.get() << std::endl;

    return 0;
}