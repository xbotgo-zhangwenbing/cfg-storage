#pragma once

#include "ObsData.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xbotgo {
enum class StorageMode { MainConfig, SeparateFile };

// 返回给业务线程的是独立副本，不能与 IO 候选缓存共享可变 JSON。
struct LoadedConfiguration {
	std::string configId;
	std::uint64_t candidateToken = 0;
	ObsData mainConfig;
	std::unordered_map<std::string, ObsData> separateModules;
};

class StorageWorker final {
public:
	StorageWorker();
	~StorageWorker();

public:
	// 组装存储结构并且启动线程。
	bool start(const std::filesystem::path &rootDirectory, std::string &error);
	bool createConfiguration(const std::string &configId, std::unordered_map<std::string, ObsData> initialModules,
				 std::string &error);
	bool loadIndex(ObsData &index, std::string &error);
	bool renameConfiguration(const std::string &configId, ObsData nextIndex, std::string &error);
	bool deleteConfiguration(const std::string &configId, ObsData nextIndex, std::string &error);
	// 仅清理本次进程新建、尚未写入索引的配置，不能删除已有配置。
	bool discardUnindexedConfiguration(const std::string &configId, std::string &error);
	// 加载不改变当前配置；业务校验完成后，凭令牌激活最近一次加载的候选。
	bool loadConfiguration(const std::string &configId, LoadedConfiguration &loaded, std::string &error);
	bool activateConfiguration(const std::string &configId, std::uint64_t candidateToken, ObsData nextIndex,
				   std::string &error);
	bool discardCandidate(std::uint64_t candidateToken, std::string &error);

	// 只等待入队；Worker 分配版本号并接管快照，实际文件操作只在 IO 线程发生。
	bool submitModule(const std::string &configId, const std::string &moduleKey, ObsData snapshot);
	bool stop(std::string &error);
	std::uint64_t memoryRevision() const;
	std::uint64_t savedRevision() const;
	std::string lastError() const;

private:
	struct SaveTask {
		std::string configId;
		std::string moduleKey;
		std::uint64_t revision;
		StorageMode mode;
		ObsData data;
	};
	using PendingTasks = std::unordered_map<std::string, SaveTask>;
	struct Result {
		bool success = false;
		std::string error;
		LoadedConfiguration loaded;
		ObsData index;
	};
	enum class CommandType {
		Create,           // 创建配置及其初始模块数据。
		Rename,           // 重命名已有配置，并迁移对应的存储目录或文件。
		Delete,           // 删除指定配置及其持久化数据。
		Load,             // 加载指定配置的完整内容。
		Activate,         // 激活候选配置，将候选数据写入当前生效版本。
		Discard,          // 丢弃指定候选版本及其未生效的数据。
		LoadIndex,        // 读取配置索引，不加载各配置的完整模块数据。
		DiscardUnindexed  // 清理存储中未被索引记录引用的配置数据。
	};
	struct Command {
		CommandType type = CommandType::Load;
		std::string configId;
		std::unordered_map<std::string, ObsData> initialModules;
		ObsData nextIndex;
		std::uint64_t candidateToken = 0;
		PendingTasks precedingTasks; // 保存屏障：命令不能越过此前已提交的修改。
		std::promise<Result> completion;
	};
	class IoState;
	enum class WorkerState {
		Stopped,  // IO 线程未运行，或者已完成停止收尾。
		Running,  // IO 线程正常运行，可以提交命令和快照。
		Stopping  // 已请求停止，IO 线程正在处理剩余任务并尝试落盘。
	};
	static bool isSafeKey(const std::string &key);
	static ObsData copyJson(obs_data_t *data);
	static bool hasUnsupportedVersion(obs_data_t *data);

private:
	Result submit(Command command);
	void run();
	Result processCommand(Command &command);
	bool createInIoThread(const std::string &configId, std::unordered_map<std::string, ObsData> modules,
			      std::string &error);
	bool loadInIoThread(const std::string &configId, LoadedConfiguration &loaded, std::string &error);
	void updateCache(SaveTask task);
	bool flushInIoThread();
	bool loadIndexInIoThread(std::string &error);
	bool saveIndexInIoThread(ObsData nextIndex, std::string &error);
	bool discardUnindexedInIoThread(const std::string &configId, std::string &error);

private:
	std::filesystem::path rootDirectory_;
	std::unordered_map<std::string, StorageMode> moduleModes_;
	std::thread thread_;
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::deque<Command> commands_;
	PendingTasks pendingTasks_;
	std::string activeConfigId_;
	// 受 mutex_ 保护；Running 表示可接收任务，Stopping 表示已请求停止但仍在处理队列和落盘。
	WorkerState workerState_ = WorkerState::Stopped;
	bool switching_ = false; // Load 或 Activate 执行期间拒绝旧配置的新快照。
	std::uint64_t memoryRevision_ = 0;
	std::uint64_t savedRevision_ = 0;
	std::string lastError_;

	// 运行期间仅由 IO 线程访问；保存失败时保留缓存和 dirty，下一次继续尝试。
	ObsData indexData_;
	bool recoveredIndex_ = false; // 从备份恢复时，首次重写主索引不能用损坏文件覆盖有效备份。
	std::unordered_set<std::string> unindexedConfigurations_;
	std::unique_ptr<IoState> ioState_;
	std::unique_ptr<IoState> candidateState_;
	std::string candidateConfigId_;
	std::uint64_t candidateToken_ = 0;
	std::uint64_t candidateRevision_ = 0;
	std::uint64_t nextCandidateToken_ = 0;
};
} // namespace xbotgo
