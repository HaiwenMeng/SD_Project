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
    setupConnections();
    connect(&Logger::instance(), &Logger::messageLogged, this, &MainWindow::appendLog);
    appendLog(QStringLiteral("Ready."));
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
    connect(ui->newProjectButton, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(ui->openProjectButton, &QPushButton::clicked, this, &MainWindow::openProject);
    connect(ui->importOkButton, &QPushButton::clicked, this, &MainWindow::importOkImages);
    connect(ui->importDefectButton, &QPushButton::clicked, this, &MainWindow::importDefectImages);
    connect(ui->saveAssetButton, &QPushButton::clicked, this, &MainWindow::saveCurrentMaskAsAsset);
    connect(ui->generateButton, &QPushButton::clicked, this, &MainWindow::generateImages);
    connect(ui->approveButton, &QPushButton::clicked, this, &MainWindow::approveSelectedGenerated);
    connect(ui->rejectButton, &QPushButton::clicked, this, &MainWindow::rejectSelectedGenerated);
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
}

void MainWindow::newProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select project directory"));
    if (dir.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString productName = QInputDialog::getText(this, QStringLiteral("Product"), QStringLiteral("Product name"), QLineEdit::Normal, QStringLiteral("DefaultProduct"), &ok);
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
    appendLog(QStringLiteral("Project created: %1").arg(dir));
}

void MainWindow::openProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Open project directory"));
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
    appendLog(QStringLiteral("Project opened: %1").arg(dir));
}

void MainWindow::importOkImages()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import OK images"), QString(), QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
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
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import defect sources"), QString(), QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
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
    appendLog(QStringLiteral("Imported %1 defect source image(s).").arg(files.size()));
}

void MainWindow::saveCurrentMaskAsAsset()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString sourceRelative = currentDefectSourceRelativePath();
    if (sourceRelative.isEmpty()) {
        showError(QStringLiteral("Select a defect source image first."));
        return;
    }
    const QString defectType = ui->defectTypeEdit->text().trimmed();
    if (defectType.isEmpty()) {
        showError(QStringLiteral("Defect type is empty."));
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
        appendLog(QStringLiteral("Defect asset saved. Type=%1 Area=%2").arg(defectType).arg(area));
    }
}

void MainWindow::generateImages()
{
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString okPath = selectedOkImagePath();
    if (okPath.isEmpty()) {
        showError(QStringLiteral("Select one OK image before generation."));
        return;
    }
    if (m_project.defectAssets.isEmpty()) {
        showError(QStringLiteral("No defect assets available."));
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
        showError(QStringLiteral("Defects max must be greater than or equal to defects min."));
        return;
    }
    if (ui->scaleMaxSpin->value() < ui->scaleMinSpin->value()) {
        showError(QStringLiteral("Scale max must be greater than or equal to scale min."));
        return;
    }
    if (ui->rotationMaxSpin->value() < ui->rotationMinSpin->value()) {
        showError(QStringLiteral("Rotation max must be greater than or equal to rotation min."));
        return;
    }
    if (ui->brightnessMaxSpin->value() < ui->brightnessMinSpin->value()) {
        showError(QStringLiteral("Brightness max must be greater than or equal to brightness min."));
        return;
    }
    if (ui->featherMaxSpin->value() < ui->featherMinSpin->value()) {
        showError(QStringLiteral("Feather max must be greater than or equal to feather min."));
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
        if (selectedType == QStringLiteral("All") || asset.defectType == selectedType) {
            request.defectAssetIds.append(asset.id);
        }
    }
    if (request.defectAssetIds.isEmpty()) {
        showError(QStringLiteral("No defect assets match the selected defect type."));
        return;
    }

    std::unique_ptr<IGenerationBackend> backend = BackendFactory::createBackend(QStringLiteral("OpenCV"));
    if (!backend) {
        showError(QStringLiteral("OpenCV generation backend is unavailable."));
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
    appendLog(QStringLiteral("Generation finished: %1 item(s).").arg(result.items.size()));
}

void MainWindow::approveSelectedGenerated()
{
    const QString path = selectedGeneratedPath();
    if (path.isEmpty()) {
        showError(QStringLiteral("Select a generated image first."));
        return;
    }
    moveReviewedFileSet(path, QStringLiteral("approved"));
    refreshGeneratedList();
}

void MainWindow::rejectSelectedGenerated()
{
    const QString path = selectedGeneratedPath();
    if (path.isEmpty()) {
        showError(QStringLiteral("Select a generated image first."));
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
    const QString outDir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select export directory"));
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
            showError(QStringLiteral("Failed to export image: %1").arg(imageInfo.fileName()));
            return;
        }
        const QString srcMask = approvedDir.filePath(maskName);
        if (!QFile::copy(srcMask, dstMask)) {
            showError(QStringLiteral("Failed to export mask: %1").arg(maskName));
            return;
        }
        const QString srcMeta = approvedDir.filePath(metaName);
        if (QFileInfo::exists(srcMeta) && !QFile::copy(srcMeta, dstMeta)) {
            showError(QStringLiteral("Failed to export metadata: %1").arg(metaName));
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
        showError(QStringLiteral("Failed to write dataset.json: %1").arg(datasetFile.errorString()));
        return;
    }
    datasetFile.write(QJsonDocument(dataset).toJson(QJsonDocument::Indented));
    appendLog(QStringLiteral("Exported approved dataset: %1 item(s).").arg(items.size()));
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
        const QString maskPath = QFileInfo(path).absolutePath() + QDir::separator() + QFileInfo(path).completeBaseName() + QStringLiteral("_mask.png");
        QImage mask(maskPath);
        if (!mask.isNull()) {
            ui->imageView->setMask(mask);
        }
    }
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
    ui->logEdit->appendPlainText(line);
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
    generateTypes.append(QStringLiteral("All"));
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
        QMessageBox::warning(const_cast<MainWindow *>(this), QStringLiteral("No project"), QStringLiteral("Create or open a project first."));
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
    QMessageBox::critical(this, QStringLiteral("Error"), message);
}

void MainWindow::moveReviewedFileSet(const QString &imagePath, const QString &targetSubDir)
{
    QDir projectDir(m_projectDir);
    QDir targetDir(projectDir.filePath(targetSubDir));
    if (!targetDir.exists() && !projectDir.mkpath(targetSubDir)) {
        showError(QStringLiteral("Failed to create review directory: %1").arg(targetSubDir));
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
        showError(QStringLiteral("Generated mask is missing: %1").arg(maskPath));
        return;
    }
    if (!QFileInfo::exists(metaPath)) {
        showError(QStringLiteral("Generated metadata is missing: %1").arg(metaPath));
        return;
    }

    if (!QFile::rename(imagePath, targetImage)) {
        showError(QStringLiteral("Failed to move image to %1").arg(targetSubDir));
        return;
    }
    if (!QFile::rename(maskPath, targetMask)) {
        showError(QStringLiteral("Failed to move mask to %1").arg(targetSubDir));
        return;
    }
    if (!QFile::rename(metaPath, targetMeta)) {
        showError(QStringLiteral("Failed to move metadata to %1").arg(targetSubDir));
        return;
    }
    appendLog(QStringLiteral("Moved generated item to %1: %2").arg(targetSubDir, base));
}
