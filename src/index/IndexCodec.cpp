#include "IndexCodec.h"
#include "common/StorageValidation.h"
#include "common/StorageValidation.h"

#include <QString>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <unordered_set>

namespace xbotgo {
namespace {
constexpr ObsDataField kIndexFormat[] = {
	{"index_schema_version", OBS_DATA_NUMBER, true, OBS_DATA_NUM_INT},
	{"configuration_version", OBS_DATA_NUMBER, true, OBS_DATA_NUM_INT},
	{"current_config_id", OBS_DATA_STRING},
	{"configurations", OBS_DATA_OBJECT},
};
constexpr ObsDataField kConfigurationEntryFormat[] = {
	{"name", OBS_DATA_STRING},
	{"created_at", OBS_DATA_STRING, false},
};
} // namespace

bool IndexCodec::isValidId(const std::string &id)
{
	return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char character) {
		return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		       (character >= '0' && character <= '9') || character == '-' || character == '_';
	});
}

bool IndexCodec::normalizeName(const std::string &input, std::string &normalized, std::string &error)
{
	const QString name = QString::fromStdString(input).trimmed();
	if (name.isEmpty()) {
		error = "配置名称不能为空";
		return false;
	}
	QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, name);
	int count = 0;
	while (boundaries.toNextBoundary() != -1) {
		++count;
	}
	if (count > 20) {
		error = "配置名称最多 20 个字符";
		return false;
	}
	normalized = name.toStdString();
	return true;
}

ObsData IndexCodec::encode(const IndexState &index)
{
	ObsData json(obs_data_create());
	obs_data_set_int(json.get(), "index_schema_version", index.indexSchemaVersion);
	obs_data_set_int(json.get(), "configuration_version", index.configurationVersion);
	obs_data_set_string(json.get(), "current_config_id", index.currentConfigId.c_str());
	ObsData configurations(obs_data_create());
	for (const auto &entry : index.configurations) {
		ObsData item(obs_data_create());
		obs_data_set_string(item.get(), "name", entry.name.c_str());
		obs_data_set_string(item.get(), "created_at", entry.createdAt.c_str());
		obs_data_set_obj(configurations.get(), entry.id.c_str(), item.get());
	}
	obs_data_set_obj(json.get(), "configurations", configurations.get());
	return json;
}

bool IndexCodec::decode(obs_data_t *json, IndexState &index, std::string &error)
{
	if (!StorageValidation::validateObsDataFormat(json, kIndexFormat, error)) {
		error = "索引格式错误: " + error;
		return false;
	}
	if (obs_data_get_int(json, "index_schema_version") != kIndexSchemaVersion ||
	    obs_data_get_int(json, "configuration_version") != kConfigurationVersion) {
		error = "索引或配置版本不兼容，原文件已保留";
		return false;
	}
	IndexState decoded;
	decoded.currentConfigId = obs_data_get_string(json, "current_config_id");
	ObsData configurations(obs_data_get_obj(json, "configurations"));
	std::unordered_set<std::string> names;
	for (auto *item = obs_data_first(configurations.get()); item;) {
		const std::string id = obs_data_item_get_name(item);
		ObsData entry(obs_data_item_get_obj(item));
		if (!StorageValidation::validateObsDataFormat(entry.get(), kConfigurationEntryFormat, error)) {
			obs_data_item_release(&item);
			error = "索引条目 " + id + " 格式错误: " + error;
			return false;
		}
		std::string name;
		if (!isValidId(id) || !normalizeName(obs_data_get_string(entry.get(), "name"), name, error) ||
		    !names.insert(name).second || decoded.configurations.size() >= kMaximumConfigurationCount) {
			obs_data_item_release(&item);
			error = "索引条目无效、重名或超过数量上限";
			return false;
		}
		// 旧索引没有创建时间时保留未知，不能把本次加载时间当作原始创建时间。
		decoded.configurations.push_back({id, std::move(name), obs_data_get_string(entry.get(), "created_at")});
		obs_data_item_next(&item);
	}
	// current_config_id 缺失对应条目时交给 Service 选择其他可加载配置，不直接丢弃整个列表。
	index = std::move(decoded);
	error.clear();
	return true;
}
} // namespace xbotgo
