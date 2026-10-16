#include "ConfigDemoWindow.h"
#include <xbotgo/storage/XBotGoConfigService.h>
#include <QApplication>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	// 使用 Qt Fusion 绘制控件和输入对话框，避免 macOS 夜间模式继续接管局部控件样式。
	app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	// Demo 使用固定浅色布局；覆盖系统夜间 palette，避免系统暗色文字与白色控件背景对比不足。
	QPalette palette = app.palette();
	palette.setColor(QPalette::Window, QColor("#F4F6F8"));
	palette.setColor(QPalette::WindowText, QColor("#243247"));
	palette.setColor(QPalette::Base, QColor("#FFFFFF"));
	palette.setColor(QPalette::AlternateBase, QColor("#F8FAFC"));
	palette.setColor(QPalette::Text, QColor("#243247"));
	palette.setColor(QPalette::Button, QColor("#FFFFFF"));
	palette.setColor(QPalette::ButtonText, QColor("#243247"));
	palette.setColor(QPalette::Highlight, QColor("#E8F0FC"));
	palette.setColor(QPalette::HighlightedText, QColor("#245796"));
	app.setPalette(palette);
	const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
	auto *service = xbotgo::XBotGoConfigService::instance();
	if (!service->initialize(root)) {
		QMessageBox::critical(nullptr, QStringLiteral("初始化失败"),
				      QString::fromStdString(service->lastError()));
		return 1;
	}
	ConfigDemoWindow window;
	window.show();
	const int result = app.exec();
	service->shutdown();
	if (!service->lastError().empty()) {
		QMessageBox::critical(nullptr, QStringLiteral("配置保存失败"),
				      QString::fromStdString(service->lastError()));
		return 1;
	}
	return result;
}
