# XS 配置文件 JSON 设计（V0.4）

本文只定义配置文件和接口需要的数据结构，不涉及 UI，也不修改现有代码。配置文件版本使用产品版本 `V0.4`；存储迁移时使用整数版本 `4`，目录名为 `v4`。`V0.4` 与运行期保存 `revision` 不是一回事。

你给出的分类总体合理，建议固定为以下模块：

- `input_sources`：已添加设备、输入顺序、预览显隐和用户别名。
- `image_parameters`：规格、编码、白平衡、防频闪和同步总开关。
- `camera_control`：追踪、限位、云台角度和变焦。
- `auto_director`：导播模式、转场、冷却时间和左/中/右机位绑定。
- `match_info`：比赛、主队和客队资料。
- `packaging`：模板、计分牌、动画和自定义图文元素。
- `match_progress`：比分、节次和两个计时器的持久化值。

输入源和相机参数使用同一个 `device_id` 关联。输入顺序和导播机位是两个概念：顺序表示输入列表中的排列位置，机位角色表示左、中、右等业务位置。自动导播只保存角色到 `device_id` 的绑定，不复制相机信息。

## 1. 文件布局

```text
xbotgo/
├── index.json
└── configurations/
    └── v4/
        └── <config-id>/
            ├── config.json
            └── assets/
                ├── <asset-id>.png
                └── <asset-id>.jpg
```

V0.4 先使用一个完整的 `config.json` 保存配置内容。图片是二进制资源，保存在配置自己的 `assets/` 目录，JSON 只保存相对路径和资源名称。当前内容规模不需要把篮球、包装等模块拆成多个 JSON 文件，之后确实需要独立加载或故障隔离时再提升配置版本。

## 2. index.json

`index.json` 是全局配置索引，不属于某一个配置。Service 在内存中持有它的结构化副本，用于查找、重命名和校验；Worker 负责读写磁盘。

```json
{
  "index_schema_version": 1,
  "configuration_version": 4,
  "current_config_id": "550e8400-e29b-41d4-a716-446655440000",
  "configurations": {
    "550e8400-e29b-41d4-a716-446655440000": {
      "name": "默认配置",
      "created_at": "2026-10-15T10:00:00+08:00",
      "last_saved_at": "2026-10-15T10:20:00+08:00"
    },
    "7f5032c4-f8d1-4f20-843d-d4f8feef04e8": {
      "name": "比赛配置",
      "created_at": "2026-10-14T19:00:00+08:00",
      "last_saved_at": "2026-10-14T20:30:00+08:00"
    }
  }
}
```

规则：

- ID 使用系统生成的 UUID，创建后不变；另存为生成新 ID。
- 名称去除首尾空白后长度为 1–20 个用户可见字符，同一索引中不能重名。
- 首次创建时名称为“未命名”；另存为的默认名称由 Service 生成，例如“当前名称_1”。
- `created_at` 只在创建时写入；`last_saved_at` 只在相关配置成功持久化后更新，保存失败不得更新。
- 最多保存 50 个配置。当前配置不允许删除。
- `configurations` 使用对象便于通过 ID 定位；Service 对外返回 `vector<ConfigurationEntry>`，不暴露 JSON 对象。
- `configuration_version: 4` 表示配置目录使用 `configurations/v4/` 和 V0.4 布局。

索引条目保留为可扩展结构，后续可以增加标签、描述或排序信息，不把配置元数据压缩成 `id -> name`。

## 3. config.json 顶层结构

```json
{
  "config_schema_version": "0.4",
  "config_id": "550e8400-e29b-41d4-a716-446655440000",
  "input_sources": {},
  "image_parameters": {},
  "camera_control": {},
  "auto_director": {},
  "match_info": {},
  "packaging": {},
  "match_progress": {}
}
```

`config_id` 用于加载后的完整性校验，必须与目录名和 index 中的 ID 相同。未知顶层模块在读写时应保留，避免旧程序覆盖新程序已经写入的字段。

## 4. 输入源和相机参数

```json
{
  "input_sources": {
    "devices": [
      {
        "device_id": "camera-main",
        "input_order": 1,
        "alias": "主机位",
        "preview_visible": true
      },
      {
        "device_id": "camera-left",
        "input_order": 2,
        "alias": "左机位",
        "preview_visible": true
      },
      {
        "device_id": "camera-right",
        "input_order": 3,
        "alias": "右机位",
        "preview_visible": false
      }
    ]
  },
  "image_parameters": {
    "synchronization_enabled": true,
    "cameras": {
      "camera-main": {
        "specification": "1080p30",
        "encoding": "H264",
        "white_balance_locked": true,
        "anti_flicker": "auto"
      },
      "camera-left": {
        "specification": "1080p30",
        "encoding": "H264",
        "white_balance_locked": true,
        "anti_flicker": "auto"
      }
    }
  }
}
```

规则：

- `input_order` 是从 1 开始的整数序号，不是枚举。它表示输入源列表中的显示和处理顺序，未来增加第四台、第五台设备时只需使用 4、5，不需要修改 JSON 结构。
- `devices` 保留离线设备记录，离线只影响运行状态，不从配置删除；移除设备时才从该列表删除。
- `device_id` 是设备唯一身份；设备当前名称以固件读取结果为准。`alias` 是用户在配置中的可选名称，若产品不需要用户别名可以删除此字段。
- 未出现在 `image_parameters.cameras` 的设备加载默认图像参数；多余的历史设备参数可以保留，不自动删除。
- 枚举值使用稳定字符串，不使用 UI 显示文字；编码使用 `H264`、`H265`，规格使用 `4k30`、`1080p30`、`1080p60`。

## 5. 相机控制和自动导播

```json
{
  "camera_control": {
    "cameras": {
      "camera-main": {
        "tracking_mode": "off",
        "horizontal_limit_degrees": 110,
        "gimbal": {
          "pan_degrees": 0,
          "tilt_degrees": 0
        },
        "auto_zoom_enabled": true,
        "manual_zoom": 1.0
      }
    }
  },
  "auto_director": {
    "mode": "type_a",
    "transition": "hard_cut",
    "cut_cooldown_seconds": 3,
    "camera_roles": [
      { "role": "left", "device_id": "camera-left" },
      { "role": "main", "device_id": "camera-main" },
      { "role": "right", "device_id": "camera-right" }
    ]
  }
}
```

约束：`tracking_mode` 为 `off` 或 `basketball`；水平限位 60–150 度；`pan_degrees`、`tilt_degrees` 和 `manual_zoom` 按设备能力校验；倍率范围 1.0–3.0。导播模式当前只有 `type_a`，转场当前只有 `hard_cut`，冷却时间为 1–5 秒。

`camera_roles` 是机位角色和摄像机 ID 的绑定表。`role` 当前支持 `left`、`main`、`right`，未来增加第四个业务机位时增加新的角色值和对应条目，不改变已有设备结构。一个角色最多绑定一台摄像机；未绑定时使用 `device_id: null`。ID 必须来自 `input_sources.devices`，设备离线时仍保留绑定。

这里不保存自动导播运行开关、当前选中机位、冷却计时和人工接管状态；加载后自动导播运行态始终为关闭。

## 6. 比赛信息

```json
{
  "match_info": {
    "visible": true,
    "match_name": "",
    "match_logo": null,
    "home_team": {
      "name": "",
      "logo": null,
      "color": "#4E9DF5"
    },
    "away_team": {
      "name": "",
      "logo": null,
      "color": "#ED856C"
    }
  }
}
```

`match_name` 长度 0–30；队名长度 0–20。`match_logo`、`home_team.logo` 和 `away_team.logo` 的值为 `null` 或资源引用：

```json
{
  "name": "主队 Logo",
  "path": "assets/2f0d8c2a.png"
}
```

`path` 必须是相对于当前配置目录的路径，Worker 只允许访问当前配置的 `assets/`。不保存绝对路径，不允许 `..` 穿透。图片导入成功后才把引用写入配置；移除图片时保存为 `null`，旧文件在新 JSON 成功保存后清理。

## 7. 包装设置

```json
{
  "packaging": {
    "template": "a",
    "scoreboard_position": "bottom_center",
    "opening_animation_enabled": true,
    "ending_animation_enabled": true,
    "replay_animation_enabled": true,
    "texts": [
      {
        "id": "text-1",
        "content": "自定义文字",
        "font_size": 0,
        "color": "#FFFFFF",
        "position": { "x": 50.0, "y": 50.0 },
        "visible": true,
        "layer": 0
      }
    ],
    "images": [
      {
        "id": "image-1",
        "asset": {
          "name": "赞助商 Logo",
          "path": "assets/7e3c1a20.png"
        },
        "position": { "x": 50.0, "y": 50.0 },
        "width_percent": 20.0,
        "visible": true,
        "layer": 1
      }
    ]
  }
}
```

规则：

- `template` 为 `a`、`b` 或 `none`。选择 `none` 只改变当前使用模板，不删除模板相关设置和自定义元素。
- `scoreboard_position` 为 `top_center`、`bottom_center`、`top_left`、`top_right`、`bottom_left`、`bottom_right`。
- 自定义文字和图片各最多 5 个，`id` 在当前配置内唯一；`layer` 为统一的叠放顺序，数值越大越靠上。
- 位置 `x`、`y` 是 0–100 的相对坐标，保存前按元素边界限制；图片宽度是画布宽度百分比，高度由素材比例计算。
- `font_size: 0` 表示使用模板默认字号，具体默认字号由包装模块决定；确定固定默认值后可在 V0.5 明确。
- 自定义图片只保存资源引用，不把二进制内容写入 JSON。

## 8. 比赛进度

```json
{
  "match_progress": {
    "home_score": 0,
    "away_score": 0,
    "period": "pregame",
    "period_duration_minutes": 10,
    "match_remaining_seconds": 600,
    "shot_clock_remaining_seconds": 24
  }
}
```

约束：比分 0–999；`period` 为 `pregame`、`1st`、`2nd`、`half`、`3rd`、`4th`、`ot`、`final`；单节时长为 5、8、10、12、15、20 分钟；比赛剩余时间 0–5999 秒；进攻时间 0–24 秒。

加载配置时恢复上述显示值，但两个计时器都必须恢复为暂停状态，不执行切节动作、不播放动画、不恢复撤销记录。软件关闭或离开配置期间不扣减时间；运行中每次成功改变秒值都可以提交保存，异常退出只能恢复最后成功保存的值。

## 9. 不保存的运行状态

以下内容不进入 V0.4 JSON：

- 设备在线状态、电量、固件版本、当前预览帧。
- 云台瞬时位置、霍尔校准结果和设备连接任务。
- 直播、录制、自动导播运行态、当前选中机位、人工接管状态。
- 云台移动、动画播放、计时器运行状态。
- 比分撤销记录以及任何 UI 临时输入状态。

配置保存的是“下次加载希望恢复的目标值”，不是设备当前实时状态。加载目标配置后，由业务层根据已恢复的目标值执行设备匹配、连接和参数下发；StorageWorker 只负责文件和 JSON。

## 10. 接口所需的数据分层

JSON 结构对应稳定的模块快照接口，不让业务接口暴露 `obs_data_t`：

```cpp
struct ConfigurationSnapshot {
    InputSourcesData inputSources;
    ImageParametersData imageParameters;
    CameraControlData cameraControl;
    AutoDirectorData autoDirector;
    MatchInfoData matchInfo;
    PackagingData packaging;
    MatchProgressData matchProgress;
};
```

Codec 负责 `Data` 与 JSON 节点的转换、默认值和字段校验。StorageWorker 只接收完整模块 JSON 或完整配置 JSON，不依赖这些业务结构体。加载时先读取并校验 JSON，再由 Service 调用 Codec 解码到临时 `ConfigurationSnapshot`；全部模块成功后才替换业务内存。

## 11. 需要在实现前确认的两点

1. 相机名称是否同时需要保存用户别名。如果设备名称永远以固件为准，可以删除 `input_sources.devices[*].alias`；如果支持用户命名，就保留 `alias`，不覆盖固件原名。
2. `font_size` 的最终单位和默认值目前需求写成“待定”。本设计用 `0` 表示模板默认值，避免现在编造一个字号。

