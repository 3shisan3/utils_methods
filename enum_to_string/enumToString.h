/*****************************************************************
File:        enumTostring.h
Version:     1.0
Author:
start date:
Description: 枚举类型转字符串

Version history
[序号][修改日期][修改者][修改内容]

*****************************************************************/

#ifndef ENUMTOSTRING_H
#define ENUMTOSTRING_H

#include <iostream>
#include <array>
#include <exception>
#include <stdexcept>
#include <string_view>

#if defined(__clang__)
#define PRETTY_FUNCTION_NAME __PRETTY_FUNCTION__
#define OFFSET 2
#elif defined(__GNUC__)
#define PRETTY_FUNCTION_NAME __PRETTY_FUNCTION__
#define OFFSET 51
#elif defined(_MSC_VER)
#define PRETTY_FUNCTION_NAME __FUNCSIG__
#define OFFSET 17
#endif

// 设置枚举类型在编译时的最大占位数量（过大会影响编译时间）
#ifndef EnumMaxSize
    #define EnumMaxSize 512
#endif

// 该函数可以分辨
template <typename E, E V>
constexpr std::string_view get_enum_value_name()
{
	std::string_view name{PRETTY_FUNCTION_NAME, sizeof(PRETTY_FUNCTION_NAME) - OFFSET};
	for (std::size_t i = name.size(); i > 0; --i)
	{
		if (!((name[i - 1] >= '0' && name[i - 1] <= '9') ||
			  (name[i - 1] >= 'a' && name[i - 1] <= 'z') ||
			  (name[i - 1] >= 'A' && name[i - 1] <= 'Z') ||
			  (name[i - 1] == '_')))
		{
			name.remove_prefix(i);
			break;
		}
	}
	if (name.size() > 0 && ((name.front() >= 'a' && name.front() <= 'z') ||
							(name.front() >= 'A' && name.front() <= 'Z') ||
							(name.front() == '_')))
	{
		return name;
	}
	return {}; // Invalid name.
}

// 该函数可以分辨
template <typename E, E V>
constexpr bool is_valid()
{
	// get_enum_value_name来自于上面一小节。
	return get_enum_value_name<E, V>().size() != 0;
}

// 制造std::integer_sequence，在[-value,value]之间
template <int... Is>
constexpr auto make_integer_list_wrapper(std::integer_sequence<int, Is...>)
{
	constexpr int half_size = sizeof...(Is) / 2;
	return std::integer_sequence<int, (Is - half_size)...>();
}

// 编译器已知的测试integer sequence
constexpr auto test_integer_sequence_v = make_integer_list_wrapper(std::make_integer_sequence<int, EnumMaxSize>());

template <typename E, int... Is>
constexpr size_t get_enum_size(std::integer_sequence<int, Is...>)
{
	constexpr std::array<bool, sizeof...(Is)> valid{is_valid<E, static_cast<E>(Is)>()...};
	constexpr std::size_t count = [](decltype((valid)) valid_) constexpr noexcept -> std::size_t
	{
		auto count_ = std::size_t{0};
		for (std::size_t i_ = 0; i_ < valid_.size(); ++i_)
		{
			if (valid_[i_])
			{
				++count_;
			}
		}
		return count_;
	}(valid);
	return count;
}

// 一个enum class里面有几个值。
template <typename E>
constexpr std::size_t enum_size_v = get_enum_size<E>(test_integer_sequence_v);

template <typename E, int... Is>
constexpr auto get_all_valid_values(std::integer_sequence<int, Is...>)
{
	constexpr std::array<bool, sizeof...(Is)> valid{is_valid<E, static_cast<E>(Is)>()...};
	constexpr std::array<int, sizeof...(Is)> integer_value{Is...};
	std::array<int, enum_size_v<E>> values{};
	for (std::size_t i = 0, v = 0; i < sizeof...(Is); ++i)
	{
		if (valid[i])
		{
			values[v++] = integer_value[i];
		}
	}
	return values;
}

template <typename E, int... Is>
constexpr auto get_all_valid_names(std::integer_sequence<int, Is...>)
{
	constexpr std::array<std::string_view, sizeof...(Is)> names{get_enum_value_name<E, static_cast<E>(Is)>()...};
	std::array<std::string_view, enum_size_v<E>> valid_names{};
	for (std::size_t i = 0, v = 0; i < names.size(); ++i)
	{
		if (names[i].size() != 0)
		{
			valid_names[v++] = names[i];
		}
	}
	return valid_names;
}

template <typename E>
constexpr auto enum_names_v = get_all_valid_names<E>(test_integer_sequence_v);

template <typename E>
constexpr auto enum_values_v = get_all_valid_values<E>(test_integer_sequence_v);

// gcc里面不允许直接在constexpr function 最后返回throw，这是gcc的bug，
// 所以造了这个函数来绕过gcc的bug
constexpr auto static_throw(int n) -> void
{
	n <= 0 ? throw std::runtime_error("should not reach here. Invalid value.") : 0;
}

template <typename E>
constexpr E string2enum(const std::string_view str)
{
	constexpr auto valid_names = enum_names_v<E>;
	constexpr auto valid_values = enum_values_v<E>;
	constexpr auto enum_size = enum_size_v<E>;
	for (size_t i = 0; i < enum_size; ++i)
	{
		if (str == valid_names[i])
		{
			return static_cast<E>(valid_values[i]);
		}
	}
	static_throw(-1);
	return E{};
}

template <typename E>
constexpr std::string_view enum2string(E V)
{
	constexpr auto valid_names = enum_names_v<E>;
	constexpr auto valid_values = enum_values_v<E>;
	constexpr auto enum_size = enum_size_v<E>;
	for (size_t i = 0; i < enum_size; ++i)
	{
		if (static_cast<int>(V) == valid_values[i])
		{
			return valid_names[i];
		}
	}
	static_throw(-1);
	return "";
}

#endif
