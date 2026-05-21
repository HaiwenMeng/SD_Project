#ifndef WIDGET_H
#define WIDGET_H

#include "StableDiffusionEngine.h"

#include <QThread>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void browseTextModel();
    void browseInpaintModel();
    void browseInitImage();
    void browseMaskImage();
    void browseOutputDir();
    void generateTextImage();
    void generateInpaintImage();
    void appendLog(const QString &message);
    void updateProgress(const QString &message);
    void handleGenerationFinished(const SdGenerateResult &result);

private:
    SdGenerateRequest makeBaseRequest(const QString &modelPath, bool inpaint) const;
    QString defaultCkptDir() const;
    QString defaultOutputDir() const;
    QString makeOutputPath(const QString &prefix) const;
    void runRequest(const SdGenerateRequest &request);
    void setBusy(bool busy);
    void showOutputImage(const QString &path);
    void showError(const QString &message);

    Ui::Widget *ui;
    StableDiffusionEngine *m_engine = nullptr;
    QThread m_engineThread;
    bool m_generationRunning = false;
};
#endif // WIDGET_H
