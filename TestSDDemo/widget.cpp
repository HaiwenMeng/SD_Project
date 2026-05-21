#include "widget.h"
#include "ui_widget.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMetaObject>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QScrollBar>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    setWindowTitle(QString(u8"Stable Diffusion 测试 - stable-diffusion.cpp"));

    ui->textModelEdit->setText(QDir(defaultCkptDir()).filePath(QStringLiteral("v1-5-pruned-emaonly.safetensors")));
    ui->inpaintModelEdit->setText(QDir(defaultCkptDir()).filePath(QStringLiteral("sd-v1-5-inpainting.safetensors")));
    ui->outputDirEdit->setText(defaultOutputDir());
    ui->promptEdit->setPlainText(QStringLiteral("industrial surface defect, realistic macro photo, sharp edge detail"));
    ui->negativePromptEdit->setPlainText(QStringLiteral("low quality, blurry, watermark, text"));
    ui->previewLabel->setText(QString(u8"暂无输出"));

    m_engine = new StableDiffusionEngine();
    const QString systemInfo = m_engine->systemInfo();
    m_engine->moveToThread(&m_engineThread);
    connect(&m_engineThread, &QThread::finished, m_engine, &QObject::deleteLater);
    connect(m_engine, &StableDiffusionEngine::logMessage, this, &Widget::appendLog, Qt::QueuedConnection);
    connect(m_engine, &StableDiffusionEngine::progressMessage, this, &Widget::updateProgress, Qt::QueuedConnection);
    m_engineThread.start();

    connect(ui->browseTextModelButton, &QPushButton::clicked, this, &Widget::browseTextModel);
    connect(ui->browseInpaintModelButton, &QPushButton::clicked, this, &Widget::browseInpaintModel);
    connect(ui->browseInitButton, &QPushButton::clicked, this, &Widget::browseInitImage);
    connect(ui->browseMaskButton, &QPushButton::clicked, this, &Widget::browseMaskImage);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &Widget::browseOutputDir);
    connect(ui->generateTextButton, &QPushButton::clicked, this, &Widget::generateTextImage);
    connect(ui->generateInpaintButton, &QPushButton::clicked, this, &Widget::generateInpaintImage);

    appendLog(QString(u8"TestSDDemo 已就绪."));
    appendLog(QString(u8"已链接 stable-diffusion.cpp CUDA 版本."));
    appendLog(systemInfo);
}

Widget::~Widget()
{
    m_engineThread.quit();
    m_engineThread.wait();
    delete ui;
}

void Widget::browseTextModel()
{
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QString(u8"选择 Txt2Img 模型"),
                                                      defaultCkptDir(),
                                                      QString(u8"模型 (*.safetensors *.ckpt *.gguf);;所有文件 (*.*)"));
    if (!file.isEmpty()) {
        ui->textModelEdit->setText(file);
    }
}

void Widget::browseInpaintModel()
{
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QString(u8"选择 Inpaint 模型"),
                                                      defaultCkptDir(),
                                                      QString(u8"模型 (*.safetensors *.ckpt *.gguf);;所有文件 (*.*)"));
    if (!file.isEmpty()) {
        ui->inpaintModelEdit->setText(file);
    }
}

void Widget::browseInitImage()
{
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QString(u8"选择原图"),
                                                      QString(),
                                                      QString(u8"图像 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
    if (!file.isEmpty()) {
        ui->initImageEdit->setText(file);
    }
}

void Widget::browseMaskImage()
{
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QString(u8"选择 Mask 图"),
                                                      QString(),
                                                      QString(u8"图像 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
    if (!file.isEmpty()) {
        ui->maskImageEdit->setText(file);
    }
}

void Widget::browseOutputDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QString(u8"选择输出目录"),
                                                          ui->outputDirEdit->text());
    if (!dir.isEmpty()) {
        ui->outputDirEdit->setText(dir);
    }
}

void Widget::generateTextImage()
{
    SdGenerateRequest request = makeBaseRequest(ui->textModelEdit->text(), false);
    request.outputPath = makeOutputPath(QStringLiteral("txt2img"));
    runRequest(request);
}

void Widget::generateInpaintImage()
{
    SdGenerateRequest request = makeBaseRequest(ui->inpaintModelEdit->text(), true);
    request.initImagePath = ui->initImageEdit->text().trimmed();
    request.maskImagePath = ui->maskImageEdit->text().trimmed();
    request.outputPath = makeOutputPath(QStringLiteral("inpaint"));
    runRequest(request);
}

void Widget::appendLog(const QString &message)
{
    ui->logEdit->append(QStringLiteral("[%1] %2")
                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                            .arg(message));
    QScrollBar *bar = ui->logEdit->verticalScrollBar();
    if (bar) {
        bar->setValue(bar->maximum());
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Widget::updateProgress(const QString &message)
{
    ui->progressLabel->setText(message);
    appendLog(message);
}

SdGenerateRequest Widget::makeBaseRequest(const QString &modelPath, bool inpaint) const
{
    SdGenerateRequest request;
    request.modelPath = modelPath.trimmed();
    request.prompt = ui->promptEdit->toPlainText().trimmed();
    request.negativePrompt = ui->negativePromptEdit->toPlainText().trimmed();
    request.width = ui->widthSpin->value();
    request.height = ui->heightSpin->value();
    request.steps = ui->stepsSpin->value();
    request.seed = ui->seedSpin->value();
    request.cfgScale = float(ui->cfgSpin->value());
    request.strength = float(ui->strengthSpin->value());
    request.inpaint = inpaint;
    return request;
}

QString Widget::defaultCkptDir() const
{
    return QStringLiteral("F:/SD_Project/ckpt");
}

QString Widget::defaultOutputDir() const
{
    return QStringLiteral("F:/SD_Project/TestSDDemo/outputs");
}

QString Widget::makeOutputPath(const QString &prefix) const
{
    QString dirPath = ui->outputDirEdit->text().trimmed();
    if (dirPath.isEmpty()) {
        dirPath = defaultOutputDir();
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    return QDir(dirPath).filePath(QStringLiteral("%1_%2.png").arg(prefix, stamp));
}

void Widget::runRequest(const SdGenerateRequest &request)
{
    if (m_generationRunning) {
        appendLog(QString(u8"已有生成任务正在执行."));
        return;
    }

    setBusy(true);
    m_generationRunning = true;
    ui->progressLabel->setText(QString(u8"运行中..."));
    appendLog(QString(u8"任务已加入后台: %1").arg(request.inpaint ? QStringLiteral("Inpaint") : QStringLiteral("Txt2Img")));

    StableDiffusionEngine *engine = m_engine;
    QPointer<Widget> guard(this);
    QMetaObject::invokeMethod(engine, [engine, request, guard]() {
        const SdGenerateResult result = request.inpaint ? engine->generateInpaint(request)
                                                        : engine->generateTextToImage(request);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, result]() {
            if (!guard) {
                return;
            }
            guard->handleGenerationFinished(result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void Widget::handleGenerationFinished(const SdGenerateResult &result)
{
    m_generationRunning = false;
    setBusy(false);
    if (!result.success) {
        ui->progressLabel->setText(QString(u8"失败"));
        showError(result.errorMessage);
        return;
    }

    ui->progressLabel->setText(QString(u8"完成"));
    showOutputImage(result.outputPath);
}

void Widget::setBusy(bool busy)
{
    ui->generateTextButton->setEnabled(!busy);
    ui->generateInpaintButton->setEnabled(!busy);
    ui->browseTextModelButton->setEnabled(!busy);
    ui->browseInpaintModelButton->setEnabled(!busy);
    ui->browseInitButton->setEnabled(!busy);
    ui->browseMaskButton->setEnabled(!busy);
    ui->browseOutputButton->setEnabled(!busy);
    if (busy) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void Widget::showOutputImage(const QString &path)
{
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        showError(QString(u8"生成文件无法预览: %1").arg(path));
        return;
    }
    ui->previewLabel->setPixmap(pixmap.scaled(ui->previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    appendLog(QString(u8"预览已加载: %1").arg(path));
}

void Widget::showError(const QString &message)
{
    appendLog(QString(u8"错误: %1").arg(message));
    QMessageBox::critical(this, QString(u8"错误"), message);
}

