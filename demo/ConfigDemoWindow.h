#pragma once

#include <QMainWindow>

class QListWidget;
class QPushButton;
class QLabel;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;

namespace xbotgo {
struct CfgError;
struct ImageParametersData;
}

class ConfigDemoWindow final : public QMainWindow {
public:
	explicit ConfigDemoWindow(QWidget *parent = nullptr);

private:
	void buildUi();
	// 刷新配置列表、当前名称及管理按钮状态，不读取图像参数。
	void refreshConfigurationList();
	void createConfiguration();
	void renameConfiguration();
	void deleteConfiguration();
	void switchConfiguration();
	void reloadConfiguration();
	void showError(const xbotgo::CfgError &error);
	void openConfigurationDirectory();
	QWidget *buildImagePanel();
	void refreshImageEditor();
	void updateImageEditor(const xbotgo::ImageParametersData &data);
	void readMemorySnapshot();
	void refreshSaveStatus();
	void finishImageEdit(bool success);

private:
	QListWidget *configurationList_ = nullptr;
	QPushButton *createButton_ = nullptr;
	QPushButton *renameButton_ = nullptr;
	QPushButton *deleteButton_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *saveStatusLabel_ = nullptr;
	QComboBox *specificationComboBox_ = nullptr;
	QComboBox *encodingComboBox_ = nullptr;
	QCheckBox *whiteBalanceCheckBox_ = nullptr;
	QComboBox *antiFlickerComboBox_ = nullptr;
	QPlainTextEdit *memorySnapshotEdit_ = nullptr;
};
