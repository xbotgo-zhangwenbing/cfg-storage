#include "ImageParametersCodec.h"
#include "common/StorageValidation.h"

#include <string>
namespace xbotgo {
namespace {
constexpr ObsDataField kImageParametersFormat[] = {
	{"specification", OBS_DATA_STRING},
	{"encoding", OBS_DATA_STRING},
	{"anti_flicker", OBS_DATA_STRING},
	{"white_balance_locked", OBS_DATA_BOOLEAN},
};
} // namespace
ObsData ImageParametersCodec::encode(const ImageParametersData &data)
{
	const auto specification = enumToString(data.specification);
	const auto encoding = enumToString(data.encoding);
	const auto antiFlicker = enumToString(data.antiFlicker);
	if (specification.empty() || encoding.empty() || antiFlicker.empty()) {
		return {};
	}
	auto *json = obs_data_create();
	obs_data_set_string(json, "specification", std::string(specification).c_str());
	obs_data_set_string(json, "encoding", std::string(encoding).c_str());
	obs_data_set_bool(json, "white_balance_locked", data.whiteBalanceLocked);
	obs_data_set_string(json, "anti_flicker", std::string(antiFlicker).c_str());
	return ObsData(json);
}
bool ImageParametersCodec::decode(obs_data_t *json, ImageParametersData &data, std::string &error)
{
	if (!StorageValidation::validateObsDataFormat(json, kImageParametersFormat, error)) {
		error = "图像参数格式错误: " + error;
		return false;
	}
	const auto specification = enumFromString<ImageSpecification>(obs_data_get_string(json, "specification"));
	const auto encoding = enumFromString<ImageEncoding>(obs_data_get_string(json, "encoding"));
	const auto antiFlicker = enumFromString<AntiFlicker>(obs_data_get_string(json, "anti_flicker"));
	if (!specification || !encoding || !antiFlicker) {
		error = "不支持的图像规格、编码或防频闪设置";
		return false;
	}
	ImageParametersData decoded;
	decoded.specification = *specification;
	decoded.encoding = *encoding;
	decoded.antiFlicker = *antiFlicker;
	decoded.whiteBalanceLocked = obs_data_get_bool(json, "white_balance_locked");
	data = decoded;
	error.clear();
	return true;
}
} // namespace xbotgo
