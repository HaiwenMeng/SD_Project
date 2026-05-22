#ifndef STABLEDIFFUSIONENGINE_H
#define STABLEDIFFUSIONENGINE_H

#include <QObject>
#include <QString>

#include "stable-diffusion.h"

struct SdGenerateRequest
{
    QString modelPath;
    QString prompt;
    QString negativePrompt;
    QString initImagePath;
    QString maskImagePath;
    QString outputPath;
    int width = 512;
    int height = 512;
    int steps = 20;
    qint64 seed = 42;
    float cfgScale = 7.0f;
    float strength = 0.25f;
    int maskEdgeWidth = 10;
    int maskBlurRadius = 12;
    bool inpaintOnlyMasked = true;
    int maskedContentMode = 0;
    bool inpaint = false;
};

struct SdGenerateResult
{
    bool success = false;
    QString outputPath;
    QString errorMessage;
};

class StableDiffusionEngine : public QObject
{
    Q_OBJECT

public:
    explicit StableDiffusionEngine(QObject *parent = nullptr);
    ~StableDiffusionEngine();

    SdGenerateResult generateTextToImage(const SdGenerateRequest &request);
    SdGenerateResult generateInpaint(const SdGenerateRequest &request);
    QString systemInfo() const;
    void clearContext();

signals:
    void logMessage(const QString &message);
    void progressMessage(const QString &message);

private:
    SdGenerateResult generate(const SdGenerateRequest &request);
    bool validateRequest(const SdGenerateRequest &request, QString *errorMessage) const;
    bool ensureContext(const QString &modelPath, bool vaeDecodeOnly, QString *errorMessage);
    void releaseContext();
    void log(const QString &message);

    static void sdLogCallback(enum sd_log_level_t level, const char *text, void *data);
    static void sdProgressCallback(int step, int steps, float time, void *data);

private:
    sd_ctx_t *m_context = nullptr;
    QString m_contextModelPath;
    bool m_contextVaeDecodeOnly = true;

    static StableDiffusionEngine *s_callbackTarget;
};

#endif // STABLEDIFFUSIONENGINE_H
