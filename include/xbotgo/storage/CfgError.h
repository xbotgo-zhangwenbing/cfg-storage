#pragma once

#include <string>
#include <utility>

namespace xbotgo {
// 面向业务判断和翻译映射的稳定错误类型；不包含任何语言文本。
enum class CfgErrorCode {
	None,
	InvalidInput,
	NotInitialized,
	NotRunning,
	OperationInProgress,
	ConfigurationNotFound,
	ConfigurationExists,
	ConfigurationInUse,
	ConfigurationLimit,
	LoadFailed,
	SaveFailed,
	DeleteFailed,
	IndexFailed,
	UnsupportedVersion,
	InternalError
};

struct CfgError {
	CfgErrorCode code = CfgErrorCode::None;
	// 具体失败原因，仅供日志和调试使用，不作为业务判断依据。
	std::string description;

	CfgError() = default;
	CfgError(bool success) : code(success ? CfgErrorCode::None : CfgErrorCode::InternalError) {}
	CfgError(CfgErrorCode errorCode, std::string errorDescription)
		: code(errorCode), description(std::move(errorDescription))
	{
	}

	operator bool() const
	{
		return code != CfgErrorCode::None;
	}
};
} // namespace xbotgo
