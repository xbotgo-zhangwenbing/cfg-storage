#include "ImageParametersConfigImpl.h"
#include "ImageParametersCodec.h"
#include "io/StorageWorker.h"

#include <mutex>

namespace xbotgo {
ImageParametersConfigImpl::ImageParametersConfigImpl(StorageWorker &worker, std::shared_mutex &configurationMutex)
	: worker_(worker),
	  configurationMutex_(configurationMutex)
{
	qRegisterMetaType<ImageParametersData>("xbotgo::ImageParametersData");
}

ImageParametersData ImageParametersConfigImpl::getAll() const
{
	std::shared_lock configurationLock(configurationMutex_);
	std::shared_lock lock(mutex_);
	return data_;
}

bool ImageParametersConfigImpl::setAll(const ImageParametersData &data)
{
	std::shared_lock configurationLock(configurationMutex_);
	// 防止非法枚举通过强制转换进入内存；校验失败不提交任何字段。
	if (!isValidEnum(data.specification) || !isValidEnum(data.encoding) || !isValidEnum(data.antiFlicker)) {
		return false;
	}
	std::unique_lock lock(mutex_);
	if (!acceptingWrites_) {
		return false;
	}
	return submitSnapshot(data);
}

ImageSpecification ImageParametersConfigImpl::getSpecification() const
{
	std::shared_lock configurationLock(configurationMutex_);
	std::shared_lock lock(mutex_);
	return data_.specification;
}

bool ImageParametersConfigImpl::setSpecification(ImageSpecification specification)
{
	std::shared_lock configurationLock(configurationMutex_);
	if (!isValidEnum(specification)) {
		return false;
	}
	std::unique_lock lock(mutex_);
	if (!acceptingWrites_) {
		return false;
	}
	auto snapshot = data_;
	snapshot.specification = specification;
	return submitSnapshot(snapshot);
}

ImageEncoding ImageParametersConfigImpl::getEncoding() const
{
	std::shared_lock configurationLock(configurationMutex_);
	std::shared_lock lock(mutex_);
	return data_.encoding;
}

bool ImageParametersConfigImpl::setEncoding(ImageEncoding encoding)
{
	std::shared_lock configurationLock(configurationMutex_);
	if (!isValidEnum(encoding)) {
		return false;
	}
	std::unique_lock lock(mutex_);
	if (!acceptingWrites_) {
		return false;
	}
	auto snapshot = data_;
	snapshot.encoding = encoding;
	return submitSnapshot(snapshot);
}

bool ImageParametersConfigImpl::getWhiteBalanceLocked() const
{
	std::shared_lock configurationLock(configurationMutex_);
	std::shared_lock lock(mutex_);
	return data_.whiteBalanceLocked;
}

bool ImageParametersConfigImpl::setWhiteBalanceLocked(bool locked)
{
	std::shared_lock configurationLock(configurationMutex_);
	std::unique_lock lock(mutex_);
	if (!acceptingWrites_) {
		return false;
	}
	auto snapshot = data_;
	snapshot.whiteBalanceLocked = locked;
	return submitSnapshot(snapshot);
}

AntiFlicker ImageParametersConfigImpl::getAntiFlicker() const
{
	std::shared_lock configurationLock(configurationMutex_);
	std::shared_lock lock(mutex_);
	return data_.antiFlicker;
}

bool ImageParametersConfigImpl::setAntiFlicker(AntiFlicker antiFlicker)
{
	std::shared_lock configurationLock(configurationMutex_);
	if (!isValidEnum(antiFlicker)) {
		return false;
	}
	std::unique_lock lock(mutex_);
	if (!acceptingWrites_) {
		return false;
	}
	auto snapshot = data_;
	snapshot.antiFlicker = antiFlicker;
	return submitSnapshot(snapshot);
}

bool ImageParametersConfigImpl::submitSnapshot(const ImageParametersData &snapshot)
{
	// 编码与入队位于同一个模块写锁内，防止并发修改把旧快照排在新快照之后。
	if (!worker_.submitModule(configId_, "image_parameters", ImageParametersCodec::encode(snapshot))) {
		return false;
	}
	data_ = snapshot;
	return true;
}

void ImageParametersConfigImpl::replaceFromStorage(std::string configId, const ImageParametersData &data)
{
	std::unique_lock lock(mutex_);
	configId_.swap(configId);
	data_ = data;
	postChanged(data);
}

void ImageParametersConfigImpl::postChanged(const ImageParametersData &snapshot)
{
	// 仅整份配置替换调用。这里只投递快照，避免持锁直接发信号导致槽函数重入死锁。
	// 回调在对象所属线程执行，切换按提交顺序通知，QObject 销毁时自动取消回调。
	QMetaObject::invokeMethod(this, [this, snapshot] { emit changed(snapshot); }, Qt::QueuedConnection);
}

void ImageParametersConfigImpl::setAcceptingWrites(bool accepting)
{
	std::unique_lock lock(mutex_);
	acceptingWrites_ = accepting;
}

} // namespace xbotgo
