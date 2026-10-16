#pragma once

#include "CfgError.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xbotgo {
class IImageParametersConfig;
struct ConfigurationSummary {
	std::string id;
	std::string name;
	std::string createdAt; // UTC ISO 8601 创建时间；旧索引或扫描恢复无法确定时为空。
};

class XBotGoConfigService final {
public:
	~XBotGoConfigService();
	XBotGoConfigService(const XBotGoConfigService &) = delete;
	XBotGoConfigService &operator=(const XBotGoConfigService &) = delete;

public:
	// 单例由程序持有，返回指针始终非空，调用方无需判空，也不能自行释放。
	static XBotGoConfigService *instance();

	CfgError initialize(const std::filesystem::path &rootDirectory);
	CfgError shutdown();
	std::filesystem::path configurationRootDirectory() const;

	// Service 持有模块，返回地址在 Service 生命周期内保持稳定；调用方不释放。
	IImageParametersConfig *imageParametersConfig();

	// 获取所有的存在index中配置列表文件列表
	std::vector<ConfigurationSummary> configurations() const;
	// 当前的配置列表id
	std::string currentConfigurationId() const;
	// 创建一个配置
	CfgError createConfiguration(const std::string &name);
	// 重名命名一个配置
	CfgError renameConfiguration(const std::string &configId, const std::string &name);
	CfgError deleteConfiguration(const std::string &configId);
	// 切换一个配置
	CfgError switchConfiguration(const std::string &configId);
	// 先保存待写修改，再读取当前配置；失败时保留业务内存。
	CfgError reloadCurrentConfiguration();

	// 临时调试接口：仅供 Demo 显示和测试验证，业务代码不要依赖。
	// TODO：存储流程测试完成后，从 Service 公共接口移除这两个方法，版本号保留在 Worker 内部。
	// 已成功入队的最新修改版本；入队成功不代表文件已经保存。
	std::uint64_t getMemoryRevision() const;
	// 已确认落盘成功的版本；保存失败时不会推进。
	std::uint64_t getSavedRevision() const;

private:
	// 单例构造函数保持私有，禁止外部创建额外实例。
	XBotGoConfigService();
	class Impl;

private:
	std::unique_ptr<Impl> impl_;
};
} // namespace xbotgo
