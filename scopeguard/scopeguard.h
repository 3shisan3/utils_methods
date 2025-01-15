/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
All rights reserved.
File:        scopeguard.h
Version:     1.0
Author:      cjx
start date: 2025-01-06
Description: 通用单例方法
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2025-01-06      cjx        create

*****************************************************************/

#pragma once

#include <functional>
#include <utility>

// 定义一个模板类 ScopeGuardImpl，它包含了要执行的清理函数和一个标志位来表示是否已执行
template<typename F>
class ScopeGuardImpl {
public:
    ScopeGuardImpl(F&& f) : func(std::forward<F>(f)), executed(false) {}
    ~ScopeGuardImpl() {
        if (!executed) {
            func();
        }
    }

    // 阻止析构中的执行
    void dismiss() {
        executed = true;
    }

private:
    F func;
    bool executed;
};

// 如果编译器不支持 __COUNTER__，定义一个替代方案，例如使用一个静态变量来生成唯一名称
#ifndef __COUNTER__
#include <mutex>
#include <atomic>
std::atomic<int> counter(0);
#define __COUNTER__ (counter.fetch_add(1))
#endif

// 生成唯一名称的宏，防止名称冲突
#define SCOPE_GUARD_UNIQUE_NAME scopeGuard##__LINE__##__COUNTER__
#define SCOPE_GUARD_DISMISS_UNIQUE_NAME dismissScopeGuard##__LINE__##__COUNTER__

// 定义一个宏来简化 ScopeGuard 的使用
// #define SCOPE_GUARD(func) \ScopeGuard<typename std::decay<F>::type>(std::forward<F>(f));
//     ScopeGuardImpl<decltype(func)> SCOPE_GUARD_UNIQUE_NAME(scope_guard)([&]() { func; })
#define SCOPE_GUARD(func) \
    auto SCOPE_GUARD_UNIQUE_NAME = ::ScopeGuardImpl<decltype(func)>(func)




