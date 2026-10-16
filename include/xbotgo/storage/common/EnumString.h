#pragma once

#include <iterator>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace xbotgo {
// 枚举通过下方宏提供 constexpr 映射，不使用运行期注册表。
template<class Enum> struct EnumStringTraits;

namespace detail {
template<class Enum> constexpr bool validEnumStringMapping()
{
	static_assert(std::is_enum_v<Enum>, "EnumString only supports enum types");
	const auto &values = EnumStringTraits<Enum>::values;
	for (std::size_t index = 0; index < std::size(values); ++index) {
		if (values[index].second.empty()) {
			return false;
		}
		for (std::size_t other = index + 1; other < std::size(values); ++other) {
			if (values[index].first == values[other].first ||
			    values[index].second == values[other].second) {
				return false;
			}
		}
	}
	return true;
}
} // namespace detail

// 常量输入可在编译期求值；运行期输入查同一份静态表。非法值返回空，不静默替换默认值。
template<class Enum> constexpr std::string_view enumToString(Enum value) noexcept
{
	static_assert(detail::validEnumStringMapping<Enum>(), "Enum mapping has empty or duplicate entries");
	for (const auto &entry : EnumStringTraits<Enum>::values) {
		if (entry.first == value) {
			return entry.second;
		}
	}
	return {};
}

template<class Enum> constexpr std::optional<Enum> enumFromString(std::string_view name) noexcept
{
	static_assert(detail::validEnumStringMapping<Enum>(), "Enum mapping has empty or duplicate entries");
	for (const auto &entry : EnumStringTraits<Enum>::values) {
		if (entry.second == name) {
			return entry.first;
		}
	}
	return std::nullopt;
}

template<class Enum> constexpr bool isValidEnum(Enum value) noexcept
{
	return !enumToString(value).empty();
}
} // namespace xbotgo

// 在 xbotgo 命名空间内、枚举定义下方调用；自动推导元素数量，隐藏模板特化样板代码。
#define XBOTGO_ENUM_STRINGS(EnumType, ...) \
	template<> struct EnumStringTraits<EnumType> { \
		inline static constexpr std::pair<EnumType, std::string_view> values[] = {__VA_ARGS__}; \
	}; \
	static_assert(detail::validEnumStringMapping<EnumType>(), "Enum mapping has empty or duplicate entries")
