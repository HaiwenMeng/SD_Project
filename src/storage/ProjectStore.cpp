#include "storage/ProjectStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>

namespace {

QJsonObject rectToJson(const QRect &rect)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), rect.x());
    obj.insert(QStringLiteral("y"), rect.y());
    obj.insert(QStringLiteral("width"), rect.width());
    obj.insert(QStringLiteral("height"), rect.height());
    return obj;
}

QRect rectFromJson(const QJsonObject &obj)
{
    return QRect(obj.value(QStringLiteral("x")).toInt(),
                 obj.value(QStringLiteral("y")).toInt(),
                 obj.value(QStringLiteral("width")).toInt(),
                 obj.value(QStringLiteral("height")).toInt());
}

QJsonObject defectAssetToJson(const DefectAssetRecord &asset)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), asset.id);
    obj.insert(QStringLiteral("imagePath"), asset.imagePath);
    obj.insert(QStringLiteral("maskPath"), asset.maskPath);
    obj.insert(QStringLiteral("defectType"), asset.defectType);
    obj.insert(QStringLiteral("bbox"), rectToJson(asset.bbox));
    obj.insert(QStringLiteral("area"), asset.area);
    obj.insert(QStringLiteral("sourceProduct"), asset.sourceProduct);
    obj.insert(QStringLiteral("createdAt"), asset.createdAt.toString(Qt::ISODate));
    obj.insert(QStringLiteral("useCount"), asset.useCount);
    return obj;
}

DefectAssetRecord defectAssetFromJson(const QJsonObject &obj)
{
    DefectAssetRecord asset;
    asset.id = obj.value(QStringLiteral("id")).toString();
    asset.imagePath = obj.value(QStringLiteral("imagePath")).toString();
    asset.maskPath = obj.value(QStringLiteral("maskPath")).toString();
    asset.defectType = obj.value(QStringLiteral("defectType")).toString();
    asset.bbox = rectFromJson(obj.value(QStringLiteral("bbox")).toObject());
    asset.area = obj.value(QStringLiteral("area")).toInt();
    asset.sourceProduct = obj.value(QStringLiteral("sourceProduct")).toString();
    asset.createdAt = QDateTime::fromString(obj.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    asset.useCount = obj.value(QStringLiteral("useCount")).toInt();
    return asset;
}

QJsonObject okImageToJson(const OkImageRecord &image)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), image.id);
    obj.insert(QStringLiteral("imagePath"), image.imagePath);
    obj.insert(QStringLiteral("allowedMaskPath"), image.allowedMaskPath);
    return obj;
}

OkImageRecord okImageFromJson(const QJsonObject &obj)
{
    OkImageRecord image;
    image.id = obj.value(QStringLiteral("id")).toString();
    image.imagePath = obj.value(QStringLiteral("imagePath")).toString();
    image.allowedMaskPath = obj.value(QStringLiteral("allowedMaskPath")).toString();
    return image;
}

} // namespace

QString ProjectStore::projectFileName()
{
    return QStringLiteral("project.ddg.json");
}

QStringList ProjectStore::requiredDirs()
{
    return QStringList()
        << QStringLiteral("ok_images")
        << QStringLiteral("defect_sources")
        << QStringLiteral("defect_masks")
        << QStringLiteral("generated")
        << QStringLiteral("approved")
        << QStringLiteral("rejected")
        << QStringLiteral("metadata")
        << QStringLiteral("logs");
}

bool ProjectStore::createProject(const QString &projectDir, const QString &productName, QString *errorMessage)
{
    if (!ensureProjectDirs(projectDir, errorMessage)) {
        return false;
    }

    ProjectData data;
    data.productName = productName.trimmed().isEmpty() ? QStringLiteral("DefaultProduct") : productName.trimmed();
    data.defaultGenerateParams.insert(QStringLiteral("scaleMin"), 0.6);
    data.defaultGenerateParams.insert(QStringLiteral("scaleMax"), 1.4);
    data.defaultGenerateParams.insert(QStringLiteral("rotationMin"), -30);
    data.defaultGenerateParams.insert(QStringLiteral("rotationMax"), 30);
    data.defaultGenerateParams.insert(QStringLiteral("brightnessMin"), -15);
    data.defaultGenerateParams.insert(QStringLiteral("brightnessMax"), 15);
    data.defaultGenerateParams.insert(QStringLiteral("featherMin"), 3);
    data.defaultGenerateParams.insert(QStringLiteral("featherMax"), 15);
    return saveProject(projectDir, data, errorMessage);
}

bool ProjectStore::loadProject(const QString &projectDir, ProjectData *data, QStringList *validationErrors, QString *errorMessage)
{
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project data output is null.");
        }
        return false;
    }

    QFile file(QDir(projectDir).filePath(projectFileName()));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open project file: %1").arg(file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid project JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    data->productName = root.value(QStringLiteral("productName")).toString();
    data->defaultGenerateParams = root.value(QStringLiteral("defaultGenerateParams")).toObject();
    data->defectTypes.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("defectTypes")).toArray()) {
        data->defectTypes.append(value.toString());
    }
    data->defectAssets.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("defectAssets")).toArray()) {
        data->defectAssets.append(defectAssetFromJson(value.toObject()));
    }
    data->okImages.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("okImages")).toArray()) {
        data->okImages.append(okImageFromJson(value.toObject()));
    }

    if (validationErrors) {
        validationErrors->clear();
        for (const DefectAssetRecord &asset : data->defectAssets) {
            if (!QFileInfo::exists(absolutePath(projectDir, asset.imagePath))) {
                validationErrors->append(QStringLiteral("Missing defect source: %1").arg(asset.imagePath));
            }
            if (!QFileInfo::exists(absolutePath(projectDir, asset.maskPath))) {
                validationErrors->append(QStringLiteral("Missing defect mask: %1").arg(asset.maskPath));
            }
        }
        for (const OkImageRecord &image : data->okImages) {
            if (!QFileInfo::exists(absolutePath(projectDir, image.imagePath))) {
                validationErrors->append(QStringLiteral("Missing OK image: %1").arg(image.imagePath));
            }
            if (!image.allowedMaskPath.isEmpty() && !QFileInfo::exists(absolutePath(projectDir, image.allowedMaskPath))) {
                validationErrors->append(QStringLiteral("Missing OK allowed mask: %1").arg(image.allowedMaskPath));
            }
        }
    }

    return true;
}

bool ProjectStore::saveProject(const QString &projectDir, const ProjectData &data, QString *errorMessage)
{
    if (!ensureProjectDirs(projectDir, errorMessage)) {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("productName"), data.productName);
    root.insert(QStringLiteral("defaultGenerateParams"), data.defaultGenerateParams);

    QJsonArray defectTypes;
    for (const QString &type : data.defectTypes) {
        defectTypes.append(type);
    }
    root.insert(QStringLiteral("defectTypes"), defectTypes);

    QJsonArray assets;
    for (const DefectAssetRecord &asset : data.defectAssets) {
        assets.append(defectAssetToJson(asset));
    }
    root.insert(QStringLiteral("defectAssets"), assets);

    QJsonArray okImages;
    for (const OkImageRecord &image : data.okImages) {
        okImages.append(okImageToJson(image));
    }
    root.insert(QStringLiteral("okImages"), okImages);

    QFile file(QDir(projectDir).filePath(projectFileName()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to save project file: %1").arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectStore::ensureProjectDirs(const QString &projectDir, QString *errorMessage)
{
    QDir dir(projectDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create project directory: %1").arg(projectDir);
        }
        return false;
    }

    for (const QString &subDir : requiredDirs()) {
        if (!dir.exists(subDir) && !dir.mkpath(subDir)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to create project sub directory: %1").arg(subDir);
            }
            return false;
        }
    }
    return true;
}

bool ProjectStore::importOkImage(const QString &projectDir, const QString &sourcePath, ProjectData *data, QString *errorMessage)
{
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project data is null.");
        }
        return false;
    }
    const QString targetRelative = uniqueRelativePath(projectDir, QStringLiteral("ok_images"), sourcePath);
    if (!copyFileToProject(projectDir, sourcePath, targetRelative, errorMessage)) {
        return false;
    }

    OkImageRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.imagePath = targetRelative;
    data->okImages.append(record);
    return true;
}

bool ProjectStore::importDefectSource(const QString &projectDir, const QString &sourcePath, QString *targetRelativePath, QString *errorMessage)
{
    const QString targetRelative = uniqueRelativePath(projectDir, QStringLiteral("defect_sources"), sourcePath);
    if (!copyFileToProject(projectDir, sourcePath, targetRelative, errorMessage)) {
        return false;
    }
    if (targetRelativePath) {
        *targetRelativePath = targetRelative;
    }
    return true;
}

bool ProjectStore::addDefectAsset(const QString &projectDir,
                                  const QString &imageRelativePath,
                                  const QString &maskAbsolutePath,
                                  const QString &defectType,
                                  const QRect &bbox,
                                  int area,
                                  ProjectData *data,
                                  QString *errorMessage)
{
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project data is null.");
        }
        return false;
    }
    if (defectType.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Defect type is empty.");
        }
        return false;
    }
    if (!bbox.isValid() || area <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Defect mask is empty or invalid.");
        }
        return false;
    }

    const QString targetRelative = uniqueRelativePath(projectDir, QStringLiteral("defect_masks"), maskAbsolutePath);
    if (!copyFileToProject(projectDir, maskAbsolutePath, targetRelative, errorMessage)) {
        return false;
    }

    DefectAssetRecord asset;
    asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.imagePath = imageRelativePath;
    asset.maskPath = targetRelative;
    asset.defectType = defectType.trimmed();
    asset.bbox = bbox;
    asset.area = area;
    asset.sourceProduct = data->productName;
    asset.createdAt = QDateTime::currentDateTime();
    asset.useCount = 0;
    data->defectAssets.append(asset);
    if (!data->defectTypes.contains(asset.defectType)) {
        data->defectTypes.append(asset.defectType);
    }
    return true;
}

QString ProjectStore::absolutePath(const QString &projectDir, const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return QString();
    }
    return QDir(projectDir).absoluteFilePath(relativePath);
}

QString ProjectStore::relativePath(const QString &projectDir, const QString &absolutePath) const
{
    return QDir(projectDir).relativeFilePath(absolutePath);
}

QString ProjectStore::uniqueRelativePath(const QString &projectDir, const QString &subDir, const QString &sourcePath) const
{
    QFileInfo sourceInfo(sourcePath);
    const QString suffix = sourceInfo.suffix().isEmpty() ? QStringLiteral("png") : sourceInfo.suffix();
    const QString base = sourceInfo.completeBaseName().isEmpty() ? QStringLiteral("image") : sourceInfo.completeBaseName();
    QString name = QStringLiteral("%1_%2.%3").arg(base, QUuid::createUuid().toString(QUuid::WithoutBraces), suffix);
    return QDir(subDir).filePath(name);
}

bool ProjectStore::copyFileToProject(const QString &projectDir, const QString &sourcePath, const QString &targetRelativePath, QString *errorMessage) const
{
    if (!QFileInfo::exists(sourcePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source file does not exist: %1").arg(sourcePath);
        }
        return false;
    }

    const QString targetPath = QDir(projectDir).absoluteFilePath(targetRelativePath);
    QFileInfo targetInfo(targetPath);
    QDir targetDir(targetInfo.absolutePath());
    if (!targetDir.exists() && !targetDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create target directory: %1").arg(targetDir.absolutePath());
        }
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to copy file to project: %1").arg(targetPath);
        }
        return false;
    }
    return true;
}
