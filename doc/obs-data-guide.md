# OBS Data 接口分析

## 1. 文档目的

本文分析 `libobs/obs-data.h` 提供的数据接口，说明其对象模型、引用计数、JSON 序列化、文件读写、默认值、数组、字段遍历、线程安全和常见误用。

相关源码：

```text
libobs/obs-data.h
libobs/obs-data.c
libobs/util/platform.c
libobs/util/platform-nix.c
```

## 2. `obs_data_t` 是什么

`obs_data_t` 是 libobs 提供的通用键值数据对象，主要用于：

- Source、Output、Encoder、Service 等 OBS 对象的 settings。
- OBS 前端和插件的结构化配置。
- JSON 字符串序列化和反序列化。
- JSON 文件加载和保存。
- 表示嵌套对象和对象数组。

它可以理解为一个支持引用计数的 JSON Object。

支持的数据类型：

| OBS 类型 | JSON 类型 | C/C++ 常用值 |
| --- | --- | --- |
| `OBS_DATA_NULL` | `null` | 无值 |
| `OBS_DATA_STRING` | string | `const char *` |
| `OBS_DATA_NUMBER` | number | `long long` 或 `double` |
| `OBS_DATA_BOOLEAN` | boolean | `bool` |
| `OBS_DATA_OBJECT` | object | `obs_data_t *` |
| `OBS_DATA_ARRAY` | array | `obs_data_array_t *` |

内部结构的关键部分：

```c
struct obs_data {
    volatile long ref;
    char *json;
    struct obs_data_item *items;
};
```

- `ref` 是引用计数的存储位置。
- `json` 缓存最近一次生成的 JSON 字符串。
- `items` 保存对象的字段。
- `volatile` 不提供原子性，引用计数的原子性来自 `os_atomic_inc_long()` 和 `os_atomic_dec_long()`。
- 引用计数是原子的，不代表对象字段读写是线程安全的。

## 3. 三种核心对象

### 3.1 `obs_data_t`

表示 JSON Object：

```json
{
  "name": "比赛配置",
  "enabled": true
}
```

### 3.2 `obs_data_array_t`

表示 JSON Array，但数组成员只能是 `obs_data_t` 对象：

```json
[
  { "device_id": "camera-001" },
  { "device_id": "camera-002" }
]
```

不能直接向数组加入字符串或整数。需要把值包装在 `obs_data_t` 中。

### 3.3 `obs_data_item_t`

表示 `obs_data_t` 中的一个字段，主要用于：

- 遍历未知结构。
- 检查字段类型。
- 根据字段名取得 item。
- 修改或删除当前 item。

业务配置字段已知时，通常直接使用 `obs_data_get_*()`，不需要使用 item API。

## 4. 生命周期与所有权

### 4.1 创建和释放

```cpp
obs_data_t *data = obs_data_create();
// 初始引用计数为 1

obs_data_release(data);
// 引用计数降为 0 时销毁
```

增加引用：

```cpp
obs_data_addref(data);
```

数组使用对应接口：

```cpp
obs_data_array_t *array = obs_data_array_create();
obs_data_array_addref(array);
obs_data_array_release(array);
```

### 4.2 C++ RAII 包装

OBS 提供了 C++ 自动释放类型：

```cpp
#include <obs.hpp>

OBSDataAutoRelease data = obs_data_create();
OBSDataArrayAutoRelease array = obs_data_array_create();
```

离开作用域时会自动调用：

```cpp
obs_data_release();
obs_data_array_release();
```

项目 C++ 代码应优先使用 RAII 包装，避免遗漏释放或在异常路径提前返回时泄漏。

### 4.3 哪些返回值需要释放

| 函数 | 返回值所有权 | 调用方操作 |
| --- | --- | --- |
| `obs_data_create()` | 新引用 | `obs_data_release()` |
| `obs_data_create_from_json()` | 新引用 | `obs_data_release()` |
| `obs_data_create_from_json_file*()` | 新引用 | `obs_data_release()` |
| `obs_data_get_defaults()` | 新引用 | `obs_data_release()` |
| `obs_data_get_obj()` | 增加过引用 | `obs_data_release()` |
| `obs_data_get_array()` | 增加过引用 | `obs_data_array_release()` |
| `obs_data_array_item()` | 增加过引用 | `obs_data_release()` |
| `obs_data_first()` | 增加过 item 引用 | `obs_data_item_release()` 或完整迭代 |
| `obs_data_item_byname()` | 增加过 item 引用 | `obs_data_item_release()` |
| `obs_data_get_string()` | 借用指针 | 不释放，必要时立即复制 |
| `obs_data_get_json*()` | 借用内部缓存 | 不释放，必要时立即复制 |

### 4.4 `set_obj()` 和 `set_array()`

调用 `obs_data_set_obj()` 或 `obs_data_set_array()` 后，父对象会持有子对象引用。

```cpp
OBSDataAutoRelease root = obs_data_create();
OBSDataAutoRelease child = obs_data_create();

obs_data_set_string(child, "device_id", "camera-001");
obs_data_set_obj(root, "camera", child);
```

`child` 在当前作用域结束时可以正常释放，`root` 内部仍持有自己的引用。

数组加入对象时也会增加引用：

```cpp
OBSDataArrayAutoRelease array = obs_data_array_create();
OBSDataAutoRelease item = obs_data_create();

obs_data_array_push_back(array, item);
```

## 5. 创建和解析数据

### 5.1 创建空对象

```cpp
OBSDataAutoRelease data = obs_data_create();
```

### 5.2 从 JSON 字符串创建

```cpp
const char *json = R"({"name":"配置 A","enabled":true})";
OBSDataAutoRelease data = obs_data_create_from_json(json);

if (!data) {
    // JSON 语法错误、重复字段或解析失败
}
```

解析使用 Jansson，并启用了重复字段拒绝。输入中存在重复 key 时会失败。

### 5.3 从文件创建

```cpp
OBSDataAutoRelease data =
    obs_data_create_from_json_file("/path/config.json");
```

此函数同步执行文件读取和 JSON 解析。

文件不存在、无法读取或 JSON 非法时返回 `nullptr`。

### 5.4 从文件安全加载

```cpp
OBSDataAutoRelease data =
    obs_data_create_from_json_file_safe(
        "/path/config.json",
        "bak");
```

处理流程：

1. 尝试加载正式文件。
2. 正式文件加载失败时检查 `config.json.bak`。
3. 备份存在时，将备份重命名为正式文件。
4. 再次加载正式文件。

注意：这里是把备份文件移动回正式文件，不是复制；恢复成功后原 `.bak` 文件会被消耗。

## 6. 基础字段设置和读取

### 6.1 字符串

```cpp
const QByteArray nameUtf8 = name.toUtf8();
obs_data_set_string(data, "name", nameUtf8.constData());

const QString loadedName =
    QString::fromUtf8(obs_data_get_string(data, "name"));
```

`obs_data_set_string()` 会复制字符串内容，不会长期保存调用方传入的指针。

`obs_data_get_string()` 返回 OBS 对象内部指针：

- 不要调用 `bfree()` 或 `free()`。
- 对象字段修改或对象释放后，该指针可能失效。
- 需要长期保存时，立即转换为 `QString` 或 `std::string`。

### 6.2 整数

```cpp
obs_data_set_int(data, "score", 10);
const long long score = obs_data_get_int(data, "score");
```

JSON number 没有独立的整数类型，但 OBS 内部记录该 number 是 int 还是 double。

### 6.3 浮点数

```cpp
obs_data_set_double(data, "zoom", 1.5);
const double zoom = obs_data_get_double(data, "zoom");
```

读取时 OBS 允许 int 和 double 相互转换：

- `get_int()` 读取 double 时会截断转换。
- `get_double()` 读取 int 时会转换为 double。

### 6.4 布尔值

```cpp
obs_data_set_bool(data, "tracking_enabled", true);
const bool enabled = obs_data_get_bool(data, "tracking_enabled");
```

### 6.5 缺失字段的返回值

字段不存在或类型不匹配时，普通 getter 返回：

| getter | 返回值 |
| --- | --- |
| `obs_data_get_string()` | 空字符串 `""` |
| `obs_data_get_int()` | `0` |
| `obs_data_get_double()` | `0.0` |
| `obs_data_get_bool()` | `false` |
| `obs_data_get_obj()` | `nullptr` |
| `obs_data_get_array()` | `nullptr` |

因此不能只通过返回值判断字段是否存在，应使用：

```cpp
if (obs_data_has_user_value(data, "score")) {
    const long long score = obs_data_get_int(data, "score");
}
```

## 7. 嵌套对象

### 7.1 设置对象

```cpp
OBSDataAutoRelease progress = obs_data_create();
obs_data_set_int(progress, "home_score", 12);
obs_data_set_int(progress, "away_score", 8);

obs_data_set_obj(root, "progress", progress);
```

生成：

```json
{
  "progress": {
    "home_score": 12,
    "away_score": 8
  }
}
```

### 7.2 读取对象

```cpp
OBSDataAutoRelease progress =
    obs_data_get_obj(root, "progress");

if (progress) {
    const long long homeScore =
        obs_data_get_int(progress, "home_score");
}
```

`obs_data_get_obj()` 返回的新引用必须释放，因此推荐直接使用 `OBSDataAutoRelease`。

## 8. 数组

### 8.1 创建数组

```cpp
OBSDataArrayAutoRelease devices = obs_data_array_create();
```

### 8.2 加入对象

```cpp
OBSDataAutoRelease camera = obs_data_create();
obs_data_set_string(camera, "device_id", "camera-001");

const size_t newCount =
    obs_data_array_push_back(devices, camera);
```

### 8.3 设置到父对象

```cpp
obs_data_set_array(root, "devices", devices);
```

### 8.4 读取和遍历

```cpp
OBSDataArrayAutoRelease devices =
    obs_data_get_array(root, "devices");

const size_t count = obs_data_array_count(devices);
for (size_t index = 0; index < count; ++index) {
    OBSDataAutoRelease camera =
        obs_data_array_item(devices, index);

    const QString deviceId = QString::fromUtf8(
        obs_data_get_string(camera, "device_id"));
}
```

`obs_data_array_item()` 返回增加过引用的对象，必须释放。

### 8.5 数组修改接口

| 函数 | 用途 |
| --- | --- |
| `obs_data_array_push_back()` | 在末尾加入对象，返回新数量 |
| `obs_data_array_insert()` | 在指定位置插入对象 |
| `obs_data_array_push_back_array()` | 将另一个数组追加到当前数组 |
| `obs_data_array_erase()` | 删除指定位置对象 |
| `obs_data_array_enum()` | 使用 C 回调遍历数组 |

调用 `obs_data_array_erase()` 前必须确保索引有效。实现本身不进行边界保护。

## 9. 用户值、默认值和 Autoselect

OBS 的一个字段可以同时拥有三类值：

| 值类型 | 含义 |
| --- | --- |
| user value | 用户或业务显式设置的值 |
| default value | 没有 user value 时使用的默认值 |
| autoselect value | 应用根据运行能力选择的实际值，已废弃 |

### 9.1 设置默认值

```cpp
obs_data_set_default_int(data, "cooldown_seconds", 3);
obs_data_set_default_bool(data, "tracking_enabled", false);
```

普通 getter 会按优先级取得有效值：

```text
user value
  → autoselect value（旧机制）
  → default value
  → 类型零值
```

### 9.2 判断值来源

```cpp
obs_data_has_user_value(data, "cooldown_seconds");
obs_data_has_default_value(data, "cooldown_seconds");
```

### 9.3 删除 user value

```cpp
obs_data_unset_user_value(data, "cooldown_seconds");
```

删除 user value 后，普通 getter 会回退到 default value。

### 9.4 导出默认值对象

```cpp
OBSDataAutoRelease defaults = obs_data_get_defaults(data);
```

返回一个新 `obs_data_t`，其中默认值被转换成普通 user value。

### 9.5 Autoselect

所有 `obs_data_*_autoselect_*()` 接口已标记为 deprecated。XBotGo 新配置系统不应使用。

## 10. 合并、删除和清空

### 10.1 合并对象

```cpp
obs_data_apply(target, source);
```

行为：

- 将 `source` 的 user value 应用到 `target`。
- 同名字段会被覆盖。
- 对象和数组会递归复制，不是简单共享同一个子对象指针。
- 不复制 default value 和 autoselect value。

### 10.2 删除整个字段

```cpp
obs_data_erase(data, "obsolete_field");
```

字段及其 user/default/autoselect 数据都会从对象中移除。

### 10.3 清空 user value

```cpp
obs_data_clear(data);
```

`obs_data_clear()` 清除所有字段的 user value，但字段的 default value 和 autoselect value可以继续保留。

它和逐字段 `obs_data_erase()` 不同。

## 11. JSON 序列化

### 11.1 紧凑 JSON

```cpp
const char *json = obs_data_get_json(data);
```

输出示例：

```json
{"name":"配置 A","enabled":true}
```

### 11.2 格式化 JSON

```cpp
const char *json = obs_data_get_json_pretty(data);
```

输出使用 4 个空格缩进，更适合调试和人工检查。

### 11.3 是否包含默认值

| 函数 | 格式 | 包含默认值 |
| --- | --- | --- |
| `obs_data_get_json()` | 紧凑 | 否 |
| `obs_data_get_json_pretty()` | 格式化 | 否 |
| `obs_data_get_json_with_defaults()` | 紧凑 | 是 |
| `obs_data_get_json_pretty_with_defaults()` | 格式化 | 是 |

配置文件通常只保存显式设置值，不建议无条件把全部默认值写入文件，否则默认策略升级会受到旧文件影响。

### 11.4 JSON 字符串生命周期

`obs_data_get_json*()` 返回 `obs_data_t` 内部缓存：

```cpp
const char *first = obs_data_get_json(data);
const char *second = obs_data_get_json_pretty(data);
```

第二次序列化会释放并替换第一次的缓存，因此 `first` 随后失效。

需要保留时立即复制：

```cpp
const std::string json = obs_data_get_json(data);
```

`obs_data_get_last_json()` 只返回最近一次生成的缓存，不会重新序列化；如果从未调用过 JSON 生成函数，可能返回 `nullptr`。

## 12. JSON 文件保存

### 12.1 普通保存

```cpp
const bool success =
    obs_data_save_json(data, "/path/config.json");
```

此函数直接覆盖目标文件。写入过程中发生异常时，原文件可能已经被截断，因此业务配置不推荐使用。

### 12.2 安全保存

```cpp
const bool success = obs_data_save_json_safe(
    data,
    "/path/config.json",
    "tmp",
    "bak");
```

保存流程：

1. 将 JSON 写入 `config.json.tmp`。
2. 调用 `fflush()` 和 `fclose()`。
3. 若正式文件存在，将正式文件重命名为 `config.json.bak`。
4. 将 `config.json.tmp` 重命名为 `config.json`。

### 12.3 格式化安全保存

```cpp
const bool success = obs_data_save_json_pretty_safe(
    data,
    "/path/config.json",
    "tmp",
    "bak");
```

与 `obs_data_save_json_safe()` 的文件替换流程相同，区别只是输出格式化 JSON。

### 12.4 重要限制

- 保存函数不会创建父目录，调用前需要使用 `os_mkdirs()` 或其他目录接口。
- `temp_ext` 不能为空，否则保存失败。
- `backup_ext` 可以是 `nullptr` 或空字符串，表示不保留备份。
- 保存是同步操作，没有内部 IO 线程。
- 保存同时包含 JSON 序列化、文件写入和重命名。
- 实现调用了 `fflush()`，但没有显式调用 `fsync()`，因此不能视为数据库级持久化保证。
- 不允许多个线程同时写同一目标文件。

### 12.5 `safe` 与 `pretty_safe` 的区别

两个函数使用相同的文件写入和替换流程：

```cpp
obs_data_save_json_safe();
obs_data_save_json_pretty_safe();
```

唯一区别是 JSON 序列化格式：

| 函数 | JSON 格式 |
| --- | --- |
| `obs_data_save_json_safe()` | `JSON_COMPACT`，紧凑单行 |
| `obs_data_save_json_pretty_safe()` | `JSON_INDENT(4)`，4 空格格式化 |

两者在安全性、备份时机和失败条件上没有区别。

### 12.6 返回 `false` 的场景

保存调用链：

```text
obs_data_save_json_safe()
  → obs_data_get_json()
  → os_quick_write_utf8_file_safe()
  → os_quick_write_utf8_file(temp)
  → os_safe_replace(target, temp, backup)
```

可能返回 `false` 的场景：

| 阶段 | 失败场景 | 结果 |
| --- | --- | --- |
| JSON 序列化 | `data == nullptr` | 没有文件操作，返回 `false` |
| JSON 序列化 | 内存分配失败导致 `json_dumps()` 返回空 | 返回 `false`，通常属于进程内存严重不足 |
| 参数检查 | `temp_ext == nullptr` 或空字符串 | 底层直接返回 `false` |
| 写临时文件 | 父目录不存在 | `fopen()` 失败，返回 `false` |
| 写临时文件 | 没有目录写权限 | `fopen()` 失败，返回 `false` |
| 写临时文件 | 目标临时路径是目录 | `fopen()` 失败，返回 `false` |
| 写临时文件 | 路径非法或超过系统限制 | `fopen()` 失败，返回 `false` |
| 写临时文件 | 磁盘空间不足或 IO 错误导致 `fwrite()` 不完整 | 返回 `false` |
| 备份替换 | 正式文件存在，但不能重命名为 `.bak` | 返回 `false`，`.tmp` 通常保留 |
| 正式替换 | 不能把 `.tmp` 重命名为正式文件 | 返回 `false` |

实现没有检查：

```cpp
fflush();
fclose();
```

的返回值。因此某些延迟写入错误可能不会反映为 `false`。此外没有调用 `fsync()`，断电持久化语义弱于数据库事务。

不要用以下方式模拟失败：

```cpp
obs_data_save_json_safe(data, nullptr, "tmp", "bak");
```

`file == nullptr` 没有在该层显式校验，行为依赖更底层实现，不属于安全测试方式。

### 12.7 `.bak` 什么时候产生

第一次保存且正式文件不存在：

```text
写 config.json.tmp
  → config.json 不存在，无旧文件可备份
  → config.json.tmp 改名为 config.json
```

结果：

```text
config.json      存在
config.json.bak  不存在
```

第二次保存且正式文件存在：

```text
写新的 config.json.tmp
  → 旧 config.json 改名为 config.json.bak
  → 新 config.json.tmp 改名为 config.json
```

结果：

```text
config.json      本次新数据
config.json.bak  上一次成功保存的数据
```

后续每次成功保存都会用上一次正式文件替换 `.bak`，因此只保留一代备份。

如果 `backup_ext` 为 `nullptr` 或空字符串，则不创建备份，只执行临时文件到正式文件的替换。

### 12.8 macOS/POSIX 替换失败后的状态

macOS 使用 POSIX `rename()`：

```c
if (backup && os_file_exists(target) && rename(target, backup) != 0)
    return -1;
return rename(from, target);
```

可能出现两种失败状态。

旧文件移到备份前失败：

```text
config.json      仍是旧数据
config.json.tmp  新数据仍在
config.json.bak  保持原状态
```

旧文件已经移到备份，但临时文件改名失败：

```text
config.json      可能不存在
config.json.tmp  新数据仍在
config.json.bak  保存旧数据
```

第二种情况下仍可通过 `.bak` 恢复旧版本，但正式文件暂时缺失。

### 12.9 安全加载如何使用 `.bak`

```cpp
obs_data_create_from_json_file_safe(
    "config.json",
    "bak");
```

流程：

1. 加载 `config.json`。
2. 正式文件不存在、不可读或 JSON 非法时，检查 `config.json.bak`。
3. 如果备份存在，将 `.bak` 重命名为正式文件。
4. 再加载恢复后的正式文件。

注意：

- 备份会被移动为正式文件，不再保留原 `.bak`。
- 实现没有检查该次 `os_rename()` 的返回值，而是直接再次读取正式文件。
- 如果 `.bak` 也损坏，最终返回 `nullptr`。
- 如果正式文件合法，即使内容业务上过期，也不会自动读取 `.bak`。

### 12.10 保存与备份结论

`obs_data_save_json_safe()` 和 `obs_data_save_json_pretty_safe()` 都是同步保存接口，安全写入流程完全相同。两者只在 JSON 是否格式化上存在区别。

推荐写法：

```cpp
const bool saved = obs_data_save_json_safe(
    data,
    filePath,
    "tmp",
    "bak");
```

工程使用时应遵循以下规则：

1. 保存前由业务代码创建父目录。
2. 正式配置始终提供非空 `temp_ext`，推荐使用 `"tmp"`。
3. 重要配置提供 `backup_ext`，推荐使用 `"bak"`。
4. 第一次保存没有旧正式文件，因此不会生成 `.bak`。
5. 第二次及以后保存时，`.bak` 保存上一代成功写入的正式文件。
6. 每个配置文件只保留一代 `.bak`，它不是历史版本系统。
7. 加载时配套使用 `obs_data_create_from_json_file_safe()`，正式文件损坏后才会尝试 `.bak`。
8. 返回 `false` 时不能假设磁盘状态完全没有变化，需要检查正式文件、`.tmp` 和 `.bak`。
9. 保存接口不会提供异步线程、重试、任务合并或错误恢复策略，这些由上层存储服务负责。
10. 同一目标文件必须串行保存，不能由多个线程或多个任务同时调用。

对于 XBotGo 配置系统，建议采用：

```text
业务结构体快照
  → 串行存储线程
  → 创建独立 obs_data_t
  → obs_data_save_json_safe(..., "tmp", "bak")
  → 根据返回值更新保存状态
```

开发阶段需要人工查看 JSON 时可以使用 `obs_data_save_json_pretty_safe()`；正式版本更关注文件体积时可以使用紧凑版本。两者不能解决多线程竞争，也不能替代存储队列和退出前 flush。

### 12.11 INI 与 JSON 的写入粒度

结论：

```text
setter：只修改内存中的局部字段
save：重新生成并写入整个目标文件
```

OBS 的 INI 和 JSON 都不是磁盘局部更新。修改一个字段时不会立即查找并覆盖磁盘中的某一行或某个 JSON 节点；只有调用保存函数时才进行文件 IO，而且会重写完整文件。

以下行号基于当前仓库版本。后续合并上游 OBS 后行号可能变化，应优先通过文件名和函数名定位。

#### 12.11.1 INI 的内存局部修改

`config_set_string()`、`config_set_int()`、`config_set_bool()` 等函数只调用 `config_set_item()` 修改 `config_t` 的内存结构，没有文件 IO。

关键代码：

```text
文件：libobs/util/config-file.c
函数：config_set_string()
当前行：552

函数：config_set_int()
当前行：559

函数：config_set_bool()
当前行：575
```

示例：

```c
void config_set_int(
    config_t *config,
    const char *section,
    const char *name,
    int64_t value)
{
    struct dstr str;
    dstr_init(&str);
    dstr_printf(&str, "%" PRId64, value);
    config_set_item(
        config,
        &config->sections,
        section,
        name,
        str.array);
}
```

这里仅更新对应 section 下的 item。

#### 12.11.2 INI 保存时整文件重写

关键代码：

```text
文件：libobs/util/config-file.c
函数：config_save()
当前行：331
```

证据一：使用 `"wb"` 打开文件，会截断原文件：

```c
f = os_fopen(config->file, "wb");
```

当前行：347。

证据二：遍历全部 section 和 item，重新构造完整 INI：

```c
HASH_ITER (hh, config->sections, section, stmp) {
    HASH_ITER (hh, section->items, item, itmp) {
        // 将全部字段追加到 str
    }
}
```

当前行：357 至 376。

证据三：写入完整字符串：

```c
fwrite(str.array, str.len, 1, f);
```

当前行：382。

因此 INI 流程是：

```text
config_set_*()
  → 修改内存中的单个字段
  → config_save_safe()
  → 生成完整 INI
  → 写完整临时文件
  → 替换正式文件
```

#### 12.11.3 JSON 的内存局部修改

`obs_data_set_string()`、`obs_data_set_int()`、`obs_data_set_obj()` 等函数只修改 `obs_data_t` 内存对象。

它们不会自动触发：

```text
JSON 序列化
文件打开
文件写入
正式文件替换
```

只有显式调用 `obs_data_save_json*()` 才会落盘。

#### 12.11.4 JSON 保存时整对象序列化

关键代码：

```text
文件：libobs/obs-data.c
函数：obs_data_get_json_internal()
当前行：699
```

完整对象先转换为 Jansson JSON Tree，再生成完整字符串：

```c
json_t *root = obs_data_to_json(data, with_defaults);
data->json = json_dumps(root, flags);
```

当前行：715 至 716。

紧凑安全保存：

```text
文件：libobs/obs-data.c
函数：obs_data_save_json_safe()
当前行：758
```

格式化安全保存：

```text
文件：libobs/obs-data.c
函数：obs_data_save_json_pretty_safe()
当前行：769
```

两个函数都会将整个 JSON 字符串和完整字节长度传给底层：

```c
os_quick_write_utf8_file_safe(
    file,
    json,
    strlen(json),
    false,
    temp_ext,
    backup_ext);
```

`strlen(json)` 是整个 UTF-8 JSON 字符串的字节长度，不包含结尾的 `\0`。

#### 12.11.5 JSON 底层整文件写入

关键代码：

```text
文件：libobs/util/platform.c
函数：os_quick_write_utf8_file()
当前行：261
```

底层同样使用 `"wb"` 打开文件：

```c
FILE *f = os_fopen(path, "wb");
```

当前行：263。

写入完整字节块：

```c
fwrite(str, len, 1, f);
```

当前行：275。

安全替换函数：

```text
文件：libobs/util/platform.c
函数：os_quick_write_utf8_file_safe()
当前行：286
```

最终流程：

```text
完整 obs_data_t
  → 完整 JSON 字符串
  → 完整写入 <file>.tmp
  → 旧正式文件移动到 <file>.bak
  → <file>.tmp 改名为正式文件
```

### 12.12 OBS 场景保存的线程行为

结论：

```text
OBS 的底层保存接口不会自动切换线程。
场景保存通过 Qt 队列延迟执行，但最终仍在 Qt 主线程同步完成文件 IO。
```

#### 12.12.1 `OBSBasic` 属于 Qt 主线程

关键代码：

```text
文件：frontend/OBSApp.cpp
函数：OBSApp 创建主窗口的初始化流程
当前行：1312
```

```cpp
auto obs_basic = new OBSBasic();
mainWindow = obs_basic;
```

`OBSBasic` 在应用主线程创建，代码中没有将其 `moveToThread()` 到其他线程。因此它的 QObject 线程亲和性属于 Qt 主线程。

#### 12.12.2 `obs_frontend_save()` 没有创建线程

关键代码：

```text
文件：frontend/OBSStudioAPI.cpp
函数：OBSStudioAPI::obs_frontend_save()
当前行：461
```

```cpp
void OBSStudioAPI::obs_frontend_save()
{
    main->SaveProject();
}
```

这里只有普通函数调用，没有创建 `QThread`、线程池或后台任务。

#### 12.12.3 `SaveProject()` 是排队到主线程，不是排队到子线程

关键代码：

```text
文件：frontend/widgets/OBSBasic_SceneCollections.cpp
函数：OBSBasic::SaveProject()
当前行：1532
```

```cpp
projectChanged = true;
QMetaObject::invokeMethod(
    this,
    &OBSBasic::SaveProjectDeferred,
    Qt::QueuedConnection);
```

当前关键调用位于第 1539 行。

`Qt::QueuedConnection` 的目标线程由接收对象 `this` 的线程亲和性决定。这里 `this` 是 `OBSBasic`，属于 Qt 主线程，因此实际行为是：

```text
将 SaveProjectDeferred()
排入 Qt 主线程的事件队列
```

它不是：

```text
创建一个子线程执行 SaveProjectDeferred()
```

排队的作用主要是：

- 推迟到下一轮 Qt 事件循环。
- 合并短时间内的重复保存请求。
- 避免在当前 UI 操作调用栈中立即保存。

#### 12.12.4 `SaveProjectNow()` 甚至不排队

关键代码：

```text
文件：frontend/widgets/OBSBasic_SceneCollections.cpp
函数：OBSBasic::SaveProjectNow()
当前行：1522
```

```cpp
projectChanged = true;
SaveProjectDeferred();
```

当前直接调用位于第 1529 行。

如果 `SaveProjectNow()` 在主线程调用，序列化和文件写入会立即在主线程执行。

#### 12.12.5 真正的文件写入仍在当前线程

`SaveProjectDeferred()`：

```text
文件：frontend/widgets/OBSBasic_SceneCollections.cpp
函数：OBSBasic::SaveProjectDeferred()
当前行：1542
```

最终调用：

```cpp
Save(currentCollection);
```

当前行：1557。

在 `Save()` 内部：

```text
文件：frontend/widgets/OBSBasic_SceneCollections.cpp
函数：OBSBasic 场景集合保存实现
当前关键行：980 至 989
```

```cpp
int sceneCollectionVersion = collection.getVersion();
obs_data_set_int(
    saveData,
    "version",
    sceneCollectionVersion);

const std::string collectionFileName =
    collection.getFilePathString();

bool success = obs_data_save_json_pretty_safe(
    saveData,
    collectionFileName.c_str(),
    "tmp",
    "bak");
```

真正的保存调用位于第 985 行。调用后会一直等到以下操作完成才返回：

```text
完整 JSON 序列化
写临时文件
fflush
fclose
rename 备份
rename 正式文件
```

该调用链没有再次使用 `QThread`、`QThreadPool`、`QtConcurrent` 或 `std::thread`。

#### 12.12.6 完整线程时序

普通场景保存：

```text
Qt 主线程调用 SaveProject()
  → projectChanged = true
  → 向 Qt 主线程事件队列投递 SaveProjectDeferred()
  → SaveProject() 返回
  → Qt 主线程处理下一轮事件
  → SaveProjectDeferred()
  → Save(currentCollection)
  → obs_data_save_json_pretty_safe()
  → Qt 主线程同步完成完整文件写入
```

`SaveProject()` 连续调用多次时，可能产生多个队列事件，但第一次保存后会设置：

```cpp
projectChanged = false;
```

后续事件进入 `SaveProjectDeferred()` 后会因为：

```cpp
if (!projectChanged) {
    return;
}
```

直接返回，从而合并重复保存。

#### 12.12.7 对 XBotGo 配置系统的结论

不能依赖 OBS 保存接口自动切换到后台线程，因为：

- `config_save_safe()` 是同步调用。
- `obs_data_save_json_safe()` 是同步调用。
- `obs_data_save_json_pretty_safe()` 是同步调用。
- Qt `QueuedConnection` 只是选择接收对象所属线程，并不创建新线程。
- OBS 场景保存接收对象是主线程中的 `OBSBasic`。

XBotGo 的普通配置和每秒比赛进度应采用：

```text
Qt 主线程更新业务结构体
  → 生成不可变快照
  → 提交到 XBotGo 串行存储线程
  → 存储线程创建独立 obs_data_t
  → 存储线程同步调用 obs_data_save_json_safe()
```

这样同步保存仍然存在，但阻塞的是专用存储线程，而不是 Qt UI 线程。

## 13. Item API

### 13.1 遍历字段

```cpp
obs_data_item_t *item = obs_data_first(data);
while (item) {
    const char *name = obs_data_item_get_name(item);
    const obs_data_type type = obs_data_item_gettype(item);

    // 处理当前字段

    if (!obs_data_item_next(&item)) {
        break;
    }
}
```

`obs_data_item_next()` 会释放当前 item 并取得下一个 item。循环完整结束时不需要再释放；如果中途 `break`，必须调用：

```cpp
obs_data_item_release(&item);
```

### 13.2 按名称取得 item

```cpp
obs_data_item_t *item =
    obs_data_item_byname(data, "score");

if (item) {
    obs_data_item_set_int(&item, 20);
    obs_data_item_release(&item);
}
```

### 13.3 类型检查

```cpp
const obs_data_type type = obs_data_item_gettype(item);
const obs_data_number_type numberType =
    obs_data_item_numtype(item);
```

`obs_data_item_numtype()` 用于区分内部记录的整数和浮点数。

### 13.4 Item 修改和删除

```cpp
obs_data_item_set_string(&item, "new value");
obs_data_item_unset_user_value(item);
obs_data_item_remove(&item);
```

调用 `obs_data_item_remove()` 后 item 会被释放并置空。

## 14. Vec、Quat 和帧率辅助接口

`obs-data.h` 还提供 OBS 图形和视频参数辅助函数：

```cpp
obs_data_set_vec2();
obs_data_set_vec3();
obs_data_set_vec4();
obs_data_set_quat();

obs_data_get_vec2();
obs_data_get_vec3();
obs_data_get_vec4();
obs_data_get_quat();
```

这些值会被编码为嵌套对象，适用于 OBS 内部变换和图形数据。

帧率辅助接口：

```cpp
obs_data_set_frames_per_second();
obs_data_get_frames_per_second();
```

它们主要服务 `media_frames_per_second` 和 OBS 帧率属性。XBotGo 普通业务配置没有必要使用，除非需要保存 OBS 原生帧率选项。

## 15. 线程安全

### 15.1 引用计数是原子的

以下操作使用原子引用计数：

```cpp
obs_data_addref();
obs_data_release();
obs_data_array_addref();
obs_data_array_release();
```

这只能保证对象不会因为并发引用释放而提前销毁。

### 15.2 对象内容不是线程安全的

`obs_data_t` 内部没有字段互斥锁。以下并发操作不安全：

```text
线程 A：obs_data_set_string()
线程 B：obs_data_get_json()
```

`obs_data_get_json()` 自身也会释放并重建 `data->json` 缓存，所以多个线程对同一个对象同时序列化也不安全。

### 15.3 推荐线程模型

```text
业务线程
  → 生成不可变业务结构体快照
  → 提交到串行存储线程

存储线程
  → 创建自己的 obs_data_t
  → 填充字段
  → obs_data_save_json_safe()
  → 释放 obs_data_t
```

不要让 UI 线程和存储线程共享并修改同一个 `obs_data_t`。

## 16. Qt 字符串转换

OBS Data 使用 UTF-8 `const char *`，Qt 页面通常使用 `QString`。

写入：

```cpp
const QByteArray utf8 = value.toUtf8();
obs_data_set_string(data, "name", utf8.constData());
```

读取：

```cpp
const QString value = QString::fromUtf8(
    obs_data_get_string(data, "name"));
```

不要使用本地 8 位编码，也不要把临时 `QByteArray::constData()` 保存到成员变量中。

## 17. 函数速查表

### 17.1 对象创建和生命周期

| 函数 | 用途 | 注意事项 |
| --- | --- | --- |
| `obs_data_create()` | 创建空对象 | 返回新引用 |
| `obs_data_create_from_json()` | 解析 JSON 字符串 | 失败返回 `nullptr` |
| `obs_data_create_from_json_file()` | 加载 JSON 文件 | 同步 IO |
| `obs_data_create_from_json_file_safe()` | 正式文件失败时尝试备份 | 备份会被移动回正式文件 |
| `obs_data_addref()` | 增加引用 | 不让字段读写变得线程安全 |
| `obs_data_release()` | 释放引用 | 降到 0 时销毁 |
| `obs_data_newref()` | 有对象则 addref，无对象则创建 | OBS 内联辅助函数 |

### 17.2 字段读写

| 函数族 | 用途 |
| --- | --- |
| `obs_data_set_string/int/double/bool()` | 设置基础 user value |
| `obs_data_get_string/int/double/bool()` | 读取当前有效值 |
| `obs_data_set_obj()` / `get_obj()` | 设置或读取子对象 |
| `obs_data_set_array()` / `get_array()` | 设置或读取数组 |
| `obs_data_set_default_*()` | 设置默认值 |
| `obs_data_get_default_*()` | 只读取默认值 |
| `obs_data_set/get_autoselect_*()` | 旧的自动选择值，已废弃 |

### 17.3 值状态和删除

| 函数 | 用途 |
| --- | --- |
| `obs_data_has_user_value()` | 判断字段是否有 user value |
| `obs_data_has_default_value()` | 判断字段是否有 default value |
| `obs_data_unset_user_value()` | 删除 user value，保留字段和默认值 |
| `obs_data_unset_default_value()` | 删除 default value |
| `obs_data_erase()` | 删除整个字段 |
| `obs_data_clear()` | 清空全部 user value |
| `obs_data_apply()` | 将一个对象的 user value 深复制到另一个对象 |

### 17.4 数组

| 函数 | 用途 |
| --- | --- |
| `obs_data_array_create()` | 创建数组 |
| `obs_data_array_count()` | 返回对象数量 |
| `obs_data_array_item()` | 获取指定对象，返回新引用 |
| `obs_data_array_push_back()` | 末尾追加对象 |
| `obs_data_array_insert()` | 插入对象 |
| `obs_data_array_push_back_array()` | 追加另一个数组 |
| `obs_data_array_erase()` | 删除指定索引 |
| `obs_data_array_enum()` | C 回调遍历 |

### 17.5 JSON 和文件

| 函数 | 用途 | 推荐程度 |
| --- | --- | --- |
| `obs_data_get_json()` | 生成紧凑 JSON | 调试或传输 |
| `obs_data_get_json_pretty()` | 生成格式化 JSON | 调试预览 |
| `obs_data_get_json_with_defaults()` | 包含默认值的紧凑 JSON | 谨慎使用 |
| `obs_data_get_json_pretty_with_defaults()` | 包含默认值的格式化 JSON | 谨慎使用 |
| `obs_data_get_last_json()` | 取得最近一次 JSON 缓存 | 不触发序列化 |
| `obs_data_save_json()` | 直接覆盖文件 | 不推荐重要配置 |
| `obs_data_save_json_safe()` | 临时文件加安全替换 | 推荐 |
| `obs_data_save_json_pretty_safe()` | 格式化加安全替换 | 推荐开发期使用 |

## 18. 常见错误

### 18.1 忘记释放 getter 返回的对象

错误：

```cpp
obs_data_t *progress = obs_data_get_obj(root, "progress");
// 没有 release
```

正确：

```cpp
OBSDataAutoRelease progress =
    obs_data_get_obj(root, "progress");
```

### 18.2 释放借用字符串

错误：

```cpp
const char *name = obs_data_get_string(data, "name");
bfree((void *)name);
```

getter 返回的是内部借用指针，不能释放。

### 18.3 保存前没有创建目录

`obs_data_save_json_safe()` 只保存文件，不创建父目录。

```cpp
os_mkdirs(directory);
```

### 18.4 多线程共享可变 `obs_data_t`

引用计数安全不等于字段读写安全。应传递业务结构体快照，由存储线程创建独立 `obs_data_t`。

### 18.5 直接修改 OBS 场景 JSON

不应通过普通文件 API 修改 OBS 的场景集合 JSON。需要更新 Source 时应使用：

```cpp
OBSDataAutoRelease settings = obs_source_get_settings(source);
obs_data_set_string(settings, "key", "value");
obs_source_update(source, settings);
```

独立 XBotGo 配置文件则可以使用自己的 `obs_data_t` 和 `obs_data_save_json_safe()`。

## 19. Demo 功能

Demo 页面包含：

- 配置名称字符串。
- 设备 ID 字符串。
- 追踪开关布尔值。
- 主客队比分整数。
- 嵌套 `progress` 对象。
- `devices` 对象数组。
- 格式化 JSON 预览。
- 安全保存和安全加载。
- 打开配置目录。

Demo 文件保存位置：

```text
~/Library/Application Support/XbotGo-Studio/
└── plugin_config/
    └── xbotgo-demo/
        ├── obs-data-demo.json
        ├── obs-data-demo.json.bak
        └── obs-data-demo.json.tmp
```

正常保存完成后 `.tmp` 不会保留。第二次及以后保存时会产生或更新 `.bak`。

## 20. Demo 构建

Demo 需要先构建主工程的 libobs：

```bash
cmake --build build_macos --config Debug --target libobs --parallel 8
```

配置 Demo：

```bash
cmake \
  -S plugins/xbotgo/frontend/demo/obs-data \
  -B plugins/xbotgo/frontend/demo/obs-data/build \
  -G Xcode \
  -DCMAKE_PREFIX_PATH="${PWD}/.deps/obs-deps-2026-07-15-universal;${PWD}/.deps/obs-deps-qt6-2026-07-15-universal" \
  -Dlibobs_DIR="${PWD}/build_macos/libobs"
```

构建：

```bash
cmake \
  --build plugins/xbotgo/frontend/demo/obs-data/build \
  --config Debug \
  --parallel 8
```

运行：

```bash
plugins/xbotgo/frontend/demo/obs-data/build/Debug/xbotgo-obs-data-demo
```

如果本地 OBS 依赖包日期不同，需要把 `CMAKE_PREFIX_PATH` 改成当前 `.deps` 下的实际目录。

## 21. Demo 流程

保存按钮执行：

```text
读取 Qt 控件
  → obs_data_create()
  → obs_data_set_*()
  → obs_data_set_obj()
  → obs_data_set_array()
  → os_mkdirs()
  → obs_data_save_json_pretty_safe()
  → JSON 预览
```

加载按钮执行：

```text
obs_data_create_from_json_file_safe()
  → obs_data_get_string/int/bool()
  → obs_data_get_obj()
  → obs_data_get_array()
  → 恢复 Qt 控件
  → JSON 预览
```

这个 Demo 故意使用同步 IO，便于观察每个接口的直接行为。正式 XBotGo 配置架构应把文件加载、JSON 解析、序列化和保存放到串行存储线程中。

## 22. 对 XBotGo 配置系统的初步结论

- 业务层使用按功能拆分的 Qt/C++ 结构体。
- `obs_data_t` 只在 Codec 和文件 Repository 边界使用。
- 每次保存根据完整业务快照创建新的 `obs_data_t`。
- 不把一个可变 `obs_data_t` 暴露给所有业务模块。
- 使用 `obs_data_save_json_safe()` 或 `obs_data_save_json_pretty_safe()`。
- 使用 `obs_data_create_from_json_file_safe()` 加载并提供备份恢复。
- 文件 IO 放在单一串行存储线程。
- Source settings 和 XBotGo 独立配置文件是两套不同的数据，后续架构文档再定义两者的映射关系。
