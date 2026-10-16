# XBotGo Config Storage Demo

Qt Widgets demo for the XBotGo configuration storage architecture. It uses libobs `obs_data_t` for JSON encoding and safe file writes.

## Prerequisites

- macOS 13 or newer on Apple Silicon
- CMake 3.28+
- Xcode
- A configured and built sibling `falconm-obs` checkout at `../falconm-obs`

The default configuration imports `OBS::libobs` from `../falconm-obs/build_macos`. Override `FALCONM_OBS_ROOT` and `FALCONM_OBS_BUILD_DIR` when the OBS checkout is elsewhere.

## Build

```bash
cmake -S . -B build_xcode -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0

cmake --build build_xcode \
  --config Debug \
  --target xbotgo-config-demo \
  --parallel 8
```

## Run

```bash
./build_xcode/Debug/xbotgo-config-demo
```

当前已完成五批最小实现：图像参数修改提交 JSON 快照，IO 线程合并修改并在首次修改约一秒后安全写入 `configurations/v4/<配置ID>/config.json`。新建会写入默认配置，双击列表切换会恢复对应参数；加载失败保留原配置。点击“重新加载”会先保存待写修改，再从磁盘读取当前配置。

四个参数编辑控件会在新建、切换、重载成功后刷新；只读文本仍仅在点击“读取内存”后显示快照。配置列表支持新建、重命名和删除；当前配置不能删除。重命名、删除和当前配置写入根目录 `index.json`，重启会恢复列表及当前参数。`ConfigurationSummary::createdAt` 提供 UTC ISO 8601 创建时间，对应索引的 `created_at`，重命名、切换和保存内容不会改变它；列表项提示会显示本地时间。旧索引缺少该字段或仅由目录扫描恢复时，时间为空，界面显示“未知”。

索引主文件损坏时尝试 `.bak`，两者均不可用时从 `configurations/v4/` 重建列表；扫描只能恢复 ID，原显示名称不能恢复。当前项损坏时回退到其他可用配置。索引写入失败不会提交重命名或切换，新建失败会清理本次未提交的目录。

本轮支持 index 格式 1 和配置布局 v4。旧布局未定义转换步骤，更高版本不能降级读取：这两种情况会报错并保留原文件，不自动覆盖。

详细进度见 [分批实现计划](doc/implementation-plan.md)。

模块存储方式在 `src/XBotGoConfigServiceImpl.cpp` 的 `kModuleStorage` 表中指定，当前图像模块默认写入主配置：

```cpp
{"image_parameters", StorageMode::MainConfig}
```

指定为 `StorageMode::SeparateFile` 时，快照写入 `image_parameters.json`，`config.json` 对应节点记录：

```json
"image_parameters": { "storage": "file", "path": "image_parameters.json" }
```

模块只提交 JSON 快照，Worker 根据固定描述决定保存位置。存储布局在程序启动时注册，不支持运行中更改。

## 整份配置替换通知

`IImageParametersConfig::changed(ImageParametersData)` 只在初始化恢复、新建、切换、重载成功并替换整份业务内存后通知，普通 setter、重命名、失败的切换及 IO 自动保存不会触发它。

模块将替换时的完整快照投递到自己的 Qt 线程，释放配置锁和模块锁后通过事件队列依次发送。模块初始化应在应用主线程进行；get/set 和 Service 管理操作可在其他线程调用。类型已注册，接收方可以使用跨线程 QueuedConnection。

Demo 监听该信号，直接用参数回填图像控件、刷新配置列表并清空旧的读取文本，不再在新建/切换/重载按钮中重新 getAll。初次订阅仍读取一次当前值；“读取内存”文本框继续只在点击时更新，普通 setter 不会清空它。

配置访问统一按“配置门锁 → 模块锁”加锁。Service 管理操作取得独占门锁，模块 getter/setter 取得共享门锁，再获取各自数据锁；不混用相反顺序，也不在持锁时直接调用信号槽。

## JSON 格式校验

`src/common/StorageValidation.h` 统一校验字段，Codec 用 `constexpr ObsDataField[]` 声明 key 和 OBS 类型，再调用一次 `StorageValidation::validateObsDataFormat(json, format, error)`。字符串、数字、布尔、对象和数组使用 OBS 原生类型；默认字段必填，也支持可选字段以及整数/浮点数的限制。

函数一次检查全部声明字段，报告所有缺失和类型错误；未声明的扩展字段不删除、不拒绝。格式检查只认实际 JSON 值，清除值后残留的 OBS item 不算有效字段。枚举值、版本值及名称唯一性仍由各 Codec 做业务校验。图像 Codec 和索引 Codec 共用这一套校验。

## 枚举与字符串映射

通用宏和转换模板位于 `include/xbotgo/storage/common/EnumString.h`，映射直接放在 `ImageParametersTypes.h` 每个枚举的下方，无需额外映射头文件，也不用手写模板特化、容器类型或数量。

```cpp
enum class ImageEncoding { H264, H265 };
XBOTGO_ENUM_STRINGS(ImageEncoding,
    {ImageEncoding::H264, "H264"},
    {ImageEncoding::H265, "H265"});
```

宏在 `xbotgo` 命名空间内使用。转换和读取方式不变：

```cpp
constexpr auto text = enumToString(ImageEncoding::H265); // "H265"
constexpr auto value = enumFromString<ImageEncoding>("H265"); // optional<ImageEncoding>
```

常量输入可编译期求值；运行期输入查静态表。未知枚举返回空 string_view，未知字符串返回 nullopt；映射中的空字符串、重复枚举或重复字符串会编译失败。JSON 字符串直接与枚举项名称一致，如 `P4K_30`、`H265`、`Hz50`；图像 Codec 仅接受枚举原名，不保留旧 Demo 字符串的兼容分支。开发阶段不兼容的旧配置清理后重建。

## CLion Debugging

Open this repository root in CLion and enable the `CLion Debug` CMake preset. The preset uses `Unix Makefiles`, builds into `cmake-build-debug`, and imports libobs and Qt from the sibling `../falconm-obs` checkout.

Select the `xbotgo-config-demo` run configuration and use Debug. No environment variables are required because the executable build RPATH contains the libobs, OBS dependency, and Qt framework directories.

本批可使用以下断点检查调用流程：

- `xbotgo::XBotGoConfigService::createConfiguration()`：内存索引校验和更新。
- `xbotgo::StorageWorker::submit()`：管理命令入队和同步等待。
- `xbotgo::StorageWorker::processCommand()`：管理命令的内部保存屏障。
- `xbotgo::ImageParametersConfigImpl::setAll()`：整体图像数据校验和提交。
- `xbotgo::ImageParametersConfigImpl::setSpecification()`：UI 修改规格后更新业务内存。
- `xbotgo::StorageWorker::submitModule()`：快照入队、任务合并和版本号分配。
- `xbotgo::StorageWorker::updateCache()`：IO 线程更新完整主配置。
- `xbotgo::StorageWorker::flushInIoThread()`：安全写入 JSON 和失败重试。
- `xbotgo::StorageWorker::createInIoThread()`：写齐默认文件后提交新目录。
- `xbotgo::StorageWorker::loadInIoThread()`：读取候选缓存并返回独立 JSON 副本。
- `xbotgo::XBotGoConfigService::Impl::loadAndActivate()`：解码校验后激活，并替换业务内存。
- `xbotgo::StorageWorker::loadIndexInIoThread()`：主索引、备份及目录扫描恢复。
- `xbotgo::StorageWorker::saveIndexInIoThread()`：索引持久化与扩展元数据保留。

运行保存流程检查：

```bash
cmake --build cmake-build-debug --parallel 4
ctest --test-dir cmake-build-debug --output-on-failure
```

测试还覆盖索引写入失败时的重命名/切换回滚、新建目录清理及重启恢复。
