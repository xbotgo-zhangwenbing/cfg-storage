#pragma once

#include <xbotgo/storage/XBotGoConfigService.h>

#include "imageparam/ImageParametersConfigImpl.h"
#include "index/IndexState.h"
#include "io/StorageWorker.h"

#include <shared_mutex>

namespace xbotgo {
// Service 的内部实现，集中管理模块、内存索引及配置切换流程。
class XBotGoConfigService::Impl final {
public:
	Impl();
	~Impl();
	Impl(const Impl &) = delete;
	Impl &operator=(const Impl &) = delete;

public:
	bool initialize(const std::filesystem::path &rootDirectory);
	void shutdown();
	std::filesystem::path configurationRootDirectory() const;
	IImageParametersConfig *imageParametersConfig();
	std::vector<ConfigurationSummary> configurations() const;
	std::string currentConfigurationId() const;
	bool createConfiguration(const std::string &name);
	bool renameConfiguration(const std::string &configId, const std::string &name);
	bool deleteConfiguration(const std::string &configId);
	bool switchConfiguration(const std::string &configId);
	bool reloadCurrentConfiguration();
	// 临时转发 Worker 版本号供 Demo 和测试使用；测试完成后随公共接口一起移除。
	std::uint64_t getMemoryRevision() const;
	std::uint64_t getSavedRevision() const;
	std::string lastError() const;

private:
	std::unordered_map<std::string, ObsData> defaultModules() const;
	// 调用者持配置独占门锁；IO 线程不会回调业务对象，不会争抢这把锁。
	bool loadAndActivate(const std::string &configId, IndexState nextIndex);
	bool createAndActivate(const std::string &id, const std::string &name, IndexState nextIndex);

private:
	mutable std::shared_mutex mutex;
	StorageWorker worker;
	ImageParametersConfigImpl imageParameters{worker, mutex};
	IndexState index;
	std::filesystem::path rootDirectory;
	std::string error;
	bool initialized = false;
};
} // namespace xbotgo
