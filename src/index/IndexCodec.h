#pragma once

#include "IndexState.h"
#include "io/ObsData.h"

namespace xbotgo {
// 只转换和校验索引，不做文件 IO；Service 使用结构体，Worker 持有 JSON 快照。
class IndexCodec final {
public:
	static ObsData encode(const IndexState &index);
	static bool decode(obs_data_t *json, IndexState &index, std::string &error);
	static bool normalizeName(const std::string &input, std::string &name, std::string &error);
	static bool isValidId(const std::string &id);
};
} // namespace xbotgo
