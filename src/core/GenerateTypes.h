#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

struct GenerateRequest
{
    QString projectDir;
    QString okImagePath;
    QString okAllowedMaskPath;
    QStringList defectAssetIds;
    int count = 0;
    int defectsPerImageMin = 1;
    int defectsPerImageMax = 3;
    int seed = 1;
    QVariantMap params;
};

struct GeneratedItem
{
    QString imagePath;
    QString maskPath;
    QString metadataPath;
};

struct GenerateResult
{
    bool success = false;
    QVector<GeneratedItem> items;
    QString errorMessage;
};

struct DefectPlacement
{
    QString defectAssetId;
    QString defectType;
    QRect bbox;
};
