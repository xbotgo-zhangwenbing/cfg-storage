#include "ConfigDemoWindow.h"

#include <xbotgo/storage/XBotGoConfigService.h>
#include <xbotgo/storage/IImageParametersConfig.h>

#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QSignalBlocker>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace xbotgo;

ConfigDemoWindow::ConfigDemoWindow(QWidget *parent) : QMainWindow(parent)
{
	buildUi();
	connect(XBotGoConfigService::instance()->imageParametersConfig(), &IImageParametersConfig::changed, this,
		[this](const ImageParametersData &data) {
			memorySnapshotEdit_->clear();
			refreshConfigurationList();
			updateImageEditor(data);
		});
	refreshConfigurationList();
	refreshImageEditor();
	refreshSaveStatus();
	// 这里只查询保存进度，不读取图像业务内存或更新手动读取文本。
	auto *statusTimer = new QTimer(this);
	statusTimer->setInterval(250);
	connect(statusTimer, &QTimer::timeout, this, [this] { refreshSaveStatus(); });
	statusTimer->start();
}

void ConfigDemoWindow::buildUi()
{
	setWindowTitle(QStringLiteral("XBotGo 配置管理"));
	resize(960, 640);
	setMinimumSize(820, 540);
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("demoRoot"));
	auto *outer = new QVBoxLayout(root);
	outer->setContentsMargins(24, 22, 24, 22);
	outer->setSpacing(20);
	auto *header = new QHBoxLayout();
	auto *heading = new QVBoxLayout();
	auto *title = new QLabel(QStringLiteral("配置管理"), root);
	title->setObjectName(QStringLiteral("pageTitle"));
	heading->addWidget(title);
	statusLabel_ = new QLabel(root);
	statusLabel_->setObjectName(QStringLiteral("currentConfigurationLabel"));
	heading->addWidget(statusLabel_);
	header->addLayout(heading);
	header->addStretch();
	auto *openDirectoryButton = new QPushButton(QStringLiteral("打开配置目录"), root);
	openDirectoryButton->setToolTip(
		QString::fromStdString(XBotGoConfigService::instance()->configurationRootDirectory().string()));
	auto *reloadButton = new QPushButton(QStringLiteral("重新加载"), root);
	header->addWidget(reloadButton);
	header->addWidget(openDirectoryButton);
	connect(reloadButton, &QPushButton::clicked, this, [this] { reloadConfiguration(); });
	outer->addLayout(header);

	auto *body = new QHBoxLayout();
	body->setSpacing(20);
	auto *configurationPanel = new QGroupBox(QStringLiteral("配置列表"), root);
	configurationPanel->setFixedWidth(290);
	auto *configurationLayout = new QVBoxLayout(configurationPanel);
	configurationLayout->setSpacing(12);
	configurationList_ = new QListWidget(configurationPanel);
	configurationList_->setSpacing(3);
	configurationLayout->addWidget(configurationList_, 1);
	auto *buttons = new QHBoxLayout();
	createButton_ = new QPushButton(QStringLiteral("新建"), configurationPanel);
	renameButton_ = new QPushButton(QStringLiteral("重命名"), configurationPanel);
	deleteButton_ = new QPushButton(QStringLiteral("删除"), configurationPanel);
	createButton_->setMinimumWidth(72);
	renameButton_->setMinimumWidth(72);
	deleteButton_->setMinimumWidth(72);
	buttons->setSpacing(8);
	buttons->addWidget(createButton_);
	buttons->addWidget(renameButton_);
	buttons->addWidget(deleteButton_);
	configurationLayout->addLayout(buttons);
	body->addWidget(configurationPanel);

	auto *editorLayout = new QVBoxLayout();
	editorLayout->setSpacing(18);
	editorLayout->addWidget(buildImagePanel());
	auto *snapshotPanel = new QGroupBox(QStringLiteral("内存读取结果"), root);
	auto *snapshotLayout = new QVBoxLayout(snapshotPanel);
	snapshotLayout->setSpacing(12);
	auto *readRow = new QHBoxLayout();
	readRow->addStretch();
	auto *readButton = new QPushButton(QStringLiteral("读取内存"), snapshotPanel);
	readRow->addWidget(readButton);
	snapshotLayout->addLayout(readRow);
	memorySnapshotEdit_ = new QPlainTextEdit(snapshotPanel);
	memorySnapshotEdit_->setObjectName(QStringLiteral("memorySnapshotEdit"));
	memorySnapshotEdit_->setReadOnly(true);
	memorySnapshotEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	memorySnapshotEdit_->setPlaceholderText(QStringLiteral("点击“读取内存”查看当前值"));
	snapshotLayout->addWidget(memorySnapshotEdit_, 1);
	editorLayout->addWidget(snapshotPanel, 1);
	body->addLayout(editorLayout, 1);
	outer->addLayout(body, 1);
	saveStatusLabel_ = new QLabel(root);
	saveStatusLabel_->setObjectName(QStringLiteral("saveStatusLabel"));
	saveStatusLabel_->setWordWrap(true);
	outer->addWidget(saveStatusLabel_);
	setCentralWidget(root);

	// 编辑区与读取结果分开：只有点击读取按钮才更新文本快照。
	connect(readButton, &QPushButton::clicked, this, [this] { readMemorySnapshot(); });
	connect(openDirectoryButton, &QPushButton::clicked, this, [this] { openConfigurationDirectory(); });
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#demoRoot { background: #F4F6F8; color: #243247; }
		QLabel#pageTitle { font-size: 23px; font-weight: 600; }
		QLabel#currentConfigurationLabel { color: #66758A; margin-top: 4px; }
		QGroupBox { background: white; border: 1px solid #DCE2EA; border-radius: 8px;
			margin-top: 12px; padding: 18px 14px 14px; font-weight: 600; }
		QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 5px; }
		QPushButton { min-height: 32px; padding: 0 12px; border: 1px solid #D2DAE5;
			border-radius: 5px; background: white; color: #243247; }
		QPushButton:hover { background: #EDF3FB; border-color: #A7BDD9; }
		QPushButton:pressed { background: #DDE9F8; }
		QPushButton:disabled { color: #9AA6B5; background: #F5F6F8; }
		QListWidget { background: white; border: 0; outline: 0; }
		QListWidget::item { padding: 10px 8px; border-radius: 5px; color: #243247; }
		QListWidget::item:selected { background: #E8F0FC; color: #245796; }
		QLabel, QCheckBox { color: #243247; }
		QComboBox { min-height: 30px; min-width: 180px; padding-left: 9px;
			background: #FFFFFF; color: #243247; }
		QComboBox QAbstractItemView { background: #FFFFFF; color: #243247;
			selection-background-color: #E8F0FC; selection-color: #245796; }
		QPlainTextEdit { background: #F8FAFC; color: #243247; border: 1px solid #E1E7EF;
			border-radius: 5px; padding: 10px; font-size: 14px; }
	)"));
	connect(createButton_, &QPushButton::clicked, this, [this] { createConfiguration(); });
	connect(renameButton_, &QPushButton::clicked, this, [this] { renameConfiguration(); });
	connect(deleteButton_, &QPushButton::clicked, this, [this] { deleteConfiguration(); });
	// 单击只选中，双击才调用切换接口。
	connect(configurationList_, &QListWidget::itemDoubleClicked, this,
		[this](QListWidgetItem *) { switchConfiguration(); });
	connect(configurationList_, &QListWidget::currentRowChanged, this, [this](int row) {
		renameButton_->setEnabled(row >= 0);
		deleteButton_->setEnabled(row >= 0);
	});
}

void ConfigDemoWindow::deleteConfiguration()
{
	const auto *item = configurationList_->currentItem();
	if (!item) {
		return;
	}
	if (QMessageBox::question(this, QStringLiteral("删除配置"),
				  QStringLiteral("确定删除配置“%1”？").arg(item->text())) != QMessageBox::Yes) {
		return;
	}
	const auto error = XBotGoConfigService::instance()->deleteConfiguration(
		item->data(Qt::UserRole).toString().toStdString());
	if (error) {
		auto *toast = new QLabel(QString::fromStdString(error.description), this);
		toast->setStyleSheet(QStringLiteral("background: #FFF1F0; color: #B42318; border: 1px solid #FDA29B; "
						    "border-radius: 5px; padding: 9px 14px;"));
		toast->adjustSize();
		toast->move((width() - toast->width()) / 2, height() - toast->height() - 50);
		toast->show();
		QTimer::singleShot(3000, toast, &QWidget::deleteLater);
		return;
	}
	// 删除成功后当前配置未变化，但索引列表已变化，需要单独刷新列表。
	refreshConfigurationList();
}

void ConfigDemoWindow::refreshConfigurationList()
{
	auto *service = XBotGoConfigService::instance();
	const auto configurations = service->configurations();
	const auto currentId = service->currentConfigurationId();
	configurationList_->clear();
	int currentRow = -1;
	for (int index = 0; index < static_cast<int>(configurations.size()); ++index) {
		const auto &configuration = configurations[index];
		auto *item = new QListWidgetItem(QString::fromStdString(configuration.name), configurationList_);
		// 通过 ID 调用 Service，重命名不会改变列表条目的身份。
		item->setData(Qt::UserRole, QString::fromStdString(configuration.id));
		const auto createdAt =
			QDateTime::fromString(QString::fromStdString(configuration.createdAt), Qt::ISODateWithMs);
		item->setToolTip(QStringLiteral("创建时间：%1")
					 .arg(createdAt.isValid() ? createdAt.toLocalTime().toString(
									    QStringLiteral("yyyy-MM-dd HH:mm:ss"))
								  : QStringLiteral("未知")));
		if (configuration.id == currentId) {
			currentRow = index;
			statusLabel_->setText(
				QStringLiteral("当前配置：%1").arg(QString::fromStdString(configuration.name)));
		}
	}
	configurationList_->setCurrentRow(currentRow);
	renameButton_->setEnabled(currentRow >= 0);
	createButton_->setEnabled(configurations.size() < 50);
	deleteButton_->setEnabled(currentRow >= 0);
}

void ConfigDemoWindow::createConfiguration()
{
	bool accepted = false;
	const auto name = QInputDialog::getText(this, QStringLiteral("新建配置"), QStringLiteral("配置名称"),
						QLineEdit::Normal, {}, &accepted);
	if (!accepted) {
		return;
	}
	const auto error = XBotGoConfigService::instance()->createConfiguration(name.toStdString());
	if (error) {
		showError(error);
		return;
	}
}

void ConfigDemoWindow::renameConfiguration()
{
	const auto *item = configurationList_->currentItem();
	if (!item) {
		return;
	}
	const auto id = item->data(Qt::UserRole).toString().toStdString();
	bool accepted = false;
	const auto name = QInputDialog::getText(this, QStringLiteral("重命名配置"), QStringLiteral("配置名称"),
						QLineEdit::Normal, item->text(), &accepted);
	if (!accepted) {
		return;
	}
	const auto error = XBotGoConfigService::instance()->renameConfiguration(id, name.toStdString());
	if (error) {
		showError(error);
		return;
	}
	refreshConfigurationList();
}

void ConfigDemoWindow::switchConfiguration()
{
	const auto *item = configurationList_->currentItem();
	if (!item) {
		return;
	}
	const auto error = XBotGoConfigService::instance()->switchConfiguration(
		item->data(Qt::UserRole).toString().toStdString());
	if (error) {
		showError(error);
		return;
	}
}

void ConfigDemoWindow::reloadConfiguration()
{
	const auto error = XBotGoConfigService::instance()->reloadCurrentConfiguration();
	if (error) {
		showError(error);
		return;
	}
}

QWidget *ConfigDemoWindow::buildImagePanel()
{
	auto *panel = new QGroupBox(QStringLiteral("图像参数"), this);
	auto *layout = new QFormLayout(panel);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->setHorizontalSpacing(22);
	layout->setVerticalSpacing(14);
	specificationComboBox_ = new QComboBox(panel);
	specificationComboBox_->setObjectName(QStringLiteral("specificationComboBox"));
	specificationComboBox_->addItem(QStringLiteral("1080p30"), static_cast<int>(ImageSpecification::P1080_30));
	specificationComboBox_->addItem(QStringLiteral("1080p60"), static_cast<int>(ImageSpecification::P1080_60));
	specificationComboBox_->addItem(QStringLiteral("4K30"), static_cast<int>(ImageSpecification::P4K_30));
	encodingComboBox_ = new QComboBox(panel);
	encodingComboBox_->setObjectName(QStringLiteral("encodingComboBox"));
	encodingComboBox_->addItem(QStringLiteral("H.264"), static_cast<int>(ImageEncoding::H264));
	encodingComboBox_->addItem(QStringLiteral("H.265"), static_cast<int>(ImageEncoding::H265));
	whiteBalanceCheckBox_ = new QCheckBox(QStringLiteral("锁定"), panel);
	whiteBalanceCheckBox_->setObjectName(QStringLiteral("whiteBalanceCheckBox"));
	antiFlickerComboBox_ = new QComboBox(panel);
	antiFlickerComboBox_->setObjectName(QStringLiteral("antiFlickerComboBox"));
	antiFlickerComboBox_->addItem(QStringLiteral("自动"), static_cast<int>(AntiFlicker::Auto));
	antiFlickerComboBox_->addItem(QStringLiteral("50 Hz"), static_cast<int>(AntiFlicker::Hz50));
	antiFlickerComboBox_->addItem(QStringLiteral("60 Hz"), static_cast<int>(AntiFlicker::Hz60));
	layout->addRow(QStringLiteral("规格"), specificationComboBox_);
	layout->addRow(QStringLiteral("编码"), encodingComboBox_);
	layout->addRow(QStringLiteral("白平衡"), whiteBalanceCheckBox_);
	layout->addRow(QStringLiteral("防频闪"), antiFlickerComboBox_);
	// 用户修改直接调用单字段 setter；界面回填时由 QSignalBlocker 阻止重复写入。
	connect(specificationComboBox_, &QComboBox::currentIndexChanged, this, [this](int) {
		finishImageEdit(XBotGoConfigService::instance()->imageParametersConfig()->setSpecification(
			static_cast<ImageSpecification>(specificationComboBox_->currentData().toInt())));
	});
	connect(encodingComboBox_, &QComboBox::currentIndexChanged, this, [this](int) {
		finishImageEdit(XBotGoConfigService::instance()->imageParametersConfig()->setEncoding(
			static_cast<ImageEncoding>(encodingComboBox_->currentData().toInt())));
	});
	connect(whiteBalanceCheckBox_, &QCheckBox::toggled, this, [this](bool locked) {
		finishImageEdit(
			XBotGoConfigService::instance()->imageParametersConfig()->setWhiteBalanceLocked(locked));
	});
	connect(antiFlickerComboBox_, &QComboBox::currentIndexChanged, this, [this](int) {
		finishImageEdit(XBotGoConfigService::instance()->imageParametersConfig()->setAntiFlicker(
			static_cast<AntiFlicker>(antiFlickerComboBox_->currentData().toInt())));
	});
	return panel;
}

void ConfigDemoWindow::refreshImageEditor()
{
	updateImageEditor(XBotGoConfigService::instance()->imageParametersConfig()->getAll());
}

void ConfigDemoWindow::updateImageEditor(const ImageParametersData &data)
{
	// 信号已携带完整快照，无需再次读取业务内存；回填不触发 setter。
	const QSignalBlocker specificationBlocker(specificationComboBox_);
	const QSignalBlocker encodingBlocker(encodingComboBox_);
	const QSignalBlocker whiteBalanceBlocker(whiteBalanceCheckBox_);
	const QSignalBlocker antiFlickerBlocker(antiFlickerComboBox_);
	specificationComboBox_->setCurrentIndex(specificationComboBox_->findData(static_cast<int>(data.specification)));
	encodingComboBox_->setCurrentIndex(encodingComboBox_->findData(static_cast<int>(data.encoding)));
	whiteBalanceCheckBox_->setChecked(data.whiteBalanceLocked);
	antiFlickerComboBox_->setCurrentIndex(antiFlickerComboBox_->findData(static_cast<int>(data.antiFlicker)));
}

void ConfigDemoWindow::finishImageEdit(bool success)
{
	if (!success) {
		QMessageBox::warning(this, QStringLiteral("图像参数"),
				     QStringLiteral("修改失败：服务已关闭或参数无效"));
		refreshImageEditor();
	}
}

void ConfigDemoWindow::refreshSaveStatus()
{
	auto *service = XBotGoConfigService::instance();
	auto status = QStringLiteral("修改版本 %1  ·  已保存版本 %2")
			      .arg(service->getMemoryRevision())
			      .arg(service->getSavedRevision());
	saveStatusLabel_->setText(status);
}

void ConfigDemoWindow::readMemorySnapshot()
{
	const auto data = XBotGoConfigService::instance()->imageParametersConfig()->getAll();
	// 显示 getter 返回的独立快照，不从界面控件取值，也不回填编辑区。
	memorySnapshotEdit_->setPlainText(
		QStringLiteral("规格：%1\n编码：%2\n白平衡：%3\n防频闪：%4")
			.arg(specificationComboBox_->itemText(
				     specificationComboBox_->findData(static_cast<int>(data.specification))),
			     encodingComboBox_->itemText(encodingComboBox_->findData(static_cast<int>(data.encoding))),
			     data.whiteBalanceLocked ? QStringLiteral("锁定") : QStringLiteral("未锁定"),
			     antiFlickerComboBox_->itemText(
				     antiFlickerComboBox_->findData(static_cast<int>(data.antiFlicker)))));
}

void ConfigDemoWindow::showError(const xbotgo::CfgError &error)
{
	QMessageBox::warning(this, QStringLiteral("配置管理"),
				     QString::fromStdString(error.description));
}

void ConfigDemoWindow::openConfigurationDirectory()
{
	const auto directory = XBotGoConfigService::instance()->configurationRootDirectory();
	const auto path = QString::fromStdString(directory.string());
	// 使用 Service 的真实配置根目录，不根据可修改的配置名称拼接路径。
	if (directory.empty() || !QDir().mkpath(path)) {
		QMessageBox::warning(this, QStringLiteral("配置目录"),
				     QStringLiteral("无法创建配置目录：%1").arg(path));
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
		QMessageBox::warning(this, QStringLiteral("配置目录"),
				     QStringLiteral("无法打开配置目录：%1").arg(path));
	}
}
