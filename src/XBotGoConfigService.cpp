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

CfgError XBotGoConfigService::initialize(const std::filesystem::path &rootDirectory)
{
	return impl_->initialize(rootDirectory);
}

CfgError XBotGoConfigService::shutdown()
{
	return impl_->shutdown();
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

CfgError XBotGoConfigService::createConfiguration(const std::string &name)
{
	return impl_->createConfiguration(name);
}

CfgError XBotGoConfigService::renameConfiguration(const std::string &configId, const std::string &name)
{
	return impl_->renameConfiguration(configId, name);
}

CfgError XBotGoConfigService::deleteConfiguration(const std::string &configId)
{
	return impl_->deleteConfiguration(configId);
}

CfgError XBotGoConfigService::switchConfiguration(const std::string &configId)
{
	return impl_->switchConfiguration(configId);
}

CfgError XBotGoConfigService::reloadCurrentConfiguration()
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

} // namespace xbotgo
