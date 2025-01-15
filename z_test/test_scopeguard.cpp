#include <iostream>

#include "scopeguard/scopeguard.h"

void exampleFunction() {
    std::cout << "Entering exampleFunction" << std::endl;

    // 使用 SCOPE_GUARD 宏来确保在作用域结束时打印 "Exiting exampleFunction"
    SCOPE_GUARD {
        std::cout << "Exiting exampleFunction" << std::endl;
    };

    // 模拟一些操作，可能抛出异常
    bool errorOccurred = true;
    if (errorOccurred) {
        throw std::runtime_error("An error occurred!");
    }

    // 如果不抛出异常，这里代码会继续执行，但最终都会执行 SCOPE_GUARD 中的 lambda 函数
    std::cout << "Performing some operations..." << std::endl;

    // 注意：这里我们可以手动调用 SCOPE_GUARD_DISMISS_UNIQUE_NAME() 来阻止 ScopeGuard 执行清理操作
    // 但在这个例子中我们不调用它，所以 "Exiting exampleFunction" 将会被打印
}

int main() {
    try {
        exampleFunction();
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    return 0;
}