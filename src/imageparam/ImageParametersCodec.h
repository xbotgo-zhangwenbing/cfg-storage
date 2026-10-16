#pragma once
#include "io/ObsData.h"
#include <xbotgo/storage/ImageParametersTypes.h>
#include <string>
namespace xbotgo {
class ImageParametersCodec final {
public:
	static ObsData encode(const ImageParametersData &data);
	static bool decode(obs_data_t *json, ImageParametersData &data, std::string &error);
};
} // namespace xbotgo
