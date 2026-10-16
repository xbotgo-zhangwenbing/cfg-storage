# StorageWorker 存储线程设计

本文档记录当前已经确认的正式实现方案。`src/` 下的代码是可迁移到实际项目的正式实现，`demo/` 只用于验证功能，不应反向决定存储层结构。

## 一、职责

`StorageWorker` 负责：

- 接收配置模块提交的最新快照；
- 在 IO 线程中维护配置内存状态；
- 合并短时间内的重复修改；
- 延时写入配置文件；
- 配置切换、重载和退出时执行同步保存；
- JSON 保存成功后清理旧图片资源。

业务配置对象的锁和业务语义由各自配置实现类负责。`StorageWorker` 不关心图像、篮球等业务，只处理 `SaveTask` 和 `obs_data_t`。

## 二、线程模型

存储层使用标准 C++ 线程，不使用 `QThread`、`QTimer` 或 `QMetaObject::invokeMethod`。

核心成员：

```cpp
std::thread thread_;
std::mutex mutex_;
std::condition_variable condition_;
```

IO 线程执行 `run()`：

1. 没有待处理任务时，使用条件变量睡眠，不进行轮询；
2. 收到任务后，一次性交换当前全部待处理任务；
3. 在同一次循环中合并这些任务到 IO 内存；
4. 首次修改设置一秒保存截止时间；
5. 到期后统一落盘；
6. 新任务到达会唤醒等待中的线程，但不会重新开始一秒计时。

`condition_variable::wait_until()` 是 IO 线程的休眠等待，不是主线程阻塞，也不会导致 CPU 空转。新的入队任务会通过 `notify_one()` 唤醒它。

## 三、任务模型

```cpp
struct SaveTask {
    std::string key;
    std::filesystem::path file;
    std::string moduleKey;
    StorageMode mode;
    std::uint64_t revision;
    obs_data_t *data;
    std::vector<std::filesystem::path> deleteAfterSave;
};
```

任务按 `key` 合并，而不是无条件追加：

```cpp
std::unordered_map<std::string, SaveTask> pendingTasks_;
```

同一个 key 再次提交时：

- 只保留 revision 较新的任务；
- 合并两个任务的 `deleteAfterSave`；
- 旧任务的 `obs_data_t` 由 `SaveTask` 的移动析构逻辑释放。

这样连续修改十次时，事件循环只需要处理这一批 key 的最终快照，而不是逐个写入。

## 四、IO 内存状态

IO 线程只拥有和修改以下状态：

```cpp
obs_data_t *mainConfigData_;
std::filesystem::path mainConfigFile_;
bool mainConfigDirty_;
std::uint64_t mainRevision_;

std::unordered_map<std::string, SeparateFileData> separateFiles_;
```

### 按存储模式处理任务

`SaveTask::mode` 决定任务如何进入 IO 内存：

```cpp
if (task.mode == StorageMode::MainConfig) {
    // 将模块节点合并到完整主配置对象。
    obs_data_set_obj(mainConfigData_, task.moduleKey.c_str(), task.data);
} else {
    // 保存该独立文件的路径和完整 obs_data_t 快照。
    separateFiles_[task.key] = {
        task.file,
        task.data,
        task.revision,
        task.deleteAfterSave,
    };
}
```

因此，`StorageMode::SeparateFile` 不能只记录文件路径，也必须保留任务携带的 `obs_data_t *data`。该对象就是这个独立文件的完整内容，落盘时直接使用它：

```cpp
obs_data_save_json_pretty_safe(
    separateFile.data,
    separateFile.file.string().c_str(),
    "tmp",
    "bak");
```

### 主配置

主配置使用一个完整的 `obs_data_t`：

```cpp
obs_data_set_obj(mainConfigData_, task.moduleKey.c_str(), task.data);
```

这表示替换主配置中的模块节点，而不是在 map 中为每个模块创建一个文件缓存。

主配置落盘时直接保存完整的 `mainConfigData_`：

```cpp
obs_data_save_json_pretty_safe(
    mainConfigData_,
    mainConfigFile_.string().c_str(),
    "tmp",
    "bak");
```

不需要从磁盘重新读取 JSON，也不需要每次落盘前重新创建完整对象。

首次加载、切换配置或重载配置时，释放旧的 `mainConfigData_`，根据当前业务内存重新建立完整快照。

### 独立文件

独立文件模块在 `separateFiles_` 中保存最新的 `obs_data_t`、路径、revision 和待清理资源。每个独立文件的 `data` 都是该文件的完整快照，落盘时分别写入对应路径，不需要再次从文件读取旧内容。

## 五、同步接口

### `syncCache(SaveTask task)`

用于首次加载、切换配置和重载配置：

- 更新 IO 线程内存；
- 不标记 dirty；
- 不启动延时保存；
- 不应从磁盘重新读取文件。

为了保证 IO 状态只由 IO 线程修改，正式实现应将同步任务提交给 IO 线程，并等待处理完成，而不是由调用线程直接修改 `mainConfigData_`。

### `queueSave(SaveTask task)`

用于普通配置修改：

- 放入 `pendingTasks_`；
- 按 key 合并；
- 唤醒 IO 线程；
- 标记对应 IO 内存为 dirty；
- 第一次修改启动一秒延时保存。

调用方不需要了解 dirty、阻塞或线程调度细节。

### `flush()`

只作为内部同步屏障，不对外暴露：

- 立即处理已经提交的任务；
- 立即保存主配置和独立文件；
- 等待 IO 线程完成；
- 用于切换、创建、删除、重载和退出。

## 六、落盘顺序

一次落盘流程如下：

1. 处理当前批次全部任务；
2. 根据 `StorageMode` 更新 `mainConfigData_` 或 `separateFiles_`；
3. 保存完整主配置文件 `mainConfigData_`；
4. 使用每个独立文件缓存中的 `data` 保存对应路径；
5. 所有相关 JSON 保存成功后，删除 `deleteAfterSave` 中的旧图片；
6. 保存成功的缓存清除 dirty 标记；
7. 更新 `savedRevision_`。

如果 JSON 保存失败，不删除旧图片，也不能清除 dirty 状态。`.tmp` 用于临时文件，`.bak` 保留用于恢复，不主动删除。

## 七、命名约定

避免使用单字母变量：

```cpp
auto &pendingTask = pendingTasks_[task.key];
for (auto &[taskKey, separateFile] : separateFiles_) {
    ...
}
```

推荐命名：

- `mainConfigData_`：完整主配置 `obs_data_t`；
- `pendingTasks_`：尚未被 IO 线程处理的任务；
- `separateFiles_`：独立文件的 IO 内存缓存；
- `separateFile`：单个独立文件状态；
- `oldAssetPath`：保存成功后待删除的旧资源路径。

## 八、明确不采用的方案

- 不在每次保存时从磁盘重新读取 JSON；
- 不为主配置的每个模块建立独立的 `CachedFile`；
- 不让主线程直接修改 IO 线程拥有的 `obs_data_t`；
- 不让每个任务都投递一个独立 Qt lambda；
- 不使用 Qt 定时器实现存储线程；
- 不把 `flush()` 作为公共业务接口。
