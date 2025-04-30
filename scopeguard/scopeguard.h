/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        scopeguard.h
Version:     1.0
Author:      cjx
start date: 2025-01-06
Description: RAII原理的通用scopeguard方法，提供接口默认执行函数不会抛异常
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2025-01-06      cjx        create

*****************************************************************/

#pragma once

#include <exception>
#include <functional>
#include <utility>

class ScopeGuardImplBase
{
public:
    void dismiss() noexcept { dismissed_ = true; }
    void rehire() noexcept { dismissed_ = false; }

protected:
    ScopeGuardImplBase(bool dismissed = false) noexcept : dismissed_(dismissed) {}

    [[noreturn]] static void terminate() noexcept
    {   // std::current_exception()不同平台不准确，可替换为项目自己的异常指针
        std::rethrow_exception(std::current_exception());
    }
    static ScopeGuardImplBase makeEmptyScopeGuard() noexcept
    {
        return ScopeGuardImplBase{};
    }

    bool dismissed_;
};

// 声明一个空结构体，作为特殊状态标记
struct ScopeGuardDismissed {};  // 构造函数explicit保证可以正确过滤

// FunctionType入参函数类型，InvokeNoexcept标记入参函数是否会抛出异常(true不会)
template <typename FunctionType, bool InvokeNoexcept>
class ScopeGuardImpl : public ScopeGuardImplBase
{
public:
    // 构造函数重载
    explicit ScopeGuardImpl(FunctionType &fn) noexcept(
        std::is_nothrow_copy_constructible<FunctionType>::value)    // 决定是否声明为noexcept
        : ScopeGuardImpl(
              std::as_const(fn),
              makeFailsafe(std::is_nothrow_copy_constructible<FunctionType>{}, &fn)) {}

    explicit ScopeGuardImpl(const FunctionType &fn) noexcept(
        std::is_nothrow_copy_constructible<FunctionType>::value)
        : ScopeGuardImpl(
              fn,
              makeFailsafe(std::is_nothrow_copy_constructible<FunctionType>{}, &fn)) {}

    explicit ScopeGuardImpl(FunctionType &&fn) noexcept(
        std::is_nothrow_move_constructible<FunctionType>::value)
        : ScopeGuardImpl(
              std::move_if_noexcept(fn),
              makeFailsafe(std::is_nothrow_move_constructible<FunctionType>{}, &fn)) {}

    // 标记为无需scopeguard进行资源保护的情况
    explicit ScopeGuardImpl(FunctionType &&fn, ScopeGuardDismissed) noexcept(
        std::is_nothrow_move_constructible<FunctionType>::value)
        : ScopeGuardImplBase{true}, function_(std::forward<FunctionType>(fn)) {}
    
    // 执行所有权交给当前新实例
    ScopeGuardImpl(ScopeGuardImpl &&other) noexcept(
        std::is_nothrow_move_constructible<FunctionType>::value)
        : function_(std::move_if_noexcept(other.function_))
    {
        dismissed_ = std::exchange(other.dismissed_, true);
    }

    ~ScopeGuardImpl() noexcept(InvokeNoexcept)
    {
        if (!dismissed_)
        {
            execute();
        }
    }

private:
    static ScopeGuardImplBase makeFailsafe(std::true_type, const void *) noexcept
    {
        return makeEmptyScopeGuard();
    }

    template <typename Fn>
    static auto makeFailsafe(std::false_type, Fn *fn) noexcept
        -> ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept>
    {
        return ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept>{
            std::ref(*fn)};
    }

    template <typename Fn>
    explicit ScopeGuardImpl(Fn &&fn, ScopeGuardImplBase &&failsafe)
        : ScopeGuardImplBase{}, function_(std::forward<Fn>(fn))
    {
        failsafe.dismiss();
    }

    void *operator new(std::size_t) = delete;

    void execute() noexcept(InvokeNoexcept)
    {
        if constexpr (InvokeNoexcept)
        {
            try
            {
                function_();
            }
            catch (...)
            {
                terminate();    // 替换为自定义的异常出现时的打印或其他行为操作
            }
        }
        else
        {
            function_();
        }
    }

    FunctionType function_;
};

// 如果拓展scopeguard类型标记时，通过重载+，参考使用模板
enum class ScopeGuardExample {};
template <typename FunctionType>
ScopeGuardImpl<typename std::decay<FunctionType>::type, true> operator+(
    ScopeGuardExample, FunctionType&& fn) {
  return ScopeGuardImpl<typename std::decay<FunctionType>::type, true>(
      std::forward<FunctionType>(fn));
}

// 如果编译器不支持 __COUNTER__，定义一个替代方案，例如使用一个静态变量来生成唯一名称
#ifndef __COUNTER__
#include <mutex>
#include <atomic>
std::atomic<int> counter(0);
#define __COUNTER__ (counter.fetch_add(1))
#endif

// 生成唯一名称的宏，防止名称冲突
#define SCOPE_GUARD_UNIQUE_NAME scopeGuard##__LINE__##__COUNTER__


///////////*//////////     具体使用涉及    //////////*///////////

// 构造返回一个scopeguard实例，提供相关的dismiss和rehire接口；需要捕获返回值，否则入参函数会直接执行
template <typename F>
ScopeGuardImpl<F, true> makeGuard(F &&f) noexcept(
    noexcept(ScopeGuardImpl<F, true>(static_cast<F &&>(f))))
{
    return ScopeGuardImpl<F, true>(static_cast<F &&>(f));
}

// 构造返回一个关闭了析构时函数执行功能的scopeguard实例；后续rehire接口再启用
template <typename F>
ScopeGuardImpl<F, true> makeDismissedGuard(F &&f) noexcept(
    noexcept(ScopeGuardImpl<F, true>(static_cast<F &&>(f), ScopeGuardDismissed{})))
{
    return ScopeGuardImpl<F, true>(static_cast<F &&>(f), ScopeGuardDismissed{});
}

// 直接使用宏接代码块(默认不会抛出异常)
#define SCOPE_GUARD \
    auto SCOPE_GUARD_UNIQUE_NAME = ScopeGuardExample() + [&]() noexcept