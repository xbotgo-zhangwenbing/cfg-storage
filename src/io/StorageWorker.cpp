#include "StorageWorker.h"
#include "common/StorageValidation.h"
#include "index/IndexCodec.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <charconv>

namespace xbotgo {
namespace {
// Worker 内部集中维护模块布局；业务层不传入文件存储策略。
const std::vector<std::pair<std::string, StorageMode>> kModuleStorage = {
	{"image_parameters", StorageMode::MainConfig},
};

} // namespace

bool StorageWorker::isSafeKey(const std::string &key)
{
	// 配置 ID 用作目录名，统一使用索引 ID 规则，避免目录穿透。
	return StorageValidation::isSafeStorageKey(key);
}

ObsData StorageWorker::copyJson(obs_data_t *data)
{
	// libobs 没有独立的深拷贝 API；addref 仍共享可变对象，所以通过 JSON 建立独立副本。
	return ObsData(data ? obs_data_create_from_json(obs_data_get_json(data)) : obs_data_create());
}

bool StorageWorker::hasUnsupportedVersion(obs_data_t *data)
{
	if (!data) {
		return false;
	}
	const auto schema = obs_data_get_int(data, "index_schema_version");
	const auto version = obs_data_get_int(data, "configuration_version");
	return (schema > 0 && schema != kIndexSchemaVersion) || (version > 0 && version != kConfigurationVersion);
}

class StorageWorker::IoState {
public:
	struct SeparateFileData {
		std::filesystem::path file;
		ObsData data;
		std::uint64_t revision = 0;
		bool dirty = false; // 该模块文件有尚未落盘的更新时才写入。
	};

	ObsData mainConfigData;
	std::filesystem::path mainConfigFile;
	bool mainConfigDirty = false;
	std::unordered_map<std::string, SeparateFileData> separateFiles;
	std::uint64_t revision = 0; // 当前配置已合并快照的最大版本，全部文件成功后才能推进保存版本。
	bool saveScheduled = false;
	std::chrono::steady_clock::time_point deadline;

public:
	bool hasDirtyFiles() const
	{
		if (mainConfigDirty) {
			return true;
		}
		return std::any_of(separateFiles.begin(), separateFiles.end(),
				   [](const auto &entry) { return entry.second.dirty; });
	}
};

StorageWorker::StorageWorker() = default;
StorageWorker::~StorageWorker()
{
	std::string error;
	stop(error);
}

bool StorageWorker::start(const std::filesystem::path &rootDirectory, std::string &error)
{
	std::lock_guard lock(mutex_);
	error.clear();
	std::unordered_map<std::string, StorageMode> moduleModes;
	for (const auto &[moduleKey, mode] : kModuleStorage) {
		if (!StorageValidation::isModuleKey(moduleKey)) {
			error = "Worker 内部模块存储描述无效: " + moduleKey;
			return false;
		}
		moduleModes.emplace(moduleKey, mode);
	}

	if (workerState_ == WorkerState::Running) {
		if (moduleModes != moduleModes_) {
			error = "运行期间不能修改模块存储方式";
			return false;
		}
		return true;
	}
	std::error_code pathError;
	const auto absoluteRoot = std::filesystem::absolute(rootDirectory, pathError).lexically_normal();
	if (rootDirectory.empty() || pathError) {
		error = "配置根目录无效";
		return false;
	}
	// 退出保存失败后允许对同一根目录重新启动重试，禁止换目录丢掉旧缓存。
	if (ioState_ && ioState_->hasDirtyFiles() && (rootDirectory_ != absoluteRoot || moduleModes_ != moduleModes)) {
		error = "旧配置尚未保存，不能更换配置根目录或模块存储方式";
		return false;
	}
	if (!ioState_ || !ioState_->hasDirtyFiles()) {
		ioState_.reset();
		candidateState_.reset();
		indexData_.reset();
		unindexedConfigurations_.clear();
		recoveredIndex_ = false;
		memoryRevision_ = savedRevision_ = 0;
		activeConfigId_.clear();
		lastError_.clear();
	}
	rootDirectory_ = absoluteRoot;
	moduleModes_ = std::move(moduleModes);
	workerState_ = WorkerState::Stopped;
	switching_ = false;
	try {
		thread_ = std::thread(&StorageWorker::run, this);
	} catch (const std::exception &exception) {
		error = "启动 IO 线程失败: " + std::string(exception.what());
		return false;
	}
	workerState_ = WorkerState::Running;
	return true;
}

bool StorageWorker::createConfiguration(const std::string &configId,
					std::unordered_map<std::string, ObsData> initialModules, std::string &error)
{
	Command command;
	command.type = CommandType::Create;
	command.configId = configId;
	command.initialModules = std::move(initialModules);
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::renameConfiguration(const std::string &configId, ObsData nextIndex, std::string &error)
{
	Command command;
	command.type = CommandType::Rename;
	command.configId = configId;
	command.nextIndex = std::move(nextIndex);
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::deleteConfiguration(const std::string &configId, ObsData nextIndex, std::string &error)
{
	Command command;
	command.type = CommandType::Delete;
	command.configId = configId;
	command.nextIndex = std::move(nextIndex);
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::loadIndex(ObsData &index, std::string &error)
{
	Command command;
	command.type = CommandType::LoadIndex;
	auto result = submit(std::move(command));
	error = std::move(result.error);
	index = std::move(result.index);
	return result.success;
}

bool StorageWorker::discardUnindexedConfiguration(const std::string &configId, std::string &error)
{
	Command command;
	command.type = CommandType::DiscardUnindexed;
	command.configId = configId;
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::loadConfiguration(const std::string &configId, LoadedConfiguration &loaded, std::string &error)
{
	loaded = {};
	Command command;
	command.type = CommandType::Load;
	command.configId = configId;
	auto result = submit(std::move(command));
	error = std::move(result.error);
	if (result.success) {
		loaded = std::move(result.loaded);
	}
	return result.success;
}

bool StorageWorker::activateConfiguration(const std::string &configId, std::uint64_t candidateToken, ObsData nextIndex,
					  std::string &error)
{
	Command command;
	command.type = CommandType::Activate;
	command.nextIndex = std::move(nextIndex);
	command.configId = configId;
	command.candidateToken = candidateToken;
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::discardCandidate(std::uint64_t candidateToken, std::string &error)
{
	Command command;
	command.type = CommandType::Discard;
	command.candidateToken = candidateToken;
	auto result = submit(std::move(command));
	error = std::move(result.error);
	return result.success;
}

bool StorageWorker::submitModule(const std::string &configId, const std::string &moduleKey, ObsData snapshot)
{
	std::lock_guard lock(mutex_);
	if (workerState_ != WorkerState::Running || switching_ || configId != activeConfigId_ || !isSafeKey(configId) ||
	    moduleModes_.find(moduleKey) == moduleModes_.end() || !snapshot) {
		lastError_ = "无法提交快照：IO 线程未运行、配置切换中或任务无效";
		return false;
	}
	const auto revision = ++memoryRevision_;
	// 同一配置和模块只保留最后一次完整快照，移动赋值自动释放被替换的 JSON。
	pendingTasks_.insert_or_assign(configId + "/" + moduleKey,
				       SaveTask{configId, moduleKey, revision, moduleModes_.at(moduleKey),
						std::move(snapshot)});
	cv_.notify_one();
	return true;
}

StorageWorker::Result StorageWorker::submit(Command command)
{
	auto completed = command.completion.get_future();
	{
		std::lock_guard lock(mutex_);
		// 所有命令都必须在 IO 线程正常运行期间提交。
		const bool workerUnavailable = workerState_ != WorkerState::Running;
		if (workerUnavailable) {
			return {false, "IO 线程未运行或配置 ID 无效", {}};
		}

		// 丢弃候选和读取索引不依赖具体配置 ID，其余命令必须使用安全的存储键。
		const bool skipsConfigIdValidation =
			command.type == CommandType::Discard || command.type == CommandType::LoadIndex;
		const bool invalidConfigId = !skipsConfigIdValidation && !isSafeKey(command.configId);
		if (invalidConfigId) {
			return {false, "IO 线程未运行或配置 ID 无效", {}};
		}

		// 加载和激活会切换当前配置，同一时间只允许一个切换操作。
		const bool isConfigurationSwitch =
			command.type == CommandType::Load || command.type == CommandType::Activate;
		if (isConfigurationSwitch) {
			if (switching_) {
				return {false, "已有配置加载或激活正在执行", {}};
			}
			switching_ = true;
		}
		command.precedingTasks.swap(pendingTasks_);
		commands_.push_back(std::move(command));
	}
	cv_.notify_one();
	return completed.get();
}

StorageWorker::Result StorageWorker::processCommand(Command &command)
{
	if (command.type == CommandType::Load) {
		candidateState_.reset();
	}
	if (command.type == CommandType::Discard) {
		if (candidateState_ && candidateToken_ == command.candidateToken) {
			candidateState_.reset();
		}
		return {true, {}, {}};
	}
	if (!flushInIoThread()) {
		return {false, lastError(), {}};
	}
	Result result;
	switch (command.type) {
	case CommandType::Create:
		if (!loadIndexInIoThread(result.error)) {
			break;
		}
		result.success = createInIoThread(command.configId, std::move(command.initialModules), result.error);
		break;
	case CommandType::Load:
		result.success = loadInIoThread(command.configId, result.loaded, result.error);
		break;
	case CommandType::Activate: {
		{
			std::lock_guard lock(mutex_);
			if (!candidateState_ || candidateConfigId_ != command.configId ||
			    candidateToken_ != command.candidateToken || candidateRevision_ != memoryRevision_) {
				result.error = "候选配置不存在或已过期，请重新加载";
				break;
			}
		}
		IndexState next;
		if (!IndexCodec::decode(command.nextIndex.get(), next, result.error) ||
		    next.currentConfigId != command.configId ||
		    std::none_of(next.configurations.begin(), next.configurations.end(),
				 [&](const auto &entry) { return entry.id == command.configId; })) {
			if (result.error.empty()) {
				result.error = "激活配置与索引不一致";
			}
			break;
		}
		if (!saveIndexInIoThread(std::move(command.nextIndex), result.error)) {
			break;
		}
		// 索引成功落盘后才提交 IO 缓存；保存失败保留原 activeConfigId 和旧缓存。
		ioState_ = std::move(candidateState_);
		{
			std::lock_guard lock(mutex_);
			activeConfigId_.swap(command.configId);
		}
		unindexedConfigurations_.erase(next.currentConfigId);
		result.success = true;
		break;
	}
	case CommandType::Rename: {
		IndexState next;
		if (!IndexCodec::decode(command.nextIndex.get(), next, result.error) ||
		    next.currentConfigId != activeConfigId_ ||
		    std::none_of(next.configurations.begin(), next.configurations.end(),
				 [&](const auto &entry) { return entry.id == command.configId; })) {
			if (result.error.empty()) {
				result.error = "重命名索引与当前配置不一致";
			}
			break;
		}
		result.success = saveIndexInIoThread(std::move(command.nextIndex), result.error);
		break;
	}
	case CommandType::Delete: {
		IndexState next;
		if (!IndexCodec::decode(command.nextIndex.get(), next, result.error) ||
		    next.currentConfigId == command.configId ||
		    std::any_of(next.configurations.begin(), next.configurations.end(),
				[&](const auto &entry) { return entry.id == command.configId; })) {
			if (result.error.empty()) {
				result.error = "删除索引与当前配置不一致";
			}
			break;
		}
		if (!saveIndexInIoThread(std::move(command.nextIndex), result.error)) {
			break;
		}
		std::error_code removeError;
		std::filesystem::remove_all(rootDirectory_ / "configurations" /
						    ("v" + std::to_string(kConfigurationVersion)) / command.configId,
					    removeError);
		if (removeError) {
			result.error = "删除配置目录失败: " + removeError.message();
			break;
		}
		result.success = true;
		break;
	}
	case CommandType::LoadIndex:
		result.success = loadIndexInIoThread(result.error);
		if (result.success) {
			result.index = StorageWorker::copyJson(indexData_.get());
		}
		break;
	case CommandType::DiscardUnindexed:
		result.success = discardUnindexedInIoThread(command.configId, result.error);
		break;
	case CommandType::Discard:
		break;
	}
	return result;
}

bool StorageWorker::createInIoThread(const std::string &configId, std::unordered_map<std::string, ObsData> modules,
				     std::string &error)
{
	if (modules.size() != moduleModes_.size()) {
		error = "新建配置缺少模块初始快照";
		return false;
	}
	for (const auto &[moduleKey, mode] : moduleModes_) {
		const auto module = modules.find(moduleKey);
		if (module == modules.end() || !module->second) {
			error = "新建配置缺少模块: " + moduleKey;
			return false;
		}
	}
	const auto directory =
		rootDirectory_ / "configurations" / ("v" + std::to_string(kConfigurationVersion)) / configId;
	const auto stagingDirectory = std::filesystem::path(directory.string() + ".creating");
	std::error_code fileError;
	if (std::filesystem::exists(directory, fileError) || fileError) {
		error = "配置目录已存在或不可访问: " + directory.string();
		return false;
	}
	std::filesystem::create_directories(directory.parent_path(), fileError);
	if (fileError || !std::filesystem::create_directory(stagingDirectory, fileError)) {
		error = "无法创建配置临时目录: " + stagingDirectory.string();
		return false;
	}
	// 临时目录内写齐所有文件后才提交，失败不暴露半份新配置，也不覆盖现有配置。
	ObsData mainConfig(obs_data_create());
	obs_data_set_string(mainConfig.get(), "config_id", configId.c_str());
	bool success = true;
	for (const auto &[moduleKey, mode] : moduleModes_) {
		if (mode == StorageMode::MainConfig) {
			obs_data_set_obj(mainConfig.get(), moduleKey.c_str(), modules.at(moduleKey).get());
		} else {
			const auto fileName = moduleKey + ".json";
			ObsData reference(obs_data_create());
			obs_data_set_string(reference.get(), "storage", "file");
			obs_data_set_string(reference.get(), "path", fileName.c_str());
			obs_data_set_obj(mainConfig.get(), moduleKey.c_str(), reference.get());
			if (!obs_data_save_json_pretty_safe(modules.at(moduleKey).get(),
							    (stagingDirectory / fileName).string().c_str(), "tmp",
							    "bak")) {
				success = false;
			}
		}
	}
	if (success) {
		success = obs_data_save_json_pretty_safe(
			mainConfig.get(), (stagingDirectory / "config.json").string().c_str(), "tmp", "bak");
	}
	if (success) {
		std::filesystem::rename(stagingDirectory, directory, fileError);
		success = !fileError;
	}
	if (!success) {
		error = "创建配置文件失败: " + directory.string();
		std::filesystem::remove_all(stagingDirectory, fileError);
	}
	if (success) {
		unindexedConfigurations_.insert(configId);
	}
	return success;
}

bool StorageWorker::loadInIoThread(const std::string &configId, LoadedConfiguration &loaded, std::string &error)
{
	auto candidate = std::make_unique<IoState>();
	candidate->mainConfigFile = rootDirectory_ / "configurations" / ("v" + std::to_string(kConfigurationVersion)) /
				    configId / "config.json";
	candidate->mainConfigData.reset(
		obs_data_create_from_json_file_safe(candidate->mainConfigFile.string().c_str(), "bak"));
	if (!candidate->mainConfigData ||
	    std::string(obs_data_get_string(candidate->mainConfigData.get(), "config_id")) != configId) {
		error = "主配置加载失败或配置 ID 不匹配: " + candidate->mainConfigFile.string();
		return false;
	}
	for (const auto &[moduleKey, mode] : moduleModes_) {
		ObsData module(obs_data_get_obj(candidate->mainConfigData.get(), moduleKey.c_str()));
		if (!module) {
			error = "配置缺少模块对象: " + moduleKey;
			return false;
		}
		if (mode == StorageMode::SeparateFile) {
			const auto fileName = moduleKey + ".json";
			if (std::string(obs_data_get_string(module.get(), "storage")) != "file" ||
			    std::string(obs_data_get_string(module.get(), "path")) != fileName) {
				error = "独立模块文件引用无效: " + moduleKey;
				return false;
			}
			auto &separateFile = candidate->separateFiles[moduleKey];
			separateFile.file = candidate->mainConfigFile.parent_path() / fileName;
			separateFile.data.reset(
				obs_data_create_from_json_file_safe(separateFile.file.string().c_str(), "bak"));
			if (!separateFile.data) {
				error = "独立模块加载失败: " + separateFile.file.string();
				return false;
			}
		}
	}
	LoadedConfiguration result;
	result.configId = configId;
	// 增加引用不等于复制；跨线程数据包必须与 IO 缓存完全独立。
	result.mainConfig.reset(obs_data_create_from_json(obs_data_get_json(candidate->mainConfigData.get())));
	for (const auto &[moduleKey, separateFile] : candidate->separateFiles) {
		ObsData copy(obs_data_create_from_json(obs_data_get_json(separateFile.data.get())));
		if (!copy) {
			error = "复制模块快照失败: " + moduleKey;
			return false;
		}
		result.separateModules.emplace(moduleKey, std::move(copy));
	}
	if (!result.mainConfig) {
		error = "复制主配置快照失败";
		return false;
	}
	candidateState_ = std::move(candidate);
	candidateConfigId_ = configId;
	candidateToken_ = ++nextCandidateToken_;
	{
		std::lock_guard lock(mutex_);
		candidateRevision_ = memoryRevision_;
		// 候选从磁盘建立且无修改，加载本身不增加修改版本或启动保存。
		candidateState_->revision = savedRevision_;
	}
	result.candidateToken = candidateToken_;
	loaded = std::move(result);
	return true;
}

bool StorageWorker::loadIndexInIoThread(std::string &error)
{
	if (indexData_) {
		return true;
	}
	const auto indexFile = rootDirectory_ / "index.json";
	for (const auto &file : {indexFile, std::filesystem::path(indexFile.string() + ".bak")}) {
		ObsData json(obs_data_create_from_json_file(file.string().c_str()));
		if (!json) {
			continue;
		}
		// 不允许用旧备份或目录扫描绕过不兼容版本，防止降级覆盖有效索引。
		if (hasUnsupportedVersion(json.get())) {
			error = "索引或配置版本不兼容，原文件已保留: " + file.string();
			return false;
		}
		IndexState decoded;
		if (IndexCodec::decode(json.get(), decoded, error)) {
			indexData_ = std::move(json);
			recoveredIndex_ = file != indexFile;
			error.clear();
			return true;
		}
	}
	// 主索引和备份均不可用时才扫描目录；扫描只能恢复 ID，名称使用不重复的默认名。
	IndexState recovered;
	const auto versionsDirectory = rootDirectory_ / "configurations";
	const auto currentDirectory = versionsDirectory / ("v" + std::to_string(kConfigurationVersion));
	std::error_code fileError;
	int highestOlderVersion = 0;
	if (std::filesystem::exists(versionsDirectory, fileError)) {
		for (const auto &entry : std::filesystem::directory_iterator(versionsDirectory, fileError)) {
			const auto name = entry.path().filename().string();
			int version = 0;
			if (name.size() > 1 && name[0] == 'v') {
				const auto parsed =
					std::from_chars(name.data() + 1, name.data() + name.size(), version);
				if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
				    version > 0 && version < kConfigurationVersion) {
					highestOlderVersion = std::max(highestOlderVersion, version);
				}
			}
		}
	}
	if (fileError) {
		error = "无法扫描配置版本目录: " + fileError.message();
		return false;
	}
	std::vector<std::string> ids;
	if (std::filesystem::exists(currentDirectory, fileError)) {
		for (const auto &entry : std::filesystem::directory_iterator(currentDirectory, fileError)) {
			if (!entry.is_directory()) {
				continue;
			}
			const auto id = entry.path().filename().string();
			if (!isSafeKey(id)) {
				continue;
			}
			LoadedConfiguration loaded;
			std::string loadError;
			if (loadInIoThread(id, loaded, loadError)) {
				ids.push_back(id);
			}
		}
	}
	candidateState_.reset();
	if (fileError) {
		error = "无法扫描配置目录: " + fileError.message();
		return false;
	}
	if (ids.empty() && highestOlderVersion > 0) {
		error = "检测到旧版配置，当前最小实现尚无该布局的迁移步骤，原目录已保留";
		return false;
	}
	if (ids.size() > kMaximumConfigurationCount) {
		error = "恢复的配置超过 50 个，请先整理配置目录";
		return false;
	}
	std::sort(ids.begin(), ids.end());
	for (const auto &id : ids) {
		recovered.configurations.push_back(
			{id, "恢复配置 " + std::to_string(recovered.configurations.size() + 1), {}});
	}
	if (!ids.empty()) {
		recovered.currentConfigId = ids.front();
	}
	indexData_ = IndexCodec::encode(recovered);
	recoveredIndex_ = true;
	error.clear();
	return true;
}

bool StorageWorker::saveIndexInIoThread(ObsData nextIndex, std::string &error)
{
	if (!loadIndexInIoThread(error)) {
		return false;
	}
	IndexState decoded;
	if (!IndexCodec::decode(nextIndex.get(), decoded, error)) {
		return false;
	}
	// 保留磁盘中尚未认识的扩展元数据，只覆盖 Service 管理的已知字段。
	ObsData merged = StorageWorker::copyJson(indexData_.get());
	obs_data_apply(merged.get(), nextIndex.get());
	ObsData entries(obs_data_create());
	ObsData oldEntries(indexData_ ? obs_data_get_obj(indexData_.get(), "configurations") : nullptr);
	ObsData nextEntries(obs_data_get_obj(nextIndex.get(), "configurations"));
	for (const auto &entry : decoded.configurations) {
		ObsData oldEntry(oldEntries ? obs_data_get_obj(oldEntries.get(), entry.id.c_str()) : nullptr);
		ObsData nextEntry(obs_data_get_obj(nextEntries.get(), entry.id.c_str()));
		auto item = StorageWorker::copyJson(oldEntry.get());
		obs_data_apply(item.get(), nextEntry.get());
		obs_data_set_obj(entries.get(), entry.id.c_str(), item.get());
	}
	obs_data_set_obj(merged.get(), "configurations", entries.get());
	if (indexData_ && !recoveredIndex_ &&
	    std::string(obs_data_get_json(indexData_.get())) == obs_data_get_json(merged.get())) {
		return true;
	}
	std::error_code directoryError;
	std::filesystem::create_directories(rootDirectory_, directoryError);
	if (directoryError ||
	    !obs_data_save_json_pretty_safe(merged.get(), (rootDirectory_ / "index.json").string().c_str(), "tmp",
					    recoveredIndex_ ? nullptr : "bak")) {
		error = "保存索引失败: " + (rootDirectory_ / "index.json").string();
		return false;
	}
	indexData_ = std::move(merged);
	recoveredIndex_ = false;
	return true;
}

bool StorageWorker::discardUnindexedInIoThread(const std::string &configId, std::string &error)
{
	ObsData entries(indexData_ ? obs_data_get_obj(indexData_.get(), "configurations") : nullptr);
	ObsData indexed(entries ? obs_data_get_obj(entries.get(), configId.c_str()) : nullptr);
	if (indexed || !unindexedConfigurations_.count(configId)) {
		error = "不能清理已有索引项或非本次创建的配置";
		return false;
	}
	if (candidateConfigId_ == configId) {
		candidateState_.reset();
	}
	std::error_code removeError;
	std::filesystem::remove_all(rootDirectory_ / "configurations" / ("v" + std::to_string(kConfigurationVersion)) /
					    configId,
				    removeError);
	if (removeError) {
		error = "清理未提交配置失败: " + removeError.message();
		return false;
	}
	unindexedConfigurations_.erase(configId);
	return true;
}

void StorageWorker::updateCache(SaveTask task)
{
	if (!ioState_->mainConfigData) {
		ioState_->mainConfigData.reset(obs_data_create());
		obs_data_set_string(ioState_->mainConfigData.get(), "config_id", task.configId.c_str());
		ioState_->mainConfigFile = rootDirectory_ / "configurations" /
					   ("v" + std::to_string(kConfigurationVersion)) / task.configId /
					   "config.json";
		ioState_->mainConfigDirty = true;
	}
	if (task.mode == StorageMode::MainConfig) {
		// 主配置模块直接替换自己的节点，始终保留一个完整主配置 JSON。
		obs_data_set_obj(ioState_->mainConfigData.get(), task.moduleKey.c_str(), task.data.get());
		ioState_->mainConfigDirty = true;
	} else {
		auto [separateFileIterator, inserted] = ioState_->separateFiles.try_emplace(task.moduleKey);
		auto &separateFile = separateFileIterator->second;
		if (inserted) {
			const auto fileName = task.moduleKey + ".json";
			separateFile.file = ioState_->mainConfigFile.parent_path() / fileName;
			// 独立文件首次进入缓存时，主配置只记录引用，不嵌入该模块的完整内容。
			ObsData reference(obs_data_create());
			obs_data_set_string(reference.get(), "storage", "file");
			obs_data_set_string(reference.get(), "path", fileName.c_str());
			obs_data_set_obj(ioState_->mainConfigData.get(), task.moduleKey.c_str(), reference.get());
			ioState_->mainConfigDirty = true;
		}
		separateFile.data = std::move(task.data);
		separateFile.revision = task.revision;
		separateFile.dirty = true;
	}
	ioState_->revision = std::max(ioState_->revision, task.revision);
	if (!ioState_->saveScheduled) {
		ioState_->saveScheduled = true;
		ioState_->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	}
}

bool StorageWorker::flushInIoThread()
{
	if (!ioState_->hasDirtyFiles()) {
		return true;
	}
	std::string error;
	auto save = [&error](obs_data_t *data, const std::filesystem::path &file, bool &dirty) {
		if (!dirty) {
			return true;
		}
		std::error_code directoryError;
		std::filesystem::create_directories(file.parent_path(), directoryError);
		if (directoryError || !obs_data_save_json_pretty_safe(data, file.string().c_str(), "tmp", "bak")) {
			error = "保存配置失败: " + file.string();
			if (directoryError) {
				error += " (" + directoryError.message() + ")";
			}
			return false;
		}
		dirty = false;
		return true;
	};
	// 每个文件单独清除成功标记；一个独立文件失败不应阻止其他文件尝试保存。
	bool success = save(ioState_->mainConfigData.get(), ioState_->mainConfigFile, ioState_->mainConfigDirty);
	for (auto &[moduleKey, separateFile] : ioState_->separateFiles) {
		if (!save(separateFile.data.get(), separateFile.file, separateFile.dirty)) {
			success = false;
		}
	}
	ioState_->saveScheduled = !success;
	if (!success) {
		// 失败文件保留完整快照和 dirty，一秒后重试；成功文件不重复写入。
		ioState_->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	}
	std::lock_guard lock(mutex_);
	if (success) {
		// 全部相关文件成功才推进，避免成功文件的高版本掩盖其他文件的失败。
		savedRevision_ = std::max(savedRevision_, ioState_->revision);
	}
	lastError_ = std::move(error);
	return success;
}

void StorageWorker::run()
{
	if (!ioState_) {
		ioState_ = std::make_unique<IoState>();
	}
	std::unique_lock lock(mutex_);
	for (;;) {
		// 没有命令、快照或到期保存时休眠；收到通知后重新检查条件，避免遗漏并发提交。
		auto ready = [this] {
			return workerState_ == WorkerState::Stopping || !commands_.empty() || !pendingTasks_.empty() ||
				(ioState_->saveScheduled && std::chrono::steady_clock::now() >= ioState_->deadline);
		};
		if (!ready()) {
			if (ioState_->saveScheduled) {
				cv_.wait_until(lock, ioState_->deadline, ready);
			} else {
				cv_.wait(lock, ready);
			}
		}

		if (!commands_.empty()) {
			auto command = std::move(commands_.front());
			commands_.pop_front();
			lock.unlock();
			for (auto &[taskKey, task] : command.precedingTasks) {
				updateCache(std::move(task));
			}
			Result result;
			try {
				result = processCommand(command);
			} catch (const std::exception &exception) {
				result.error = exception.what();
			}
			if (command.type == CommandType::Load || command.type == CommandType::Activate) {
				std::lock_guard switchingLock(mutex_);
				switching_ = false;
			}
			command.completion.set_value(std::move(result));
			lock.lock();
			continue;
		}
		if (!pendingTasks_.empty()) {
			PendingTasks tasks;
			tasks.swap(pendingTasks_);
			lock.unlock();
			for (auto &[taskKey, task] : tasks) {
				updateCache(std::move(task));
			}
			lock.lock();
		}
		if (workerState_ == WorkerState::Stopping) {
			lock.unlock();
			flushInIoThread();
			return;
		}
		if (ioState_->saveScheduled && std::chrono::steady_clock::now() >= ioState_->deadline) {
			lock.unlock();
			flushInIoThread();
			lock.lock();
			continue;
		}
	}
}

bool StorageWorker::stop(std::string &error)
{
	{
		std::lock_guard lock(mutex_);
		if (workerState_ == WorkerState::Stopped) {
			error = lastError_;
			return !ioState_ || !ioState_->hasDirtyFiles();
		}
		// 只发出停止请求；真正退出由 IO 线程完成，stop() 随后通过 join 等待收尾结束。
		workerState_ = WorkerState::Stopping;
	}
	cv_.notify_one();
	thread_.join();
	std::lock_guard lock(mutex_);
	workerState_ = WorkerState::Stopped;
	error = lastError_;
	return !ioState_->hasDirtyFiles();
}

std::uint64_t StorageWorker::memoryRevision() const
{
	std::lock_guard lock(mutex_);
	return memoryRevision_;
}
std::uint64_t StorageWorker::savedRevision() const
{
	std::lock_guard lock(mutex_);
	return savedRevision_;
}
std::string StorageWorker::lastError() const
{
	std::lock_guard lock(mutex_);
	return lastError_;
}
} // namespace xbotgo
