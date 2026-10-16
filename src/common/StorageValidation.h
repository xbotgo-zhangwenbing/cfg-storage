#pragma once

#include <obs-data.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace xbotgo {
struct ObsDataField {
	const char *key;
	obs_data_type type;
	bool required = true;
	obs_data_number_type numberType = OBS_DATA_NUM_INVALID;
};

// 集中处理存储 key 和 JSON 结构校验，避免 Worker/Codec 各自复制规则。
class StorageValidation final {
public:
	static bool isSafeStorageKey(std::string_view key)
	{
		return !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char character) {
			return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			       (character >= '0' && character <= '9') || character == '-' || character == '_';
		});
	}

	static bool isModuleKey(std::string_view key)
	{
		if (key.empty() || key == "config" || key == "config_id") {
			return false;
		}
		return std::all_of(key.begin(), key.end(), [](unsigned char character) {
			return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
			       character == '_';
		});
	}

	template<std::size_t Count>
	static bool validateObsDataFormat(obs_data_t *data, const ObsDataField (&format)[Count], std::string &error)
	{
		error.clear();
		if (!data) {
			error = "JSON 对象为空";
			return false;
		}
		auto appendError = [&error](const std::string &message) {
			if (!error.empty()) {
				error += "; ";
			}
			error += message;
		};
		for (const auto &field : format) {
			auto *item = obs_data_item_byname(data, field.key);
			// OBS 清除字段值后可能仍保留 item 和类型；没有实际值不算有效字段。
			if (!item || !obs_data_item_has_user_value(item)) {
				if (field.required) {
					appendError("缺少字段: " + std::string(field.key));
				}
				obs_data_item_release(&item);
				continue;
			}
			const auto actualType = obs_data_item_gettype(item);
			if (actualType != field.type) {
				appendError("字段类型错误: " + std::string(field.key));
			} else if (actualType == OBS_DATA_NUMBER && field.numberType != OBS_DATA_NUM_INVALID &&
				   obs_data_item_numtype(item) != field.numberType) {
				appendError("字段数字类型错误: " + std::string(field.key));
			}
			obs_data_item_release(&item);
		}
		return error.empty();
	}
};
} // namespace xbotgo
