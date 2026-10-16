#include <xbotgo/storage/IImageParametersConfig.h>
#include <xbotgo/storage/XBotGoConfigService.h>
#include "common/StorageValidation.h"
#include "imageparam/ImageParametersCodec.h"
#include "index/IndexCodec.h"
#include "imageparam/ImageParametersConfigImpl.h"
#include "io/StorageWorker.h"

#include <QCoreApplication>
#include <QEvent>
#include <QUuid>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace xbotgo;
namespace {
bool isDefault(const ImageParametersData &data)
{
	return data.specification == ImageSpecification::P1080_30 && data.encoding == ImageEncoding::H264 &&
	       data.whiteBalanceLocked && data.antiFlicker == AntiFlicker::Auto;
}
struct TempDir {
	std::filesystem::path path = std::filesystem::temp_directory_path() /
				     QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
	TempDir() { std::filesystem::create_directories(path); }
	~TempDir() { std::filesystem::remove_all(path); }
};
void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
void require(const CfgError &error, const char *message)
{
	if (error) {
		throw std::runtime_error(message);
	}
}
void requireError(const CfgError &error, const char *message)
{
	if (!error) {
		throw std::runtime_error(message);
	}
}
std::filesystem::path file(const std::filesystem::path &root, const std::string &id)
{
	return root / "configurations" / "v4" / id / "config.json";
}
void events()
{
	for (int i = 0; i < 3; ++i) {
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	}
	QCoreApplication::processEvents();
}
void testFormat()
{
	constexpr ObsDataField format[] = {{"text", OBS_DATA_STRING},
					   {"number", OBS_DATA_NUMBER},
					   {"flag", OBS_DATA_BOOLEAN},
					   {"object", OBS_DATA_OBJECT},
					   {"array", OBS_DATA_ARRAY}};
	ObsData data(obs_data_create()), obj(obs_data_create());
	obs_data_set_string(data.get(), "text", "ok");
	obs_data_set_int(data.get(), "number", 1);
	obs_data_set_bool(data.get(), "flag", true);
	obs_data_set_obj(data.get(), "object", obj.get());
	auto *array = obs_data_array_create();
	obs_data_set_array(data.get(), "array", array);
	obs_data_array_release(array);
	std::string error;
	require(StorageValidation::validateObsDataFormat(data.get(), format, error),
		"format validator rejected valid JSON");
	obs_data_set_bool(data.get(), "text", true);
	require(!StorageValidation::validateObsDataFormat(data.get(), format, error),
		"format validator accepted bad type");
}
void testService()
{
	TempDir dir;
	auto *service = XBotGoConfigService::instance();
	require(service->initialize(dir.path), "initialize failed");
	auto *image = service->imageParametersConfig();
	const auto first = service->currentConfigurationId();
	ImageParametersData value{ImageSpecification::P4K_30, ImageEncoding::H265, false, AntiFlicker::Hz60};
	require(image->setAll(value), "set failed");
	events();
	require(service->createConfiguration("second"), "create failed");
	const auto second = service->currentConfigurationId();
	events();
	require(isDefault(image->getAll()), "new configuration did not reset data");
	require(service->switchConfiguration(first), "switch failed");
	events();
	require(image->getAll().specification == ImageSpecification::P4K_30, "switch did not restore data");
	require(service->deleteConfiguration(second), "delete failed");
	require(!std::filesystem::exists(file(dir.path, second)), "delete did not remove directory");
	requireError(service->deleteConfiguration(first), "current delete accepted");
	service->shutdown();
	require(service->initialize(dir.path), "restart failed");
	require(service->currentConfigurationId() == first, "index current id not restored");
	service->shutdown();
}
void testSignal()
{
	TempDir dir;
	auto *service = XBotGoConfigService::instance();
	require(service->initialize(dir.path), "signal init failed");
	events(); // 初始化替换通知不能计入后续 setter 的信号断言。
	auto *image = service->imageParametersConfig();
	int count = 0;
	QObject receiver;
	QObject::connect(image, &IImageParametersConfig::changed, &receiver, [&](ImageParametersData) {
		++count;
		image->getAll();
	});
	require(image->setSpecification(ImageSpecification::P4K_30), "signal setter failed");
	events();
	require(count == 0, "setter emitted signal");
	require(service->createConfiguration("signal"), "signal create failed");
	events();
	require(count == 1, "replace signal missing");
	service->shutdown();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	try {
		testFormat();
		testService();
		testSignal();
		std::cout << "Storage flow checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		XBotGoConfigService::instance()->shutdown();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
