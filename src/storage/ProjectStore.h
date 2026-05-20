#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

struct DefectAssetRecord
{
    QString id;
    QString imagePath;
    QString maskPath;
    QString defectType;
    QRect bbox;
    int area = 0;
    QString sourceProduct;
    QDateTime createdAt;
    int useCount = 0;
};

struct OkImageRecord
{
    QString id;
    QString imagePath;
    QString allowedMaskPath;
};

struct ProjectData
{
    QString productName;
    QStringList defectTypes;
    QVector<DefectAssetRecord> defectAssets;
    QVector<OkImageRecord> okImages;
    QJsonObject defaultGenerateParams;
};

class ProjectStore
{
public:
    static QString projectFileName();
    static QStringList requiredDirs();

    bool createProject(const QString &projectDir, const QString &productName, QString *errorMessage);
    bool loadProject(const QString &projectDir, ProjectData *data, QStringList *validationErrors, QString *errorMessage);
    bool saveProject(const QString &projectDir, const ProjectData &data, QString *errorMessage);
    bool ensureProjectDirs(const QString &projectDir, QString *errorMessage);

    bool importOkImage(const QString &projectDir, const QString &sourcePath, ProjectData *data, QString *errorMessage);
    bool importDefectSource(const QString &projectDir, const QString &sourcePath, QString *targetRelativePath, QString *errorMessage);
    bool addDefectAsset(const QString &projectDir,
                        const QString &imageRelativePath,
                        const QString &maskAbsolutePath,
                        const QString &defectType,
                        const QRect &bbox,
                        int area,
                        ProjectData *data,
                        QString *errorMessage);

    QString absolutePath(const QString &projectDir, const QString &relativePath) const;
    QString relativePath(const QString &projectDir, const QString &absolutePath) const;

private:
    QString uniqueRelativePath(const QString &projectDir, const QString &subDir, const QString &sourcePath) const;
    bool copyFileToProject(const QString &projectDir, const QString &sourcePath, const QString &targetRelativePath, QString *errorMessage) const;
};
