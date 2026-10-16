#include "XBotGoConfigServiceImpl.h"

#include "imageparam/ImageParametersCodec.h"
#include "index/IndexCodec.h"

#include <QUuid>
#include <QDateTime>
#include <algorithm>
#include <mutex>
#include <utility>

namespace xbotgo {
namespace {
} // namespace

XBotGoConfigService::Impl::Impl() = default;
XBotGoConfigService::Impl::~Impl()
{
	shutdown();
}

CfgError XBotGoConfigService::Impl::initialize(const std::filesystem::path &rootDirectory)
{
	std::lock_guard lock(mutex);
	errorCode = CfgErrorCode::None;
	if (initialized) {
		return true;
	}
	// 启动io 线程。
	if (!worker.start(rootDirectory, error)) {
		errorCode = CfgErrorCode::NotRunning;
		return {errorCode, error};
	}
	this->rootDirectory = std::filesystem::absolute(rootDirectory);
	ObsData indexJson;
	IndexState restored;
	bool ready = worker.loadIndex(indexJson, error) && IndexCodec::decode(indexJson.get(), restored, error);
	if (ready) {
		std::vector<std::string> ids;
		for (const auto &entry : restored.configurations) {
			if (entry.id == restored.currentConfigId) {
				ids.insert(ids.begin(), entry.id);
			} else {
				ids.push_back(entry.id);
			}
		}
		ready = false;
		for (const auto &id : ids) {
			if (loadAndActivate(id, restored)) {
				ready = true;
				break;
			}
		}
		if (!ready && restored.configurations.size() < kMaximumConfigurationCount) {
			std::string defaultName = "未命名";
			for (int suffix = 1; std::any_of(restored.configurations.begin(), restored.configurations.end(),
							 [&](const auto &entry) { return entry.name == defaultName; });
			     ++suffix) {
				defaultName = "未命名_" + std::to_string(suffix);
			}
			const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
			ready = createAndActivate(id, defaultName, restored);
		}
	}
	if (!ready) {
		errorCode = CfgErrorCode::LoadFailed;
		if (error.empty()) {
			error = "没有可用配置，且无法创建默认配置";
		}
		std::string stopError;
		worker.stop(stopError);
		return {errorCode, error};
	}
	error.clear();
	errorCode = CfgErrorCode::None;
	imageParameters.setAcceptingWrites(true);
	initialized = true;
	return true;
}

CfgError XBotGoConfigService::Impl::shutdown()
{
	std::lock_guard lock(mutex);
	if (!initialized) {
		return {};
	}
	// 先等待已进入模块的 setter 完成并关闭写入口，再停止 IO 线程。
	imageParameters.setAcceptingWrites(false);
	worker.stop(error);
	initialized = false;
	if (!error.empty()) {
		errorCode = CfgErrorCode::SaveFailed;
		return {errorCode, error};
	}
	errorCode = CfgErrorCode::None;
	return {};
}

std::filesystem::path XBotGoConfigService::Impl::configurationRootDirectory() const
{
	std::lock_guard lock(mutex);
	return rootDirectory;
}

IImageParametersConfig *XBotGoConfigService::Impl::imageParametersConfig()
{
	return &imageParameters;
}

std::vector<ConfigurationSummary> XBotGoConfigService::Impl::configurations() const
{
	std::lock_guard lock(mutex);
	std::vector<ConfigurationSummary> summaries;
	for (const auto &entry : index.configurations) {
		summaries.push_back({entry.id, entry.name, entry.createdAt});
	}
	return summaries;
}

std::string XBotGoConfigService::Impl::currentConfigurationId() const
{
	std::lock_guard lock(mutex);
	return index.currentConfigId;
}

CfgError XBotGoConfigService::Impl::createConfiguration(const std::string &name)
{
	std::lock_guard lock(mutex);
	error.clear();
	errorCode = CfgErrorCode::None;
	if (!initialized) {
		errorCode = CfgErrorCode::NotInitialized;
		error = "配置服务尚未初始化";
		return {errorCode, error};
	}
	if (index.configurations.size() >= kMaximumConfigurationCount) {
		errorCode = CfgErrorCode::ConfigurationLimit;
		error = "最多保存 50 个配置";
		return {errorCode, error};
	}
	std::string normalized;
	if (!IndexCodec::normalizeName(name, normalized, error)) {
		errorCode = CfgErrorCode::InvalidInput;
		return {errorCode, error};
	}
	const auto duplicate = std::any_of(index.configurations.begin(), index.configurations.end(),
					   [&](const auto &item) { return item.name == normalized; });
	if (duplicate) {
		errorCode = CfgErrorCode::ConfigurationExists;
		error = "配置名称已存在";
		return {errorCode, error};
	}
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
	return createAndActivate(id, normalized, index);
}

CfgError XBotGoConfigService::Impl::renameConfiguration(const std::string &configId, const std::string &name)
{
	std::lock_guard lock(mutex);
	error.clear();
	errorCode = CfgErrorCode::None;
	if (!initialized) {
		errorCode = CfgErrorCode::NotInitialized;
		error = "配置服务尚未初始化";
		return {errorCode, error};
	}
	std::string normalized;
	if (!IndexCodec::normalizeName(name, normalized, error)) {
		errorCode = CfgErrorCode::InvalidInput;
		return {errorCode, error};
	}
	auto nextIndex = index;
	auto item = std::find_if(nextIndex.configurations.begin(), nextIndex.configurations.end(),
				 [&](const auto &entry) { return entry.id == configId; });
	if (item == nextIndex.configurations.end()) {
		errorCode = CfgErrorCode::ConfigurationNotFound;
		error = "配置不存在";
		return {errorCode, error};
	}
	const auto duplicate =
		std::any_of(index.configurations.begin(), index.configurations.end(),
			    [&](const auto &entry) { return entry.id != configId && entry.name == normalized; });
	if (duplicate) {
		errorCode = CfgErrorCode::ConfigurationExists;
		error = "配置名称已存在";
		return {errorCode, error};
	}
	item->name = normalized;
	if (!worker.renameConfiguration(configId, IndexCodec::encode(nextIndex), error)) {
		errorCode = CfgErrorCode::SaveFailed;
		return {errorCode, error};
	}
	index = std::move(nextIndex);
	return true;
}

CfgError XBotGoConfigService::Impl::deleteConfiguration(const std::string &configId)
{
	std::lock_guard lock(mutex);
	error.clear();
	errorCode = CfgErrorCode::None;
	if (!initialized) {
		errorCode = CfgErrorCode::NotInitialized;
		error = "配置服务尚未初始化";
		return {errorCode, error};
	}
	if (configId == index.currentConfigId) {
		errorCode = CfgErrorCode::ConfigurationInUse;
		error = "当前配置不能删除";
		return {errorCode, error};
	}
	if (index.configurations.size() <= 1) {
		errorCode = CfgErrorCode::ConfigurationLimit;
		error = "至少需要保留一个配置";
		return {errorCode, error};
	}
	auto nextIndex = index;
	const auto item = std::find_if(nextIndex.configurations.begin(), nextIndex.configurations.end(),
				       [&](const auto &entry) { return entry.id == configId; });
	if (item == nextIndex.configurations.end()) {
		errorCode = CfgErrorCode::ConfigurationNotFound;
		error = "配置不存在";
		return {errorCode, error};
	}
	nextIndex.configurations.erase(item);
	if (!worker.deleteConfiguration(configId, IndexCodec::encode(nextIndex), error)) {
		errorCode = CfgErrorCode::DeleteFailed;
		return {errorCode, error};
	}
	index = std::move(nextIndex);
	return true;
}

CfgError XBotGoConfigService::Impl::switchConfiguration(const std::string &configId)
{
	std::lock_guard lock(mutex);
	error.clear();
	errorCode = CfgErrorCode::None;
	if (!initialized) {
		errorCode = CfgErrorCode::NotInitialized;
		error = "配置服务尚未初始化";
		return false;
	}
	const auto item = std::find_if(index.configurations.begin(), index.configurations.end(),
				       [&](const auto &entry) { return entry.id == configId; });
	if (item == index.configurations.end()) {
		errorCode = CfgErrorCode::ConfigurationNotFound;
		error = "配置不存在";
		return false;
	}
	if (configId == index.currentConfigId) {
		return true;
	}
	return loadAndActivate(configId, index);
}

CfgError XBotGoConfigService::Impl::reloadCurrentConfiguration()
{
	std::lock_guard lock(mutex);
	error.clear();
	errorCode = CfgErrorCode::None;
	if (!initialized) {
		errorCode = CfgErrorCode::NotInitialized;
		error = "配置服务尚未初始化";
		return false;
	}
	return loadAndActivate(index.currentConfigId, index);
}

std::uint64_t XBotGoConfigService::Impl::getMemoryRevision() const
{
	return worker.memoryRevision();
}

std::uint64_t XBotGoConfigService::Impl::getSavedRevision() const
{
	return worker.savedRevision();
}


std::unordered_map<std::string, ObsData> XBotGoConfigService::Impl::defaultModules() const
{
	std::unordered_map<std::string, ObsData> modules;
	modules.emplace("image_parameters", ImageParametersCodec::encode(ImageParametersData{}));
	return modules;
}

bool XBotGoConfigService::Impl::loadAndActivate(const std::string &configId, IndexState nextIndex)
{
	LoadedConfiguration loaded;
	if (!worker.loadConfiguration(configId, loaded, error)) {
		errorCode = CfgErrorCode::LoadFailed;
		return false;
	}
	ObsData image;
	const auto separate = loaded.separateModules.find("image_parameters");
	if (separate != loaded.separateModules.end()) {
		image = std::move(separate->second);
	} else {
		image.reset(obs_data_get_obj(loaded.mainConfig.get(), "image_parameters"));
	}
	ImageParametersData decoded;
	if (!ImageParametersCodec::decode(image.get(), decoded, error)) {
		errorCode = CfgErrorCode::InvalidInput;
		std::string discardError;
		worker.discardCandidate(loaded.candidateToken, discardError);
		return false;
	}
	std::string moduleConfigId = configId;
	nextIndex.currentConfigId = configId;
	if (!worker.activateConfiguration(configId, loaded.candidateToken, IndexCodec::encode(nextIndex), error)) {
		errorCode = CfgErrorCode::SaveFailed;
		return false;
	}
	// 所有可失败步骤在前；这里只交换已准备好的内存，不触发 setter 和自动保存。
	imageParameters.replaceFromStorage(std::move(moduleConfigId), decoded);
	index = std::move(nextIndex);
	return true;
}

bool XBotGoConfigService::Impl::createAndActivate(const std::string &id, const std::string &name, IndexState nextIndex)
{
	// 创建时间只在新增条目时产生；重命名、保存及切换继续沿用原值。
	nextIndex.configurations.push_back(
		{id, name, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()});
	if (!worker.createConfiguration(id, defaultModules(), error)) {
		errorCode = CfgErrorCode::SaveFailed;
		return false;
	}
	if (loadAndActivate(id, std::move(nextIndex))) {
		return true;
	}
	std::string cleanupError;
	if (!worker.discardUnindexedConfiguration(id, cleanupError)) {
		error += "; " + cleanupError;
	}
	return false;
}
} // namespace xbotgo
