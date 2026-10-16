#pragma once

#include <xbotgo/storage/IImageParametersConfig.h>

#include <shared_mutex>
#include <string>

namespace xbotgo {
class StorageWorker;

// 业务内存的唯一持有者，修改后直接向 Worker 提交完整模块快照。
class ImageParametersConfigImpl final : public IImageParametersConfig {
public:
	ImageParametersConfigImpl(StorageWorker &worker, std::shared_mutex &configurationMutex);

public:
	ImageParametersData getAll() const override;
	bool setAll(const ImageParametersData &data) override;
	ImageSpecification getSpecification() const override;
	bool setSpecification(ImageSpecification specification) override;
	ImageEncoding getEncoding() const override;
	bool setEncoding(ImageEncoding encoding) override;
	bool getWhiteBalanceLocked() const override;
	bool setWhiteBalanceLocked(bool locked) override;
	AntiFlicker getAntiFlicker() const override;
	bool setAntiFlicker(AntiFlicker antiFlicker) override;

	// 仅供 Service 控制生命周期：关闭后保留最后一次数据，拒绝继续修改。
	void setAcceptingWrites(bool accepting);
	// Service 持配置独占门锁，激活成功后一次替换 ID 和数据，不重新提交保存。
	void replaceFromStorage(std::string configId, const ImageParametersData &data);

private:
	// 调用者持配置门锁和模块写锁；入队失败时不提交业务内存修改。
	bool submitSnapshot(const ImageParametersData &snapshot);
	void postChanged(const ImageParametersData &snapshot);

private:
	StorageWorker &worker_;
	// 所有路径统一按配置门锁 → 模块锁加锁；Service 独占门锁后也遵守这个顺序。
	std::shared_mutex &configurationMutex_;
	std::string configId_;
	mutable std::shared_mutex mutex_;
	ImageParametersData data_;
	bool acceptingWrites_ = false;
};
} // namespace xbotgo
