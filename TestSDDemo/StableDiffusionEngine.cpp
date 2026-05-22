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
            *errorMessage = QString(u8"输出文件名为空.");
        }
        return false;
    }

    QDir dir(fileInfo.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QString(u8"创建输出目录失败: %1").arg(dir.absolutePath());
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
            *errorMessage = QString(u8"图像缓存大小无效.");
        }
        return false;
    }

    image->data = static_cast<uint8_t *>(std::malloc(size_t(byteCount)));
    if (!image->data) {
        if (errorMessage) {
            *errorMessage = QString(u8"分配图像缓存失败.");
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
            *errorMessage = QString(u8"加载图像失败: %1").arg(path);
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
            *errorMessage = QString(u8"加载 Mask 图失败: %1").arg(path);
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
            *errorMessage = QString(u8"Mask 二值化后没有白色修复区域: %1").arg(path);
        }
        return false;
    }
    if (whiteCount >= pixelCount * 8 / 10) {
        std::free(image.data);
        if (errorMessage) {
            *errorMessage = QString(u8"Mask 二值化后修复区域过大. 请使用黑色背景和白色局部修复区域: %1").arg(path);
        }
        return false;
    }

    outImage->reset(image);
    return true;
}

QImage loadScaledRgbQImage(const QString &path, int expectedWidth, int expectedHeight, QString *errorMessage)
{
    QImage src(path);
    if (src.isNull()) {
        if (errorMessage) {
            *errorMessage = QString(u8"加载图像失败: %1").arg(path);
        }
        return QImage();
    }
    if (src.width() != expectedWidth || src.height() != expectedHeight) {
        src = src.scaled(expectedWidth, expectedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return src.convertToFormat(QImage::Format_RGB888);
}

bool qImageToSdImage(const QImage &src, int channel, OwnedSdImage *outImage, QString *errorMessage)
{
    QImage converted;
    if (channel == 3) {
        converted = src.convertToFormat(QImage::Format_RGB888);
    } else if (channel == 1) {
        converted = src.convertToFormat(QImage::Format_Grayscale8);
    } else {
        if (errorMessage) {
            *errorMessage = QString(u8"图像通道数不支持: %1").arg(channel);
        }
        return false;
    }

    sd_image_t image = {0, 0, 0, nullptr};
    if (!allocImage(&image, converted.width(), converted.height(), channel, errorMessage)) {
        return false;
    }

    const size_t rowBytes = size_t(converted.width()) * size_t(channel);
    for (int y = 0; y < converted.height(); ++y) {
        std::memcpy(image.data + size_t(y) * rowBytes, converted.constScanLine(y), rowBytes);
    }

    outImage->reset(image);
    return true;
}

QImage sdImageToQImage(const sd_image_t &image, QString *errorMessage)
{
    if (!image.data || image.width == 0 || image.height == 0 || image.channel == 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"生成图像为空.");
        }
        return QImage();
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
                     QImage::Format_RGBA8888).copy().convertToFormat(QImage::Format_RGB888);
    } else if (image.channel == 1) {
        out = QImage(image.data,
                     int(image.width),
                     int(image.height),
                     int(image.width),
                     QImage::Format_Grayscale8).copy().convertToFormat(QImage::Format_RGB888);
    } else {
        if (errorMessage) {
            *errorMessage = QString(u8"生成图像通道数不支持: %1").arg(image.channel);
        }
        return QImage();
    }

    if (out.isNull() && errorMessage) {
        *errorMessage = QString(u8"转换生成图像失败.");
    }
    return out;
}

QImage makeEdgeBandMask(const QImage &binaryMask, int radius)
{
    const QImage binary = binaryMask.convertToFormat(QImage::Format_Grayscale8);
    if (radius <= 0) {
        return binary;
    }

    const int width = binary.width();
    const int height = binary.height();
    std::vector<int> integral(size_t(width + 1) * size_t(height + 1), 0);

    for (int y = 0; y < height; ++y) {
        int rowSum = 0;
        const uchar *line = binary.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            rowSum += line[x] > 0 ? 1 : 0;
            integral[size_t(y + 1) * size_t(width + 1) + size_t(x + 1)] =
                integral[size_t(y) * size_t(width + 1) + size_t(x + 1)] + rowSum;
        }
    }

    QImage edge(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar *dst = edge.scanLine(y);
        const int y0 = std::max(0, y - radius);
        const int y1 = std::min(height - 1, y + radius);
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(width - 1, x + radius);
            const int count = integral[size_t(y1 + 1) * size_t(width + 1) + size_t(x1 + 1)]
                            - integral[size_t(y0) * size_t(width + 1) + size_t(x1 + 1)]
                            - integral[size_t(y1 + 1) * size_t(width + 1) + size_t(x0)]
                            + integral[size_t(y0) * size_t(width + 1) + size_t(x0)];
            const int area = (x1 - x0 + 1) * (y1 - y0 + 1);
            dst[x] = (count > 0 && count < area) ? uchar(255) : uchar(0);
        }
    }
    return edge;
}

QImage gaussianBlurMask(const QImage &mask, int radius)
{
    const QImage gray = mask.convertToFormat(QImage::Format_Grayscale8);
    if (radius <= 0) {
        return gray;
    }

    const int width = gray.width();
    const int height = gray.height();
    const double sigma = std::max(0.1, double(radius) / 3.0);
    std::vector<double> kernel(size_t(radius * 2 + 1), 0.0);
    double kernelSum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double value = std::exp(-(double(i * i)) / (2.0 * sigma * sigma));
        kernel[size_t(i + radius)] = value;
        kernelSum += value;
    }
    for (double &value : kernel) {
        value /= kernelSum;
    }

    std::vector<float> temp(size_t(width) * size_t(height), 0.0f);
    for (int y = 0; y < height; ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            double accum = 0.0;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::min(width - 1, std::max(0, x + k));
                accum += double(line[sx]) * kernel[size_t(k + radius)];
            }
            temp[size_t(y) * size_t(width) + size_t(x)] = float(accum);
        }
    }

    QImage out(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < width; ++x) {
            double accum = 0.0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::min(height - 1, std::max(0, y + k));
                accum += double(temp[size_t(sy) * size_t(width) + size_t(x)]) * kernel[size_t(k + radius)];
            }
            dst[x] = uchar(std::min(255, std::max(0, int(std::round(accum)))));
        }
    }
    return out;
}

QImage buildProcessedMask(const QString &path,
                          int expectedWidth,
                          int expectedHeight,
                          int edgeWidth,
                          int blurRadius,
                          QString *errorMessage)
{
    QImage src(path);
    if (src.isNull()) {
        if (errorMessage) {
            *errorMessage = QString(u8"加载 Mask 图失败: %1").arg(path);
        }
        return QImage();
    }
    if (src.width() != expectedWidth || src.height() != expectedHeight) {
        src = src.scaled(expectedWidth, expectedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    QImage binary(gray.width(), gray.height(), QImage::Format_Grayscale8);
    int originalWhiteCount = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *srcLine = gray.constScanLine(y);
        uchar *dstLine = binary.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            const uchar value = srcLine[x] >= 128 ? uchar(255) : uchar(0);
            dstLine[x] = value;
            if (value == 255) {
                ++originalWhiteCount;
            }
        }
    }

    if (originalWhiteCount <= 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"Mask 二值化后没有白色修复区域: %1").arg(path);
        }
        return QImage();
    }

    QImage processed = makeEdgeBandMask(binary, edgeWidth);
    int processedWhiteCount = 0;
    for (int y = 0; y < processed.height(); ++y) {
        const uchar *line = processed.constScanLine(y);
        for (int x = 0; x < processed.width(); ++x) {
            if (line[x] > 0) {
                ++processedWhiteCount;
            }
        }
    }

    const int pixelCount = processed.width() * processed.height();
    if (processedWhiteCount <= 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"Mask 边缘环为空. 请增大 Mask 边缘宽度或检查 Mask 图: %1").arg(path);
        }
        return QImage();
    }
    if (processedWhiteCount >= pixelCount * 8 / 10) {
        if (errorMessage) {
            *errorMessage = QString(u8"Mask 修复区域过大. 请使用黑色背景和白色局部修复区域: %1").arg(path);
        }
        return QImage();
    }

    return gaussianBlurMask(processed, blurRadius);
}

QRect maskBoundingRect(const QImage &mask)
{
    const QImage gray = mask.convertToFormat(QImage::Format_Grayscale8);
    int minX = gray.width();
    int minY = gray.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            if (line[x] > 0) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        return QRect();
    }
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QRect expandedAspectRect(QRect rect, const QSize &bounds, int targetWidth, int targetHeight, int padding)
{
    rect = rect.adjusted(-padding, -padding, padding, padding);
    rect = rect.intersected(QRect(QPoint(0, 0), bounds));
    if (rect.isEmpty()) {
        return QRect(QPoint(0, 0), bounds);
    }

    const double targetAspect = double(targetWidth) / double(targetHeight);
    int newWidth = rect.width();
    int newHeight = rect.height();
    if (double(newWidth) / double(newHeight) < targetAspect) {
        newWidth = int(std::ceil(double(newHeight) * targetAspect));
    } else {
        newHeight = int(std::ceil(double(newWidth) / targetAspect));
    }
    newWidth = std::min(newWidth, bounds.width());
    newHeight = std::min(newHeight, bounds.height());

    int cx = rect.center().x();
    int cy = rect.center().y();
    int x = cx - newWidth / 2;
    int y = cy - newHeight / 2;
    x = std::min(std::max(0, x), bounds.width() - newWidth);
    y = std::min(std::max(0, y), bounds.height() - newHeight);
    return QRect(x, y, newWidth, newHeight);
}

QImage composeWithMask(const QImage &baseImage,
                       const QImage &generatedImage,
                       const QImage &alphaMask,
                       const QRect &targetRect,
                       QString *errorMessage)
{
    if (baseImage.isNull() || generatedImage.isNull() || alphaMask.isNull() || targetRect.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString(u8"输出融合输入为空.");
        }
        return QImage();
    }

    QImage result = baseImage.convertToFormat(QImage::Format_RGB888);
    const QImage generated = generatedImage.scaled(targetRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    const QImage alpha = alphaMask.convertToFormat(QImage::Format_Grayscale8);

    for (int y = 0; y < targetRect.height(); ++y) {
        uchar *dstLine = result.scanLine(targetRect.y() + y) + targetRect.x() * 3;
        const uchar *genLine = generated.constScanLine(y);
        const uchar *alphaLine = alpha.constScanLine(targetRect.y() + y) + targetRect.x();
        for (int x = 0; x < targetRect.width(); ++x) {
            const int a = alphaLine[x];
            if (a == 0) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                const int baseValue = dstLine[x * 3 + c];
                const int genValue = genLine[x * 3 + c];
                dstLine[x * 3 + c] = uchar((baseValue * (255 - a) + genValue * a + 127) / 255);
            }
        }
    }
    return result;
}

struct PreparedInpaintImages
{
    OwnedSdImage initImage;
    OwnedSdImage maskImage;
    QImage baseImage;
    QImage alphaMask;
    QRect pasteRect;
};

bool prepareInpaintImages(const SdGenerateRequest &request, PreparedInpaintImages *prepared, QString *errorMessage)
{
    QImage baseImage = loadScaledRgbQImage(request.initImagePath, request.width, request.height, errorMessage);
    if (baseImage.isNull()) {
        return false;
    }

    QImage alphaMask = buildProcessedMask(request.maskImagePath,
                                          request.width,
                                          request.height,
                                          request.maskEdgeWidth,
                                          request.maskBlurRadius,
                                          errorMessage);
    if (alphaMask.isNull()) {
        return false;
    }

    QRect pasteRect(QPoint(0, 0), baseImage.size());
    QImage initForModel = baseImage;
    QImage maskForModel = alphaMask;
    if (request.inpaintOnlyMasked) {
        QRect bounds = maskBoundingRect(alphaMask);
        if (bounds.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QString(u8"Only masked 区域为空. 请检查 Mask 图.");
            }
            return false;
        }
        const int padding = std::max(32, request.maskEdgeWidth + request.maskBlurRadius * 2 + 8);
        pasteRect = expandedAspectRect(bounds, baseImage.size(), request.width, request.height, padding);
        initForModel = baseImage.copy(pasteRect).scaled(request.width, request.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        maskForModel = alphaMask.copy(pasteRect).scaled(request.width, request.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    if (!qImageToSdImage(initForModel, 3, &prepared->initImage, errorMessage)) {
        return false;
    }
    if (!qImageToSdImage(maskForModel, 1, &prepared->maskImage, errorMessage)) {
        return false;
    }

    prepared->baseImage = baseImage;
    prepared->alphaMask = alphaMask;
    prepared->pasteRect = pasteRect;
    return true;
}

bool saveInpaintResult(const sd_image_t &image, const PreparedInpaintImages &prepared, const QString &path, QString *errorMessage)
{
    QImage generated = sdImageToQImage(image, errorMessage);
    if (generated.isNull()) {
        return false;
    }
    QImage composed = composeWithMask(prepared.baseImage, generated, prepared.alphaMask, prepared.pasteRect, errorMessage);
    if (composed.isNull()) {
        return false;
    }
    if (!composed.save(path, "PNG")) {
        if (errorMessage) {
            *errorMessage = QString(u8"保存输出图像失败: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool saveSdImage(const sd_image_t &image, const QString &path, QString *errorMessage)
{
    if (!image.data || image.width == 0 || image.height == 0 || image.channel == 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"生成图像为空.");
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
            *errorMessage = QString(u8"生成图像通道数不支持: %1").arg(image.channel);
        }
        return false;
    }

    if (out.isNull()) {
        if (errorMessage) {
            *errorMessage = QString(u8"转换生成图像失败.");
        }
        return false;
    }
    if (!out.save(path, "PNG")) {
        if (errorMessage) {
            *errorMessage = QString(u8"保存输出图像失败: %1").arg(path);
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
    return info ? QString::fromUtf8(info) : QString(u8"stable-diffusion.cpp 系统信息不可用.");
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
        log(QString(u8"错误: %1").arg(error));
        return result;
    }
    if (!ensureParentDir(request.outputPath, &error)) {
        result.errorMessage = error;
        log(QString(u8"错误: %1").arg(error));
        return result;
    }
    const bool vaeDecodeOnly = !request.inpaint;
    if (!ensureContext(request.modelPath, vaeDecodeOnly, &error)) {
        result.errorMessage = error;
        log(QString(u8"错误: %1").arg(error));
        return result;
    }

    PreparedInpaintImages preparedInpaint;
    if (request.inpaint) {
        if (!prepareInpaintImages(request, &preparedInpaint, &error)) {
            result.errorMessage = error;
            log(QString(u8"错误: %1").arg(error));
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
        params.init_image = preparedInpaint.initImage.image();
        params.mask_image = preparedInpaint.maskImage.image();
    }

    log(QString(u8"开始生成. model=%1 size=%2x%3 steps=%4 seed=%5")
            .arg(request.modelPath)
            .arg(request.width)
            .arg(request.height)
            .arg(request.steps)
            .arg(request.seed));
    if (request.inpaint) {
        log(QString(u8"Inpaint 参数. edge=%1 px blur=%2 px only_masked=%3 strength=%4")
                .arg(request.maskEdgeWidth)
                .arg(request.maskBlurRadius)
                .arg(request.inpaintOnlyMasked ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(double(request.strength), 0, 'f', 2));
    }

    s_callbackTarget = this;
    sd_set_log_callback(&StableDiffusionEngine::sdLogCallback, nullptr);
    sd_set_progress_callback(&StableDiffusionEngine::sdProgressCallback, nullptr);

    sd_image_t *images = generate_image(m_context, &params);
    s_callbackTarget = nullptr;

    if (!images) {
        result.errorMessage = QString(u8"stable-diffusion.cpp generate_image 返回空结果.");
        log(QString(u8"错误: %1").arg(result.errorMessage));
        return result;
    }

    const bool saved = request.inpaint
        ? saveInpaintResult(images[0], preparedInpaint, request.outputPath, &error)
        : saveSdImage(images[0], request.outputPath, &error);
    if (!saved) {
        freeGeneratedImages(images, 1);
        result.errorMessage = error;
        log(QString(u8"错误: %1").arg(error));
        return result;
    }

    freeGeneratedImages(images, 1);
    result.success = true;
    log(QString(u8"输出已保存: %1").arg(request.outputPath));
    return result;
}

bool StableDiffusionEngine::validateRequest(const SdGenerateRequest &request, QString *errorMessage) const
{
    if (request.modelPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString(u8"模型路径为空.");
        }
        return false;
    }
    if (!QFileInfo::exists(request.modelPath)) {
        if (errorMessage) {
            *errorMessage = QString(u8"模型文件不存在: %1").arg(request.modelPath);
        }
        return false;
    }
    if (request.prompt.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString(u8"提示词为空.");
        }
        return false;
    }
    if (request.width <= 0 || request.height <= 0 || request.width % 64 != 0 || request.height % 64 != 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"宽度和高度必须是大于 0 的 64 倍数.");
        }
        return false;
    }
    if (request.steps <= 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"步数必须大于 0.");
        }
        return false;
    }
    if (request.outputPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString(u8"输出路径为空.");
        }
        return false;
    }
    if (request.strength <= 0.0f || request.strength > 1.0f) {
        if (errorMessage) {
            *errorMessage = QString(u8"重绘强度必须在 (0, 1] 范围内.");
        }
        return false;
    }
    if (request.maskEdgeWidth < 0 || request.maskBlurRadius < 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"Mask 边缘宽度和模糊半径不能小于 0.");
        }
        return false;
    }
    if (request.inpaint && request.maskedContentMode != 0) {
        if (errorMessage) {
            *errorMessage = QString(u8"masked content=latent noise 需要修改 stable-diffusion.cpp 并重编 DLL, 当前未启用.");
        }
        return false;
    }
    if (request.inpaint) {
        if (!QFileInfo::exists(request.initImagePath)) {
            if (errorMessage) {
                *errorMessage = QString(u8"原图文件不存在: %1").arg(request.initImagePath);
            }
            return false;
        }
        if (!QFileInfo::exists(request.maskImagePath)) {
            if (errorMessage) {
                *errorMessage = QString(u8"Mask 图文件不存在: %1").arg(request.maskImagePath);
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
    params.free_params_immediately = false;

    log(QString(u8"正在加载模型上下文: %1, vae_decode_only=%2")
            .arg(normalized)
            .arg(vaeDecodeOnly ? QStringLiteral("true") : QStringLiteral("false")));
    m_context = new_sd_ctx(&params);
    if (!m_context) {
        if (errorMessage) {
            *errorMessage = QString(u8"初始化 stable-diffusion.cpp 上下文失败, model=%1").arg(normalized);
        }
        return false;
    }
    if (!sd_ctx_supports_image_generation(m_context)) {
        releaseContext();
        if (errorMessage) {
            *errorMessage = QString(u8"已加载模型不支持图像生成: %1").arg(normalized);
        }
        return false;
    }

    m_contextModelPath = normalized;
    m_contextVaeDecodeOnly = vaeDecodeOnly;
    log(QString(u8"模型上下文加载完成."));
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
    emit s_callbackTarget->progressMessage(QString(u8"步数 %1/%2, %3 ms")
                                               .arg(step)
                                               .arg(steps)
                                               .arg(double(time), 0, 'f', 2));
}
