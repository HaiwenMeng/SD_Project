#include "vision/OpenCvDefectTransferBackend.h"

#include "core/Logger.h"
#include "storage/ProjectStore.h"
#include "vision/MaskUtils.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <random>
#include <opencv2/imgproc.hpp>

QString OpenCvDefectTransferBackend::backendName() const
{
    return QStringLiteral("OpenCV");
}

GenerateResult OpenCvDefectTransferBackend::generate(const GenerateRequest &request,
                                                     const ProjectData &projectData,
                                                     const ProjectStore &projectStore)
{
    GenerateResult result;
    QString error;
    if (!validateRequest(request, &error)) {
        result.errorMessage = error;
        Logger::instance().log(Logger::Error, error);
        return result;
    }

    cv::Mat okImage;
    if (!MaskUtils::loadImage(request.okImagePath, &okImage, &error)) {
        result.errorMessage = error;
        Logger::instance().log(Logger::Error, error);
        return result;
    }

    cv::Mat allowedMask(okImage.rows, okImage.cols, CV_8UC1, cv::Scalar(255));
    if (!request.okAllowedMaskPath.isEmpty()) {
        if (!MaskUtils::loadGrayMask(request.okAllowedMaskPath, &allowedMask, &error)) {
            result.errorMessage = error;
            Logger::instance().log(Logger::Error, error);
            return result;
        }
        if (allowedMask.size() != okImage.size()) {
            result.errorMessage = QStringLiteral("Allowed mask size does not match OK image.");
            Logger::instance().log(Logger::Error, result.errorMessage);
            return result;
        }
        if (cv::countNonZero(allowedMask) <= 0) {
            result.errorMessage = QStringLiteral("Allowed mask has no valid placement pixels.");
            Logger::instance().log(Logger::Error, result.errorMessage);
            return result;
        }
    }

    QVector<AssetMat> assets;
    if (!loadAssets(request, projectData, projectStore, &assets, &error)) {
        result.errorMessage = error;
        Logger::instance().log(Logger::Error, error);
        return result;
    }

    const QVariantMap params = normalizeParams(request.params);
    std::mt19937 rng(static_cast<unsigned int>(request.seed));
    QDir projectDir(request.projectDir);
    QDir generatedDir(projectDir.filePath(QStringLiteral("generated")));
    QDir metadataDir(projectDir.filePath(QStringLiteral("metadata")));
    if (!generatedDir.exists() && !projectDir.mkpath(QStringLiteral("generated"))) {
        result.errorMessage = QStringLiteral("Failed to create generated directory.");
        Logger::instance().log(Logger::Error, result.errorMessage);
        return result;
    }
    if (!metadataDir.exists() && !projectDir.mkpath(QStringLiteral("metadata"))) {
        result.errorMessage = QStringLiteral("Failed to create metadata directory.");
        Logger::instance().log(Logger::Error, result.errorMessage);
        return result;
    }

    std::uniform_int_distribution<int> assetDist(0, assets.size() - 1);
    std::uniform_int_distribution<int> defectCountDist(request.defectsPerImageMin, request.defectsPerImageMax);

    for (int i = 0; i < request.count; ++i) {
        cv::Mat canvas = okImage.clone();
        cv::Mat combinedMask(okImage.rows, okImage.cols, CV_8UC1, cv::Scalar(0));
        QVector<DefectPlacement> placements;
        const int defectsThisImage = defectCountDist(rng);
        for (int d = 0; d < defectsThisImage; ++d) {
            DefectPlacement placement;
            const AssetMat &asset = assets.at(assetDist(rng));
            if (!placeOneDefect(asset, &canvas, &combinedMask, allowedMask, &rng, params, &placement, &error)) {
                result.errorMessage = QStringLiteral("Failed to place defect on image %1: %2").arg(i).arg(error);
                Logger::instance().log(Logger::Error, result.errorMessage);
                return result;
            }
            placements.append(placement);
        }

        const QString id = QStringLiteral("%1_%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz")),
                                                       QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString imagePath = generatedDir.filePath(id + QStringLiteral(".png"));
        const QString maskPath = generatedDir.filePath(id + QStringLiteral("_mask.png"));
        const QString metadataPath = metadataDir.filePath(id + QStringLiteral(".json"));
        if (!MaskUtils::saveImage(imagePath, canvas, &error) || !MaskUtils::saveMask(maskPath, combinedMask, &error)) {
            result.errorMessage = error;
            Logger::instance().log(Logger::Error, error);
            return result;
        }

        QJsonObject meta;
        meta.insert(QStringLiteral("okImagePath"), request.okImagePath);
        meta.insert(QStringLiteral("imagePath"), projectStore.relativePath(request.projectDir, imagePath));
        meta.insert(QStringLiteral("maskPath"), projectStore.relativePath(request.projectDir, maskPath));
        meta.insert(QStringLiteral("seed"), request.seed);
        meta.insert(QStringLiteral("backend"), backendName());
        meta.insert(QStringLiteral("params"), QJsonObject::fromVariantMap(params));
        QJsonArray placementArray;
        for (const DefectPlacement &placement : placements) {
            QJsonObject obj;
            obj.insert(QStringLiteral("defectAssetId"), placement.defectAssetId);
            obj.insert(QStringLiteral("defectType"), placement.defectType);
            QJsonObject bbox;
            bbox.insert(QStringLiteral("x"), placement.bbox.x());
            bbox.insert(QStringLiteral("y"), placement.bbox.y());
            bbox.insert(QStringLiteral("width"), placement.bbox.width());
            bbox.insert(QStringLiteral("height"), placement.bbox.height());
            obj.insert(QStringLiteral("bbox"), bbox);
            placementArray.append(obj);
        }
        meta.insert(QStringLiteral("placements"), placementArray);

        QFile metadataFile(metadataPath);
        if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.errorMessage = QStringLiteral("Failed to write metadata: %1").arg(metadataFile.errorString());
            Logger::instance().log(Logger::Error, result.errorMessage);
            return result;
        }
        metadataFile.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));

        GeneratedItem item;
        item.imagePath = imagePath;
        item.maskPath = maskPath;
        item.metadataPath = metadataPath;
        result.items.append(item);
    }

    result.success = true;
    Logger::instance().log(Logger::Info, QStringLiteral("Generated %1 item(s).").arg(result.items.size()));
    return result;
}

bool OpenCvDefectTransferBackend::validateRequest(const GenerateRequest &request, QString *errorMessage) const
{
    if (request.projectDir.isEmpty()) {
        *errorMessage = QStringLiteral("Project directory is empty.");
        return false;
    }
    if (request.okImagePath.isEmpty()) {
        *errorMessage = QStringLiteral("OK image path is empty.");
        return false;
    }
    if (request.defectAssetIds.isEmpty()) {
        *errorMessage = QStringLiteral("No defect asset selected.");
        return false;
    }
    if (request.count <= 0) {
        *errorMessage = QStringLiteral("Generation count must be greater than 0.");
        return false;
    }
    if (request.defectsPerImageMin <= 0 || request.defectsPerImageMax < request.defectsPerImageMin) {
        *errorMessage = QStringLiteral("Invalid defects per image range.");
        return false;
    }
    return true;
}

bool OpenCvDefectTransferBackend::loadAssets(const GenerateRequest &request,
                                             const ProjectData &projectData,
                                             const ProjectStore &projectStore,
                                             QVector<AssetMat> *assets,
                                             QString *errorMessage) const
{
    for (const QString &assetId : request.defectAssetIds) {
        auto it = std::find_if(projectData.defectAssets.begin(), projectData.defectAssets.end(), [&](const DefectAssetRecord &record) {
            return record.id == assetId;
        });
        if (it == projectData.defectAssets.end()) {
            *errorMessage = QStringLiteral("Selected defect asset does not exist: %1").arg(assetId);
            return false;
        }
        AssetMat asset;
        asset.record = *it;
        QString loadError;
        if (!MaskUtils::loadImage(projectStore.absolutePath(request.projectDir, it->imagePath), &asset.image, &loadError) ||
            !MaskUtils::loadGrayMask(projectStore.absolutePath(request.projectDir, it->maskPath), &asset.mask, &loadError)) {
            *errorMessage = loadError;
            return false;
        }
        if (asset.image.size() != asset.mask.size()) {
            *errorMessage = QStringLiteral("Defect image and mask size mismatch: %1").arg(it->id);
            return false;
        }
        QRect bbox;
        int area = 0;
        if (!MaskUtils::maskStats(asset.mask, &bbox, &area, errorMessage)) {
            return false;
        }
        asset.bbox = cv::Rect(bbox.x(), bbox.y(), bbox.width(), bbox.height());
        if (asset.bbox.width <= 0 || asset.bbox.height <= 0) {
            *errorMessage = QStringLiteral("Invalid defect bbox: %1").arg(it->id);
            return false;
        }
        assets->append(asset);
    }
    if (assets->isEmpty()) {
        *errorMessage = QStringLiteral("No valid defect assets loaded.");
        return false;
    }
    return true;
}

bool OpenCvDefectTransferBackend::placeOneDefect(const AssetMat &asset,
                                                 cv::Mat *canvas,
                                                 cv::Mat *combinedMask,
                                                 const cv::Mat &allowedMask,
                                                 std::mt19937 *rng,
                                                 const QVariantMap &params,
                                                 DefectPlacement *placement,
                                                 QString *errorMessage) const
{
    const double scaleMin = params.value(QStringLiteral("scaleMin")).toDouble();
    const double scaleMax = params.value(QStringLiteral("scaleMax")).toDouble();
    const int rotationMin = params.value(QStringLiteral("rotationMin")).toInt();
    const int rotationMax = params.value(QStringLiteral("rotationMax")).toInt();
    const int brightnessMin = params.value(QStringLiteral("brightnessMin")).toInt();
    const int brightnessMax = params.value(QStringLiteral("brightnessMax")).toInt();
    const int featherMin = params.value(QStringLiteral("featherMin")).toInt();
    const int featherMax = params.value(QStringLiteral("featherMax")).toInt();

    std::uniform_real_distribution<double> scaleDist(scaleMin, scaleMax);
    std::uniform_real_distribution<double> rotationDist(rotationMin, rotationMax);
    std::uniform_int_distribution<int> brightnessDist(brightnessMin, brightnessMax);
    std::uniform_int_distribution<int> featherDist(featherMin, featherMax);

    cv::Mat cropImage = asset.image(asset.bbox).clone();
    cv::Mat cropMask = asset.mask(asset.bbox).clone();
    const double scale = scaleDist(*rng);
    cv::Mat scaledImage;
    cv::Mat scaledMask;
    cv::resize(cropImage, scaledImage, cv::Size(), scale, scale, cv::INTER_LINEAR);
    cv::resize(cropMask, scaledMask, cv::Size(), scale, scale, cv::INTER_NEAREST);
    if (scaledImage.empty() || scaledMask.empty() || scaledImage.cols < 2 || scaledImage.rows < 2) {
        *errorMessage = QStringLiteral("Scaled defect is empty.");
        return false;
    }

    const cv::Point2f center(scaledImage.cols / 2.0f, scaledImage.rows / 2.0f);
    cv::Mat matrix = cv::getRotationMatrix2D(center, rotationDist(*rng), 1.0);
    const double absCos = std::abs(matrix.at<double>(0, 0));
    const double absSin = std::abs(matrix.at<double>(0, 1));
    const int boundWidth = static_cast<int>(scaledImage.rows * absSin + scaledImage.cols * absCos);
    const int boundHeight = static_cast<int>(scaledImage.rows * absCos + scaledImage.cols * absSin);
    matrix.at<double>(0, 2) += boundWidth / 2.0 - center.x;
    matrix.at<double>(1, 2) += boundHeight / 2.0 - center.y;

    cv::Mat rotatedImage;
    cv::Mat rotatedMask;
    cv::warpAffine(scaledImage, rotatedImage, matrix, cv::Size(boundWidth, boundHeight), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    cv::warpAffine(scaledMask, rotatedMask, matrix, cv::Size(boundWidth, boundHeight), cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
    cv::threshold(rotatedMask, rotatedMask, 0, 255, cv::THRESH_BINARY);

    QRect rotatedBbox;
    int rotatedArea = 0;
    if (!MaskUtils::maskStats(rotatedMask, &rotatedBbox, &rotatedArea, errorMessage)) {
        return false;
    }
    if (rotatedBbox.width() >= canvas->cols || rotatedBbox.height() >= canvas->rows) {
        *errorMessage = QStringLiteral("Defect is larger than OK image after transform.");
        return false;
    }

    cv::Mat finalImage = rotatedImage(cv::Rect(rotatedBbox.x(), rotatedBbox.y(), rotatedBbox.width(), rotatedBbox.height())).clone();
    cv::Mat finalMask = rotatedMask(cv::Rect(rotatedBbox.x(), rotatedBbox.y(), rotatedBbox.width(), rotatedBbox.height())).clone();
    finalImage.convertTo(finalImage, -1, 1.0, brightnessDist(*rng));

    std::uniform_int_distribution<int> xDist(0, canvas->cols - finalImage.cols);
    std::uniform_int_distribution<int> yDist(0, canvas->rows - finalImage.rows);
    cv::Point target;
    bool found = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        target = cv::Point(xDist(*rng), yDist(*rng));
        cv::Rect roi(target.x, target.y, finalImage.cols, finalImage.rows);
        cv::Mat allowedRoi = allowedMask(roi);
        cv::Mat overlap;
        cv::bitwise_and(allowedRoi, finalMask, overlap);
        const int maskArea = cv::countNonZero(finalMask);
        if (maskArea > 0 && cv::countNonZero(overlap) == maskArea) {
            found = true;
            break;
        }
    }
    if (!found) {
        *errorMessage = QStringLiteral("No valid placement found in allowed area.");
        return false;
    }

    int feather = featherDist(*rng);
    if (feather % 2 == 0) {
        ++feather;
    }
    feather = std::max(1, feather);
    cv::Mat alpha;
    finalMask.convertTo(alpha, CV_32FC1, 1.0 / 255.0);
    if (feather > 1) {
        cv::GaussianBlur(alpha, alpha, cv::Size(feather, feather), 0);
    }
    cv::Mat alpha3;
    cv::cvtColor(alpha, alpha3, cv::COLOR_GRAY2BGR);

    cv::Rect roi(target.x, target.y, finalImage.cols, finalImage.rows);
    cv::Mat canvasRoi = (*canvas)(roi);
    cv::Mat baseFloat;
    cv::Mat defectFloat;
    canvasRoi.convertTo(baseFloat, CV_32FC3);
    finalImage.convertTo(defectFloat, CV_32FC3);
    cv::Mat inverseAlpha = cv::Mat::ones(alpha3.size(), alpha3.type()) - alpha3;
    cv::Mat blended = defectFloat.mul(alpha3) + baseFloat.mul(inverseAlpha);
    blended.convertTo(canvasRoi, CV_8UC3);

    cv::Mat maskRoi = (*combinedMask)(roi);
    cv::bitwise_or(maskRoi, finalMask, maskRoi);

    placement->defectAssetId = asset.record.id;
    placement->defectType = asset.record.defectType;
    placement->bbox = QRect(target.x, target.y, finalImage.cols, finalImage.rows);
    return true;
}

QVariantMap OpenCvDefectTransferBackend::normalizeParams(const QVariantMap &input) const
{
    QVariantMap params = input;
    auto ensure = [&params](const QString &key, const QVariant &value) {
        if (!params.contains(key)) {
            params.insert(key, value);
        }
    };
    ensure(QStringLiteral("scaleMin"), 0.6);
    ensure(QStringLiteral("scaleMax"), 1.4);
    ensure(QStringLiteral("rotationMin"), -30);
    ensure(QStringLiteral("rotationMax"), 30);
    ensure(QStringLiteral("brightnessMin"), -15);
    ensure(QStringLiteral("brightnessMax"), 15);
    ensure(QStringLiteral("featherMin"), 3);
    ensure(QStringLiteral("featherMax"), 15);
    if (params.value(QStringLiteral("scaleMin")).toDouble() <= 0.0) {
        params[QStringLiteral("scaleMin")] = 0.6;
    }
    if (params.value(QStringLiteral("scaleMax")).toDouble() < params.value(QStringLiteral("scaleMin")).toDouble()) {
        params[QStringLiteral("scaleMax")] = params.value(QStringLiteral("scaleMin"));
    }
    return params;
}
