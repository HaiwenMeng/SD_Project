#include "StableDiffusionEngine.h"

#include "stable-diffusion.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include <cstdlib>
#include <cstring>

namespace {

class OwnedSdImage
{
public:
    OwnedSdImage() = default;
    ~OwnedSdImage()
    {
        reset();
    }

    OwnedSdImage(const OwnedSdImage &) = delete;
    OwnedSdImage &operator=(const OwnedSdImage &) = delete;

    sd_image_t image() const
    {
        return m_image;
    }

    void reset(sd_image_t image = {0, 0, 0, nullptr})
    {
        if (m_image.data) {
            std::free(m_image.data);
        }
        m_image = image;
    }

private:
    sd_image_t m_image = {0, 0, 0, nullptr};
};

QString normalizePath(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool ensureParentDir(const QString &filePath, QString *errorMessage)
{
    const QFileInfo fileInfo(filePath);
    if (fileInfo.fileName().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Output file name is empty.");
        }
        return false;
    }

    QDir dir(fileInfo.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create output directory: %1").arg(dir.absolutePath());
        }
        return false;
    }
    return true;
}

bool allocImage(sd_image_t *image, int width, int height, int channel, QString *errorMessage)
{
    const qsizetype byteCount = qsizetype(width) * qsizetype(height) * qsizetype(channel);
    if (byteCount <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid image allocation size.");
        }
        return false;
    }

    image->data = static_cast<uint8_t *>(std::malloc(size_t(byteCount)));
    if (!image->data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to allocate image buffer.");
        }
        return false;
    }
    image->width = uint32_t(width);
    image->height = uint32_t(height);
    image->channel = uint32_t(channel);
    return true;
}

bool loadRgbImage(const QString &path, int expectedWidth, int expectedHeight, OwnedSdImage *outImage, QString *errorMessage)
{
    QImage src(path);
    if (src.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to load image: %1").arg(path);
        }
        return false;
    }
    if (src.width() != expectedWidth || src.height() != expectedHeight) {
        src = src.scaled(expectedWidth, expectedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage rgb = src.convertToFormat(QImage::Format_RGB888);
    sd_image_t image = {0, 0, 0, nullptr};
    if (!allocImage(&image, rgb.width(), rgb.height(), 3, errorMessage)) {
        return false;
    }

    for (int y = 0; y < rgb.height(); ++y) {
        const uchar *line = rgb.constScanLine(y);
        std::memcpy(image.data + size_t(y) * size_t(rgb.width()) * 3, line, size_t(rgb.width()) * 3);
    }

    outImage->reset(image);
    return true;
}

bool loadMaskImage(const QString &path, int expectedWidth, int expectedHeight, OwnedSdImage *outImage, QString *errorMessage)
{
    QImage src(path);
    if (src.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to load mask image: %1").arg(path);
        }
        return false;
    }

    if (src.width() != expectedWidth || src.height() != expectedHeight) {
        src = src.scaled(expectedWidth, expectedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    sd_image_t image = {0, 0, 0, nullptr};
    if (!allocImage(&image, gray.width(), gray.height(), 1, errorMessage)) {
        return false;
    }

    int whiteCount = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            const uint8_t value = line[x] >= 128 ? uint8_t(255) : uint8_t(0);
            image.data[size_t(y) * size_t(gray.width()) + size_t(x)] = value;
            if (value == 255) {
                ++whiteCount;
            }
        }
    }

    const int pixelCount = gray.width() * gray.height();
    if (whiteCount <= 0) {
        std::free(image.data);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask has no white repair area after thresholding: %1").arg(path);
        }
        return false;
    }
    if (whiteCount >= pixelCount * 8 / 10) {
        std::free(image.data);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask repair area is too large after thresholding. Use a black background with white local repair area: %1").arg(path);
        }
        return false;
    }

    outImage->reset(image);
    return true;
}

bool saveSdImage(const sd_image_t &image, const QString &path, QString *errorMessage)
{
    if (!image.data || image.width == 0 || image.height == 0 || image.channel == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Generated image is empty.");
        }
        return false;
    }

    QImage out;
    if (image.channel == 3) {
        out = QImage(image.data,
                     int(image.width),
                     int(image.height),
                     int(image.width * image.channel),
                     QImage::Format_RGB888).copy();
    } else if (image.channel == 4) {
        out = QImage(image.data,
                     int(image.width),
                     int(image.height),
                     int(image.width * image.channel),
                     QImage::Format_RGBA8888).copy();
    } else if (image.channel == 1) {
        out = QImage(image.data,
                     int(image.width),
                     int(image.height),
                     int(image.width),
                     QImage::Format_Grayscale8).copy();
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported generated image channel count: %1").arg(image.channel);
        }
        return false;
    }

    if (out.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to convert generated image.");
        }
        return false;
    }
    if (!out.save(path, "PNG")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to save output image: %1").arg(path);
        }
        return false;
    }
    return true;
}

void freeGeneratedImages(sd_image_t *images, int count)
{
    if (!images) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        std::free(images[i].data);
        images[i].data = nullptr;
    }
    std::free(images);
}

QString levelName(int level)
{
    switch (level) {
    case SD_LOG_DEBUG:
        return QStringLiteral("DEBUG");
    case SD_LOG_INFO:
        return QStringLiteral("INFO");
    case SD_LOG_WARN:
        return QStringLiteral("WARN");
    case SD_LOG_ERROR:
        return QStringLiteral("ERROR");
    default:
        return QStringLiteral("UNKNOWN");
    }
}

} // namespace

StableDiffusionEngine *StableDiffusionEngine::s_callbackTarget = nullptr;

StableDiffusionEngine::StableDiffusionEngine(QObject *parent)
    : QObject(parent)
{
}

StableDiffusionEngine::~StableDiffusionEngine()
{
    releaseContext();
}

SdGenerateResult StableDiffusionEngine::generateTextToImage(const SdGenerateRequest &request)
{
    SdGenerateRequest local = request;
    local.inpaint = false;
    return generate(local);
}

SdGenerateResult StableDiffusionEngine::generateInpaint(const SdGenerateRequest &request)
{
    SdGenerateRequest local = request;
    local.inpaint = true;
    return generate(local);
}

QString StableDiffusionEngine::systemInfo() const
{
    const char *info = sd_get_system_info();
    return info ? QString::fromUtf8(info) : QStringLiteral("stable-diffusion.cpp system info unavailable.");
}

void StableDiffusionEngine::clearContext()
{
    releaseContext();
}

SdGenerateResult StableDiffusionEngine::generate(const SdGenerateRequest &request)
{
    SdGenerateResult result;
    result.outputPath = request.outputPath;

    QString error;
    if (!validateRequest(request, &error)) {
        result.errorMessage = error;
        log(QStringLiteral("ERROR: %1").arg(error));
        return result;
    }
    if (!ensureParentDir(request.outputPath, &error)) {
        result.errorMessage = error;
        log(QStringLiteral("ERROR: %1").arg(error));
        return result;
    }
    const bool vaeDecodeOnly = !request.inpaint;
    if (!ensureContext(request.modelPath, vaeDecodeOnly, &error)) {
        result.errorMessage = error;
        log(QStringLiteral("ERROR: %1").arg(error));
        return result;
    }

    OwnedSdImage initImage;
    OwnedSdImage maskImage;
    if (request.inpaint) {
        if (!loadRgbImage(request.initImagePath, request.width, request.height, &initImage, &error)) {
            result.errorMessage = error;
            log(QStringLiteral("ERROR: %1").arg(error));
            return result;
        }
        if (!loadMaskImage(request.maskImagePath, request.width, request.height, &maskImage, &error)) {
            result.errorMessage = error;
            log(QStringLiteral("ERROR: %1").arg(error));
            return result;
        }
    }

    QByteArray promptUtf8 = request.prompt.toUtf8();
    QByteArray negativeUtf8 = request.negativePrompt.toUtf8();

    sd_img_gen_params_t params;
    sd_img_gen_params_init(&params);
    params.prompt = promptUtf8.constData();
    params.negative_prompt = negativeUtf8.constData();
    params.width = request.width;
    params.height = request.height;
    params.sample_params.sample_steps = request.steps;
    params.sample_params.guidance.txt_cfg = request.cfgScale;
    params.sample_params.guidance.img_cfg = request.cfgScale;
    params.sample_params.sample_method = sd_get_default_sample_method(m_context);
    params.sample_params.scheduler = sd_get_default_scheduler(m_context, params.sample_params.sample_method);
    params.seed = request.seed;
    params.batch_count = 1;
    params.strength = request.strength;
    params.vae_tiling_params.enabled = true;
    params.vae_tiling_params.tile_size_x = 32;
    params.vae_tiling_params.tile_size_y = 32;

    if (request.inpaint) {
        params.init_image = initImage.image();
        params.mask_image = maskImage.image();
    }

    log(QStringLiteral("Start generation. model=%1 size=%2x%3 steps=%4 seed=%5")
            .arg(request.modelPath)
            .arg(request.width)
            .arg(request.height)
            .arg(request.steps)
            .arg(request.seed));

    s_callbackTarget = this;
    sd_set_log_callback(&StableDiffusionEngine::sdLogCallback, nullptr);
    sd_set_progress_callback(&StableDiffusionEngine::sdProgressCallback, nullptr);

    sd_image_t *images = generate_image(m_context, &params);
    s_callbackTarget = nullptr;

    if (!images) {
        result.errorMessage = QStringLiteral("stable-diffusion.cpp generate_image returned null.");
        log(QStringLiteral("ERROR: %1").arg(result.errorMessage));
        return result;
    }

    if (!saveSdImage(images[0], request.outputPath, &error)) {
        freeGeneratedImages(images, 1);
        result.errorMessage = error;
        log(QStringLiteral("ERROR: %1").arg(error));
        return result;
    }

    freeGeneratedImages(images, 1);
    result.success = true;
    log(QStringLiteral("Output saved: %1").arg(request.outputPath));
    return result;
}

bool StableDiffusionEngine::validateRequest(const SdGenerateRequest &request, QString *errorMessage) const
{
    if (request.modelPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Model path is empty.");
        }
        return false;
    }
    if (!QFileInfo::exists(request.modelPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Model file does not exist: %1").arg(request.modelPath);
        }
        return false;
    }
    if (request.prompt.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Prompt is empty.");
        }
        return false;
    }
    if (request.width <= 0 || request.height <= 0 || request.width % 64 != 0 || request.height % 64 != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Width and height must be positive multiples of 64.");
        }
        return false;
    }
    if (request.steps <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Steps must be greater than zero.");
        }
        return false;
    }
    if (request.outputPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Output path is empty.");
        }
        return false;
    }
    if (request.inpaint) {
        if (!QFileInfo::exists(request.initImagePath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Init image file does not exist: %1").arg(request.initImagePath);
            }
            return false;
        }
        if (!QFileInfo::exists(request.maskImagePath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Mask image file does not exist: %1").arg(request.maskImagePath);
            }
            return false;
        }
    }
    return true;
}

bool StableDiffusionEngine::ensureContext(const QString &modelPath, bool vaeDecodeOnly, QString *errorMessage)
{
    const QString normalized = normalizePath(modelPath);
    if (m_context && m_contextModelPath == normalized && m_contextVaeDecodeOnly == vaeDecodeOnly) {
        return true;
    }

    releaseContext();

    QByteArray modelPathUtf8 = normalized.toUtf8();
    sd_ctx_params_t params;
    sd_ctx_params_init(&params);
    params.model_path = modelPathUtf8.constData();
    params.n_threads = sd_get_num_physical_cores();
    params.rng_type = CUDA_RNG;
    params.sampler_rng_type = CUDA_RNG;
    params.enable_mmap = true;
    params.vae_decode_only = vaeDecodeOnly;
    params.free_params_immediately = true;

    log(QStringLiteral("Loading model context: %1, vae_decode_only=%2")
            .arg(normalized)
            .arg(vaeDecodeOnly ? QStringLiteral("true") : QStringLiteral("false")));
    m_context = new_sd_ctx(&params);
    if (!m_context) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to initialize stable-diffusion.cpp context for model: %1").arg(normalized);
        }
        return false;
    }
    if (!sd_ctx_supports_image_generation(m_context)) {
        releaseContext();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Loaded model does not support image generation: %1").arg(normalized);
        }
        return false;
    }

    m_contextModelPath = normalized;
    m_contextVaeDecodeOnly = vaeDecodeOnly;
    log(QStringLiteral("Model context loaded."));
    return true;
}

void StableDiffusionEngine::releaseContext()
{
    if (m_context) {
        free_sd_ctx(m_context);
        m_context = nullptr;
    }
    m_contextModelPath.clear();
    m_contextVaeDecodeOnly = true;
}

void StableDiffusionEngine::log(const QString &message)
{
    emit logMessage(message);
}

void StableDiffusionEngine::sdLogCallback(enum sd_log_level_t level, const char *text, void *data)
{
    Q_UNUSED(data);
    if (!s_callbackTarget || !text) {
        return;
    }
    emit s_callbackTarget->logMessage(QStringLiteral("[%1] %2").arg(levelName(level), QString::fromUtf8(text)));
}

void StableDiffusionEngine::sdProgressCallback(int step, int steps, float time, void *data)
{
    Q_UNUSED(data);
    if (!s_callbackTarget) {
        return;
    }
    emit s_callbackTarget->progressMessage(QStringLiteral("Step %1/%2, %3 ms")
                                               .arg(step)
                                               .arg(steps)
                                               .arg(double(time), 0, 'f', 2));
}
