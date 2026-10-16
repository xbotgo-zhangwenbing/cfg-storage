#pragma once

#include "ImageParametersTypes.h"

#include <QObject>
#include <QMetaType>

namespace xbotgo {
// 初始化前和关闭后仍允许读取内存，但所有 setter 返回 false。
class IImageParametersConfig : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;
	~IImageParametersConfig() override = default;

public:
	// 返回完整数据副本；整体提交先校验全部字段，失败时不改变原有数据。
	virtual ImageParametersData getAll() const = 0;
	virtual bool setAll(const ImageParametersData &data) = 0;

	// 单字段修改在模块锁内完成，避免用旧的整体快照覆盖其他字段的新值。
	virtual ImageSpecification getSpecification() const = 0;
	virtual bool setSpecification(ImageSpecification specification) = 0;

	virtual ImageEncoding getEncoding() const = 0;
	virtual bool setEncoding(ImageEncoding encoding) = 0;

	virtual bool getWhiteBalanceLocked() const = 0;
	virtual bool setWhiteBalanceLocked(bool locked) = 0;

	virtual AntiFlicker getAntiFlicker() const = 0;
	virtual bool setAntiFlicker(AntiFlicker antiFlicker) = 0;
signals:
	// 新建、切换、重载等整份配置替换成功后通知完整快照，普通 setter 不发信号。
	// 在对象所属线程通过事件队列通知，不在配置锁或模块锁内执行槽函数。
	void changed(xbotgo::ImageParametersData data);
};
} // namespace xbotgo

Q_DECLARE_METATYPE(xbotgo::ImageParametersData)
