#pragma once

#include "common/EnumString.h"

namespace xbotgo {
enum class ImageSpecification { P1080_30, P1080_60, P4K_30 };
// JSON 字符串与枚举项名称一致；编码、解码和校验共用这份映射。
XBOTGO_ENUM_STRINGS(ImageSpecification, {ImageSpecification::P1080_30, "P1080_30"},
		    {ImageSpecification::P1080_60, "P1080_60"}, {ImageSpecification::P4K_30, "P4K_30"});

enum class ImageEncoding { H264, H265 };
XBOTGO_ENUM_STRINGS(ImageEncoding, {ImageEncoding::H264, "H264"}, {ImageEncoding::H265, "H265"});

enum class AntiFlicker { Auto, Hz50, Hz60 };
XBOTGO_ENUM_STRINGS(AntiFlicker, {AntiFlicker::Auto, "Auto"}, {AntiFlicker::Hz50, "Hz50"}, {AntiFlicker::Hz60, "Hz60"});

// V0.4 MVP：先验证单份图像配置的保存、加载和切换流程。
struct ImageParametersData {
	ImageSpecification specification = ImageSpecification::P1080_30;
	ImageEncoding encoding = ImageEncoding::H264;
	bool whiteBalanceLocked = true;
	AntiFlicker antiFlicker = AntiFlicker::Auto;
};
} // namespace xbotgo
