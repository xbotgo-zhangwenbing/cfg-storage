#pragma once

#include <string>
#include <vector>

namespace xbotgo {
inline constexpr int kIndexSchemaVersion = 1;
inline constexpr int kConfigurationVersion = 4;
inline constexpr std::size_t kMaximumConfigurationCount = 50;

// 内部索引条目采用结构体，后续可扩展元数据，不限制成 ID 到名称的字符串映射。
struct ConfigurationEntry {
	std::string id;
	std::string name;
	std::string createdAt;
};
struct IndexState {
	int indexSchemaVersion = kIndexSchemaVersion;
	int configurationVersion = kConfigurationVersion;
	std::string currentConfigId;
	std::vector<ConfigurationEntry> configurations;
};
} // namespace xbotgo
