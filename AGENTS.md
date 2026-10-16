# 代码风格约定

- 关键函数、变量和不直观的逻辑使用中文注释，说明职责、原因或线程边界，不逐行注释。
- 类声明按职责分区：构造、析构和拷贝/移动声明放在一段 `public:`；公开业务方法另起一段 `public:`。
- 私有方法与成员变量分别使用独立的 `private:` 分隔；有内部类型时也可单独分区。只有实际内容才建立分区，不添加空的访问区段。
- 单例的构造函数保持私有，防止额外实例；`instance()` 返回指针，并以中文说明其始终非空、调用方无需判空且不持有所有权。

- `XBotGoConfigService::Impl` 的声明和实现分别放在 `src/XBotGoConfigServiceImpl.h`、`src/XBotGoConfigServiceImpl.cpp`；公共 Service 源文件只保留单例和接口转发。

- 枚举的稳定字符串映射使用 `XBOTGO_ENUM_STRINGS`，紧跟枚举定义放置，字符串与枚举项名称一致（如 `{ImageSpecification::P4K_30, "P4K_30"}`）；转换模板共用公共 `common/EnumString.h`，不为每个模块另建映射头文件。

- 开发阶段不为旧 Demo 枚举值保留兼容映射；不兼容的旧配置清理后重建。

- JSON 字段格式使用 `StorageValidation` 的 `ObsDataField` 格式表和 `validateObsDataFormat` 一次校验，不在 Codec 内堆叠逐字段类型判断；枚举、版本等业务值校验仍由对应模块负责。
