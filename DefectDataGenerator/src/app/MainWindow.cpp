#include "app/MainWindow.h"
#include "ui_MainWindow.h"

#include "core/Logger.h"
#include "vision/BackendFactory.h"
#include "vision/IGenerationBackend.h"
#include "vision/MaskUtils.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUuid>
#include <opencv2/imgcodecs.hpp>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->okListView->setModel(&m_okModel);
    ui->defectListView->setModel(&m_defectModel);
    ui->assetListView->setModel(&m_assetModel);
    ui->generatedListView->setModel(&m_generatedModel);
    ui->okListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->defectListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->assetListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->generatedListView->setContextMenuPolicy(Qt::CustomContextMenu);
    setupConnections();
    connect(&Logger::instance(), &Logger::messageLogged, this, &MainWindow::appendLog);
    appendLog(QString(u8"准备就绪"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupConnections()
{
    connect(ui->newProjectAction, &QAction::triggered, this, &MainWindow::newProject);
    connect(ui->openProjectAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(ui->importOkAction, &QAction::triggered, this, &MainWindow::importOkImages);
    connect(ui->importDefectAction, &QAction::triggered, this, &MainWindow::importDefectImages);
    connect(ui->saveAssetButton, &QPushButton::clicked, this, &MainWindow::saveCurrentMaskAsAsset);
    connect(ui->generateButton, &QPushButton::clicked, this, &MainWindow::generateImages);
    connect(ui->approveButton, &QPushButton::clicked, this, &MainWindow::approveSelectedGenerated);
    connect(ui->rejectButton, &QPushButton::clicked, this, &MainWindow::rejectSelectedGenerated);
    connect(ui->showGeneratedMaskCheck, &QCheckBox::toggled, this, &MainWindow::onGeneratedSelected);
    connect(ui->exportButton, &QPushButton::clicked, this, &MainWindow::exportApprovedDataset);
    connect(ui->viewButton, &QPushButton::clicked, this, &MainWindow::setToolView);
    connect(ui->rectButton, &QPushButton::clicked, this, &MainWindow::setToolRect);
    connect(ui->polygonButton, &QPushButton::clicked, this, &MainWindow::setToolPolygon);
    connect(ui->brushButton, &QPushButton::clicked, this, &MainWindow::setToolBrush);
    connect(ui->eraserButton, &QPushButton::clicked, this, &MainWindow::setToolEraser);
    connect(ui->clearMaskButton, &QPushButton::clicked, this, &MainWindow::clearMask);
    connect(ui->brushSizeSpin, qOverload<int>(&QSpinBox::valueChanged), ui->imageView, &ImageView::setBrushSize);
    connect(ui->imageView, &ImageView::statusTextChanged, this, [this](const QString &text) {
        ui->statusBar->showMessage(text);
    });
    connect(ui->okListView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onOkImageSelected);
    connect(ui->defectListView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onDefectSourceSelected);
    connect(ui->generatedListView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onGeneratedSelected);
    connect(ui->okListView, &QListView::customContextMenuRequested, this, &MainWindow::showOkContextMenu);
    connect(ui->defectListView, &QListView::customContextMenuRequested, this, &MainWindow::showDefectSourceContextMenu);
    connect(ui->assetListView, &QListView::customContextMenuRequested, this, &MainWindow::showAssetContextMenu);
    connect(ui->generatedListView, &QListView::customContextMenuRequested, this, &MainWindow::showGeneratedContextMenu);
}

void MainWindow::newProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QString(u8"选择项目目录"));
    if (dir.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString productName = QInputDialog::getText(this, QString(u8"产品"), QString(u8"产品名称"), QLineEdit::Normal, QString(u8"默认产品"), &ok);
    if (!ok) {
        return;
    }
    QString error;
    if (!m_store.createProject(dir, productName, &error)) {
        showError(error);
        return;
    }
    ProjectData data;
    QStringList validation;
    if (!m_store.loadProject(dir, &data, &validation, &error)) {
        showError(error);
        return;
    }
    m_project = data;
    setProjectDir(dir);
    refreshProjectViews();
    appendLog(QString(u8"项目已创建: %1").arg(dir));
}

void MainWindow::openProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QString(u8"打开项目目录"));
    if (dir.isEmpty()) {
        return;
    }
    QString error;
    QStringList validation;
    ProjectData data;
    if (!m_store.loadProject(dir, &data, &validation, &error)) {
        showError(error);
        return;
    }
    m_project = data;
    setProjectDir(dir);
    refreshProjectViews();
    for (const QString &line : validation) {
        Logger::instance().log(Logger::Warning, line);
    }
    appendLog(QString(u8"项目已打开: %1").arg(dir));
}

void MainWindow::importOkImages()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, QString(u8"导入 OK 图像"), QString(), QString(u8"图像 (*.png *.jpg *.jpeg *.bmp)"));
    if (files.isEmpty()) {
        return;
    }
    for (const QString &file : files) {
        QString error;
        if (!m_store.importOkImage(m_projectDir, file, &m_project, &error)) {
            showError(error);
            return;
        }
    }
    if (saveProject()) {
        refreshProjectViews();
    }
}

void MainWindow::importDefectImages()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, QString(u8"导入缺陷源图"), QString(), QString(u8"图像 (*.png *.jpg *.jpeg *.bmp)"));
    if (files.isEmpty()) {
        return;
    }
    QStringList current = m_defectModel.stringList();
    for (const QString &file : files) {
        QString error;
        QString relativePath;
        if (!m_store.importDefectSource(m_projectDir, file, &relativePath, &error)) {
            showError(error);
            return;
        }
        current.append(relativePath);
    }
    m_defectModel.setStringList(current);
    appendLog(QString(u8"已导入 %1 张缺陷源图").arg(files.size()));
}

void MainWindow::saveCurrentMaskAsAsset()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString sourceRelative = currentDefectSourceRelativePath();
    if (sourceRelative.isEmpty()) {
        showError(QString(u8"请先选择一张缺陷源图"));
        return;
    }
    const QString defectType = ui->defectTypeEdit->text().trimmed();
    if (defectType.isEmpty()) {
        showError(QString(u8"缺陷类型为空"));
        return;
    }

    const QString tempMaskPath = makeTempMaskPath();
    QString error;
    if (!ui->imageView->saveMask(tempMaskPath, &error)) {
        showError(error);
        return;
    }

    cv::Mat mask;
    if (!MaskUtils::loadGrayMask(tempMaskPath, &mask, &error)) {
        showError(error);
        return;
    }
    QRect bbox;
    int area = 0;
    if (!MaskUtils::maskStats(mask, &bbox, &area, &error)) {
        showError(error);
        return;
    }
    if (!m_store.addDefectAsset(m_projectDir, sourceRelative, tempMaskPath, defectType, bbox, area, &m_project, &error)) {
        showError(error);
        return;
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"缺陷素材已保存. 类型=%1 面积=%2").arg(defectType).arg(area));
    }
}

void MainWindow::generateImages()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString okPath = selectedOkImagePath();
    if (okPath.isEmpty()) {
        showError(QString(u8"生成前请先选择一张 OK 图像"));
        return;
    }
    if (m_project.defectAssets.isEmpty()) {
        showError(QString(u8"没有可用的缺陷素材"));
        return;
    }

    GenerateRequest request;
    request.projectDir = m_projectDir;
    request.okImagePath = okPath;
    request.count = ui->countSpin->value();
    request.defectsPerImageMin = ui->defectMinSpin->value();
    request.defectsPerImageMax = ui->defectMaxSpin->value();
    request.seed = ui->seedSpin->value();
    if (request.defectsPerImageMax < request.defectsPerImageMin) {
        showError(QString(u8"单图缺陷数最大值必须大于等于最小值"));
        return;
    }
    if (ui->scaleMaxSpin->value() < ui->scaleMinSpin->value()) {
        showError(QString(u8"缩放最大值必须大于等于最小值"));
        return;
    }
    if (ui->rotationMaxSpin->value() < ui->rotationMinSpin->value()) {
        showError(QString(u8"旋转最大值必须大于等于最小值"));
        return;
    }
    if (ui->brightnessMaxSpin->value() < ui->brightnessMinSpin->value()) {
        showError(QString(u8"亮度最大值必须大于等于最小值"));
        return;
    }
    if (ui->featherMaxSpin->value() < ui->featherMinSpin->value()) {
        showError(QString(u8"羽化最大值必须大于等于最小值"));
        return;
    }
    request.params.insert(QStringLiteral("scaleMin"), ui->scaleMinSpin->value());
    request.params.insert(QStringLiteral("scaleMax"), ui->scaleMaxSpin->value());
    request.params.insert(QStringLiteral("rotationMin"), ui->rotationMinSpin->value());
    request.params.insert(QStringLiteral("rotationMax"), ui->rotationMaxSpin->value());
    request.params.insert(QStringLiteral("brightnessMin"), ui->brightnessMinSpin->value());
    request.params.insert(QStringLiteral("brightnessMax"), ui->brightnessMaxSpin->value());
    request.params.insert(QStringLiteral("featherMin"), ui->featherMinSpin->value());
    request.params.insert(QStringLiteral("featherMax"), ui->featherMaxSpin->value());
    const QString selectedType = ui->generateTypeCombo->currentText();
    for (const DefectAssetRecord &asset : m_project.defectAssets) {
        if (selectedType == QString(u8"全部") || asset.defectType == selectedType) {
            request.defectAssetIds.append(asset.id);
        }
    }
    if (request.defectAssetIds.isEmpty()) {
        showError(QString(u8"没有匹配当前缺陷类型的缺陷素材"));
        return;
    }

    std::unique_ptr<IGenerationBackend> backend = BackendFactory::createBackend(QStringLiteral("OpenCV"));
    if (!backend) {
        showError(QString(u8"OpenCV 生成后端不可用"));
        return;
    }
    setEnabled(false);
    GenerateResult result = backend->generate(request, m_project, m_store);
    setEnabled(true);
    if (!result.success) {
        showError(result.errorMessage);
        return;
    }
    refreshGeneratedList();
    appendLog(QString(u8"生成完成: %1 张").arg(result.items.size()));
}

void MainWindow::approveSelectedGenerated()
{
    const QString path = selectedGeneratedPath();
    if (path.isEmpty()) {
        showError(QString(u8"请先选择一张生成图"));
        return;
    }
    moveReviewedFileSet(path, QStringLiteral("approved"));
    refreshGeneratedList();
}

void MainWindow::rejectSelectedGenerated()
{
    const QString path = selectedGeneratedPath();
    if (path.isEmpty()) {
        showError(QString(u8"请先选择一张生成图"));
        return;
    }
    moveReviewedFileSet(path, QStringLiteral("rejected"));
    refreshGeneratedList();
}

void MainWindow::exportApprovedDataset()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString outDir = QFileDialog::getExistingDirectory(this, QString(u8"选择导出目录"));
    if (outDir.isEmpty()) {
        return;
    }
    QDir dir(outDir);
    dir.mkpath(QStringLiteral("images"));
    dir.mkpath(QStringLiteral("masks"));
    dir.mkpath(QStringLiteral("metadata"));

    QDir approvedDir(QDir(m_projectDir).filePath(QStringLiteral("approved")));
    const QFileInfoList imageFiles = approvedDir.entryInfoList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    QJsonArray items;
    for (const QFileInfo &imageInfo : imageFiles) {
        if (imageInfo.baseName().endsWith(QStringLiteral("_mask"))) {
            continue;
        }
        const QString maskName = imageInfo.completeBaseName() + QStringLiteral("_mask.png");
        const QString metaName = imageInfo.completeBaseName() + QStringLiteral(".json");
        const QString dstImage = dir.filePath(QStringLiteral("images/") + imageInfo.fileName());
        const QString dstMask = dir.filePath(QStringLiteral("masks/") + maskName);
        const QString dstMeta = dir.filePath(QStringLiteral("metadata/") + metaName);
        if (!QFile::copy(imageInfo.absoluteFilePath(), dstImage)) {
            showError(QString(u8"导出图像失败: %1").arg(imageInfo.fileName()));
            return;
        }
        const QString srcMask = approvedDir.filePath(maskName);
        if (!QFile::copy(srcMask, dstMask)) {
            showError(QString(u8"导出 mask 失败: %1").arg(maskName));
            return;
        }
        const QString srcMeta = approvedDir.filePath(metaName);
        if (QFileInfo::exists(srcMeta) && !QFile::copy(srcMeta, dstMeta)) {
            showError(QString(u8"导出 metadata 失败: %1").arg(metaName));
            return;
        }
        QJsonObject item;
        item.insert(QStringLiteral("image"), QStringLiteral("images/") + imageInfo.fileName());
        item.insert(QStringLiteral("mask"), QStringLiteral("masks/") + maskName);
        item.insert(QStringLiteral("metadata"), QStringLiteral("metadata/") + metaName);
        items.append(item);
    }

    QJsonObject dataset;
    dataset.insert(QStringLiteral("productName"), m_project.productName);
    dataset.insert(QStringLiteral("items"), items);
    QFile datasetFile(dir.filePath(QStringLiteral("dataset.json")));
    if (!datasetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(QString(u8"写入 dataset.json 失败: %1").arg(datasetFile.errorString()));
        return;
    }
    datasetFile.write(QJsonDocument(dataset).toJson(QJsonDocument::Indented));
    appendLog(QString(u8"已导出通过数据集: %1 条").arg(items.size()));
}

void MainWindow::onOkImageSelected()
{
    const QString path = selectedOkImagePath();
    if (!path.isEmpty()) {
        QString error;
        if (!ui->imageView->loadImage(path, &error)) {
            showError(error);
        }
    }
}

void MainWindow::onDefectSourceSelected()
{
    const QString relative = currentDefectSourceRelativePath();
    if (relative.isEmpty()) {
        return;
    }
    QString error;
    if (!ui->imageView->loadImage(m_store.absolutePath(m_projectDir, relative), &error)) {
        showError(error);
    }
}

void MainWindow::onGeneratedSelected()
{
    const QString path = selectedGeneratedPath();
    if (!path.isEmpty()) {
        QString error;
        if (!ui->imageView->loadImage(path, &error)) {
            showError(error);
        }
        if (ui->showGeneratedMaskCheck->isChecked()) {
            const QString maskPath = QFileInfo(path).absolutePath() + QDir::separator() + QFileInfo(path).completeBaseName() + QStringLiteral("_mask.png");
            QImage mask(maskPath);
            if (!mask.isNull()) {
                ui->imageView->setMask(mask);
            }
        }
    }
}

void MainWindow::showOkContextMenu(const QPoint &pos)
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QModelIndex index = ui->okListView->indexAt(pos);
    if (index.isValid()) {
        ui->okListView->setCurrentIndex(index);
    }
    QMenu menu(this);
    QAction *deleteAction = menu.addAction(QString(u8"删除选中项"));
    deleteAction->setEnabled(ui->okListView->currentIndex().isValid());
    QAction *clearAction = menu.addAction(QString(u8"清空全部"));
    clearAction->setEnabled(!m_project.okImages.isEmpty());
    QAction *chosen = menu.exec(ui->okListView->viewport()->mapToGlobal(pos));
    if (chosen == deleteAction) {
        deleteSelectedOkImage();
    } else if (chosen == clearAction) {
        clearOkImages();
    }
}

void MainWindow::showDefectSourceContextMenu(const QPoint &pos)
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QModelIndex index = ui->defectListView->indexAt(pos);
    if (index.isValid()) {
        ui->defectListView->setCurrentIndex(index);
    }
    QMenu menu(this);
    QAction *deleteAction = menu.addAction(QString(u8"删除选中项"));
    deleteAction->setEnabled(ui->defectListView->currentIndex().isValid());
    QAction *clearAction = menu.addAction(QString(u8"清空全部"));
    clearAction->setEnabled(!m_defectModel.stringList().isEmpty());
    QAction *chosen = menu.exec(ui->defectListView->viewport()->mapToGlobal(pos));
    if (chosen == deleteAction) {
        deleteSelectedDefectSource();
    } else if (chosen == clearAction) {
        clearDefectSources();
    }
}

void MainWindow::showAssetContextMenu(const QPoint &pos)
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QModelIndex index = ui->assetListView->indexAt(pos);
    if (index.isValid()) {
        ui->assetListView->setCurrentIndex(index);
    }
    QMenu menu(this);
    QAction *deleteAction = menu.addAction(QString(u8"删除选中项"));
    deleteAction->setEnabled(ui->assetListView->currentIndex().isValid());
    QAction *clearAction = menu.addAction(QString(u8"清空全部"));
    clearAction->setEnabled(!m_project.defectAssets.isEmpty());
    QAction *chosen = menu.exec(ui->assetListView->viewport()->mapToGlobal(pos));
    if (chosen == deleteAction) {
        deleteSelectedAsset();
    } else if (chosen == clearAction) {
        clearAssets();
    }
}

void MainWindow::showGeneratedContextMenu(const QPoint &pos)
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QModelIndex index = ui->generatedListView->indexAt(pos);
    if (index.isValid()) {
        ui->generatedListView->setCurrentIndex(index);
    }
    QMenu menu(this);
    QAction *deleteAction = menu.addAction(QString(u8"删除选中项"));
    deleteAction->setEnabled(ui->generatedListView->currentIndex().isValid());
    QAction *clearAction = menu.addAction(QString(u8"清空全部"));
    clearAction->setEnabled(!m_generatedModel.stringList().isEmpty());
    QAction *chosen = menu.exec(ui->generatedListView->viewport()->mapToGlobal(pos));
    if (chosen == deleteAction) {
        deleteSelectedGenerated();
    } else if (chosen == clearAction) {
        clearGeneratedImages();
    }
}

void MainWindow::deleteSelectedOkImage()
{
    const int row = ui->okListView->currentIndex().row();
    if (row < 0 || row >= m_project.okImages.size()) {
        showError(QString(u8"请先选择一张 OK 图像"));
        return;
    }
    if (!confirmDelete(QString(u8"删除选中的 OK 图像吗"))) {
        return;
    }
    QString error;
    if (!removeOkImageAt(row, &error)) {
        showError(error);
        return;
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除选中的 OK 图像"));
    }
}

void MainWindow::clearOkImages()
{
    if (m_project.okImages.isEmpty()) {
        return;
    }
    if (!confirmDelete(QString(u8"删除全部 OK 图像吗"))) {
        return;
    }
    QString error;
    while (!m_project.okImages.isEmpty()) {
        if (!removeOkImageAt(m_project.okImages.size() - 1, &error)) {
            showError(error);
            return;
        }
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除全部 OK 图像"));
    }
}

void MainWindow::deleteSelectedDefectSource()
{
    const QString relativePath = currentDefectSourceRelativePath();
    if (relativePath.isEmpty()) {
        showError(QString(u8"请先选择一张缺陷源图"));
        return;
    }
    if (!confirmDelete(QString(u8"删除选中的缺陷源图及关联素材吗"))) {
        return;
    }
    QString error;
    if (!removeDefectSourceByRelativePath(relativePath, &error)) {
        showError(error);
        return;
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除缺陷源图: %1").arg(relativePath));
    }
}

void MainWindow::clearDefectSources()
{
    const QStringList sources = m_defectModel.stringList();
    if (sources.isEmpty()) {
        return;
    }
    if (!confirmDelete(QString(u8"删除全部缺陷源图及关联素材吗"))) {
        return;
    }
    QString error;
    for (const QString &relativePath : sources) {
        if (!removeDefectSourceByRelativePath(relativePath, &error)) {
            showError(error);
            return;
        }
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除全部缺陷源图"));
    }
}

void MainWindow::deleteSelectedAsset()
{
    const int row = ui->assetListView->currentIndex().row();
    if (row < 0 || row >= m_project.defectAssets.size()) {
        showError(QString(u8"请先选择一个缺陷素材"));
        return;
    }
    if (!confirmDelete(QString(u8"删除选中的缺陷素材吗"))) {
        return;
    }
    QString error;
    if (!removeAssetAt(row, &error)) {
        showError(error);
        return;
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除选中的缺陷素材"));
    }
}

void MainWindow::clearAssets()
{
    if (m_project.defectAssets.isEmpty()) {
        return;
    }
    if (!confirmDelete(QString(u8"删除全部缺陷素材吗. 缺陷源图会保留"))) {
        return;
    }
    QString error;
    while (!m_project.defectAssets.isEmpty()) {
        if (!removeAssetAt(m_project.defectAssets.size() - 1, &error)) {
            showError(error);
            return;
        }
    }
    if (saveProject()) {
        refreshProjectViews();
        appendLog(QString(u8"已删除全部缺陷素材"));
    }
}

void MainWindow::deleteSelectedGenerated()
{
    const QString imagePath = selectedGeneratedPath();
    if (imagePath.isEmpty()) {
        showError(QString(u8"请先选择一张生成图"));
        return;
    }
    if (!confirmDelete(QString(u8"删除选中的生成图 mask 和 metadata 吗"))) {
        return;
    }
    QString error;
    if (!removeGeneratedFileSet(imagePath, &error)) {
        showError(error);
        return;
    }
    refreshGeneratedList();
    appendLog(QString(u8"已删除选中的生成项"));
}

void MainWindow::clearGeneratedImages()
{
    const QStringList images = m_generatedModel.stringList();
    if (images.isEmpty()) {
        return;
    }
    if (!confirmDelete(QString(u8"删除全部生成图 mask 和 metadata 吗"))) {
        return;
    }
    QString error;
    for (const QString &imagePath : images) {
        if (!removeGeneratedFileSet(imagePath, &error)) {
            showError(error);
            return;
        }
    }
    refreshGeneratedList();
    appendLog(QString(u8"已删除全部生成项"));
}

void MainWindow::setToolView()
{
    ui->imageView->setToolMode(ImageView::ViewMode);
}

void MainWindow::setToolRect()
{
    ui->imageView->setToolMode(ImageView::RectMode);
}

void MainWindow::setToolPolygon()
{
    ui->imageView->setToolMode(ImageView::PolygonMode);
}

void MainWindow::setToolBrush()
{
    ui->imageView->setToolMode(ImageView::BrushMode);
}

void MainWindow::setToolEraser()
{
    ui->imageView->setToolMode(ImageView::EraserMode);
}

void MainWindow::clearMask()
{
    ui->imageView->clearMask();
}

void MainWindow::appendLog(const QString &line)
{
    QString displayLine = line;
    const bool looksLikeUtf8Mojibake =
        displayLine.contains(QChar(0x00C3)) ||
        displayLine.contains(QChar(0x00E5)) ||
        displayLine.contains(QChar(0x00E6)) ||
        displayLine.contains(QChar(0x00E7)) ||
        displayLine.contains(QChar(0x00E8)) ||
        displayLine.contains(QChar(0x00E9));
    if (looksLikeUtf8Mojibake) {
        const QString repaired = QString::fromUtf8(displayLine.toLatin1());
        bool hasCjk = false;
        for (const QChar ch : repaired) {
            if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) {
                hasCjk = true;
                break;
            }
        }
        if (hasCjk) {
            displayLine = repaired;
        }
    }
    ui->logEdit->appendPlainText(displayLine);
}

void MainWindow::refreshProjectViews()
{
    QStringList okItems;
    for (const OkImageRecord &record : m_project.okImages) {
        okItems.append(record.imagePath);
    }
    m_okModel.setStringList(okItems);

    QStringList assetItems;
    QStringList generateTypes;
    generateTypes.append(QString(u8"全部"));
    for (const DefectAssetRecord &asset : m_project.defectAssets) {
        assetItems.append(QStringLiteral("%1 | %2 | area=%3").arg(asset.id, asset.defectType).arg(asset.area));
        if (!generateTypes.contains(asset.defectType)) {
            generateTypes.append(asset.defectType);
        }
    }
    m_assetModel.setStringList(assetItems);
    const QString currentType = ui->generateTypeCombo->currentText();
    ui->generateTypeCombo->clear();
    ui->generateTypeCombo->addItems(generateTypes);
    const int typeIndex = ui->generateTypeCombo->findText(currentType);
    if (typeIndex >= 0) {
        ui->generateTypeCombo->setCurrentIndex(typeIndex);
    }

    QDir defectDir(QDir(m_projectDir).filePath(QStringLiteral("defect_sources")));
    const QFileInfoList defectFiles = defectDir.entryInfoList(QStringList() << QStringLiteral("*.png") << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg") << QStringLiteral("*.bmp"), QDir::Files);
    QStringList defectItems;
    for (const QFileInfo &file : defectFiles) {
        defectItems.append(QDir(m_projectDir).relativeFilePath(file.absoluteFilePath()));
    }
    m_defectModel.setStringList(defectItems);

    refreshGeneratedList();
}

void MainWindow::refreshGeneratedList()
{
    if (m_projectDir.isEmpty()) {
        m_generatedModel.setStringList(QStringList());
        return;
    }
    QDir dir(QDir(m_projectDir).filePath(QStringLiteral("generated")));
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.png"), QDir::Files, QDir::Time);
    QStringList items;
    for (const QFileInfo &file : files) {
        if (!file.completeBaseName().endsWith(QStringLiteral("_mask"))) {
            items.append(file.absoluteFilePath());
        }
    }
    m_generatedModel.setStringList(items);
}

bool MainWindow::ensureProjectLoaded() const
{
    if (m_projectDir.isEmpty()) {
        QMessageBox::warning(const_cast<MainWindow *>(this), QString(u8"项目"), QString(u8"请先创建或打开项目"));
        return false;
    }
    return true;
}

bool MainWindow::saveProject()
{
    QString error;
    if (!m_store.saveProject(m_projectDir, m_project, &error)) {
        showError(error);
        return false;
    }
    return true;
}

QString MainWindow::selectedOkImagePath() const
{
    QModelIndex index = ui->okListView->currentIndex();
    if (!index.isValid()) {
        return QString();
    }
    const QString relative = m_okModel.data(index, Qt::DisplayRole).toString();
    return m_store.absolutePath(m_projectDir, relative);
}

QString MainWindow::selectedGeneratedPath() const
{
    QModelIndex index = ui->generatedListView->currentIndex();
    if (!index.isValid()) {
        return QString();
    }
    return m_generatedModel.data(index, Qt::DisplayRole).toString();
}

QString MainWindow::currentDefectSourceRelativePath() const
{
    QModelIndex index = ui->defectListView->currentIndex();
    if (!index.isValid()) {
        return QString();
    }
    return m_defectModel.data(index, Qt::DisplayRole).toString();
}

QString MainWindow::makeTempMaskPath() const
{
    return QDir(m_projectDir).filePath(QStringLiteral("defect_masks/tmp_%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void MainWindow::setProjectDir(const QString &dir)
{
    m_projectDir = dir;
    QString logError;
    Logger::instance().open(QDir(m_projectDir).filePath(QStringLiteral("logs/app.log")), &logError);
    setWindowTitle(QStringLiteral("DefectDataGenerator - %1").arg(m_project.productName));
}

void MainWindow::showError(const QString &message)
{
    Logger::instance().log(Logger::Error, message);
    QMessageBox::critical(this, QString(u8"错误"), message);
}

void MainWindow::moveReviewedFileSet(const QString &imagePath, const QString &targetSubDir)
{
    QDir projectDir(m_projectDir);
    QDir targetDir(projectDir.filePath(targetSubDir));
    if (!targetDir.exists() && !projectDir.mkpath(targetSubDir)) {
        showError(QString(u8"创建审核目录失败: %1").arg(targetSubDir));
        return;
    }
    QFileInfo imageInfo(imagePath);
    const QString base = imageInfo.completeBaseName();
    const QString maskPath = imageInfo.absolutePath() + QDir::separator() + base + QStringLiteral("_mask.png");
    const QString metaPath = projectDir.filePath(QStringLiteral("metadata/") + base + QStringLiteral(".json"));
    const QString targetImage = targetDir.filePath(imageInfo.fileName());
    const QString targetMask = targetDir.filePath(base + QStringLiteral("_mask.png"));
    const QString targetMeta = targetDir.filePath(base + QStringLiteral(".json"));

    if (!QFileInfo::exists(maskPath)) {
        showError(QString(u8"生成 mask 缺失: %1").arg(maskPath));
        return;
    }
    if (!QFileInfo::exists(metaPath)) {
        showError(QString(u8"生成 metadata 缺失: %1").arg(metaPath));
        return;
    }

    if (!QFile::rename(imagePath, targetImage)) {
        showError(QString(u8"移动图像失败: %1").arg(targetSubDir));
        return;
    }
    if (!QFile::rename(maskPath, targetMask)) {
        showError(QString(u8"移动 mask 失败: %1").arg(targetSubDir));
        return;
    }
    if (!QFile::rename(metaPath, targetMeta)) {
        showError(QString(u8"移动 metadata 失败: %1").arg(targetSubDir));
        return;
    }
    appendLog(QString(u8"已移动生成项到 %1: %2").arg(targetSubDir, base));
}

bool MainWindow::removeFileIfExists(const QString &path, QString *errorMessage)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return true;
    }
    QFile file(path);
    if (!file.remove()) {
        if (errorMessage) {
            *errorMessage = QString(u8"删除文件失败: %1, %2").arg(path, file.errorString());
        }
        return false;
    }
    Logger::instance().log(Logger::Info, QString(u8"已删除文件: %1").arg(path));
    return true;
}

bool MainWindow::removeOkImageAt(int row, QString *errorMessage)
{
    if (row < 0 || row >= m_project.okImages.size()) {
        if (errorMessage) {
            *errorMessage = QString(u8"OK 图像索引无效");
        }
        return false;
    }
    const OkImageRecord record = m_project.okImages.at(row);
    if (!removeFileIfExists(m_store.absolutePath(m_projectDir, record.imagePath), errorMessage)) {
        return false;
    }
    if (!record.allowedMaskPath.isEmpty() &&
        !removeFileIfExists(m_store.absolutePath(m_projectDir, record.allowedMaskPath), errorMessage)) {
        return false;
    }
    m_project.okImages.removeAt(row);
    return true;
}

bool MainWindow::removeAssetAt(int row, QString *errorMessage)
{
    if (row < 0 || row >= m_project.defectAssets.size()) {
        if (errorMessage) {
            *errorMessage = QString(u8"缺陷素材索引无效");
        }
        return false;
    }
    const DefectAssetRecord asset = m_project.defectAssets.at(row);
    if (!removeFileIfExists(m_store.absolutePath(m_projectDir, asset.maskPath), errorMessage)) {
        return false;
    }
    m_project.defectAssets.removeAt(row);
    return true;
}

bool MainWindow::removeDefectSourceByRelativePath(const QString &relativePath, QString *errorMessage)
{
    for (int i = m_project.defectAssets.size() - 1; i >= 0; --i) {
        if (m_project.defectAssets.at(i).imagePath == relativePath) {
            if (!removeAssetAt(i, errorMessage)) {
                return false;
            }
        }
    }
    return removeFileIfExists(m_store.absolutePath(m_projectDir, relativePath), errorMessage);
}

bool MainWindow::removeGeneratedFileSet(const QString &imagePath, QString *errorMessage)
{
    QFileInfo imageInfo(imagePath);
    const QString base = imageInfo.completeBaseName();
    const QString maskPath = imageInfo.absolutePath() + QDir::separator() + base + QStringLiteral("_mask.png");
    const QString metadataPath = QDir(m_projectDir).filePath(QStringLiteral("metadata/") + base + QStringLiteral(".json"));
    if (!removeFileIfExists(imagePath, errorMessage)) {
        return false;
    }
    if (!removeFileIfExists(maskPath, errorMessage)) {
        return false;
    }
    return removeFileIfExists(metadataPath, errorMessage);
}

bool MainWindow::confirmDelete(const QString &message) const
{
    return QMessageBox::question(const_cast<MainWindow *>(this),
                                 QString(u8"确认删除"),
                                 message,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}
