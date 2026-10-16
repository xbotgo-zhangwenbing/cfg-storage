#include <xbotgo/storage/XBotGoConfigService.h>

#include "XBotGoConfigServiceImpl.h"

namespace xbotgo {
XBotGoConfigService *XBotGoConfigService::instance()
{
	static XBotGoConfigService service;
	return &service;
}

XBotGoConfigService::XBotGoConfigService() : impl_(std::make_unique<Impl>()) {}
XBotGoConfigService::~XBotGoConfigService() = default;

bool XBotGoConfigService::initialize(const std::filesystem::path &rootDirectory)
{
	return impl_->initialize(rootDirectory);
}

void XBotGoConfigService::shutdown()
{
	impl_->shutdown();
}

std::filesystem::path XBotGoConfigService::configurationRootDirectory() const
{
	return impl_->configurationRootDirectory();
}

IImageParametersConfig *XBotGoConfigService::imageParametersConfig()
{
	return impl_->imageParametersConfig();
}

std::vector<ConfigurationSummary> XBotGoConfigService::configurations() const
{
	return impl_->configurations();
}

std::string XBotGoConfigService::currentConfigurationId() const
{
	return impl_->currentConfigurationId();
}

bool XBotGoConfigService::createConfiguration(const std::string &name)
{
	return impl_->createConfiguration(name);
}

bool XBotGoConfigService::renameConfiguration(const std::string &configId, const std::string &name)
{
	return impl_->renameConfiguration(configId, name);
}

bool XBotGoConfigService::deleteConfiguration(const std::string &configId)
{
	return impl_->deleteConfiguration(configId);
}

bool XBotGoConfigService::switchConfiguration(const std::string &configId)
{
	return impl_->switchConfiguration(configId);
}

bool XBotGoConfigService::reloadCurrentConfiguration()
{
	return impl_->reloadCurrentConfiguration();
}

std::uint64_t XBotGoConfigService::getMemoryRevision() const
{
	return impl_->getMemoryRevision();
}

std::uint64_t XBotGoConfigService::getSavedRevision() const
{
	return impl_->getSavedRevision();
}

std::string XBotGoConfigService::lastError() const
{
	return impl_->lastError();
}
} // namespace xbotgo
