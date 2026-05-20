#pragma once

#include "vision/IGenerationBackend.h"

#include "storage/ProjectStore.h"

#include <opencv2/core.hpp>
#include <random>

class OpenCvDefectTransferBackend : public IGenerationBackend
{
public:
    QString backendName() const override;
    GenerateResult generate(const GenerateRequest &request,
                            const ProjectData &projectData,
                            const ProjectStore &projectStore) override;

private:
    struct AssetMat
    {
        DefectAssetRecord record;
        cv::Mat image;
        cv::Mat mask;
        cv::Rect bbox;
    };

    bool validateRequest(const GenerateRequest &request, QString *errorMessage) const;
    bool loadAssets(const GenerateRequest &request,
                    const ProjectData &projectData,
                    const ProjectStore &projectStore,
                    QVector<AssetMat> *assets,
                    QString *errorMessage) const;
    bool placeOneDefect(const AssetMat &asset,
                        cv::Mat *canvas,
                        cv::Mat *combinedMask,
                        const cv::Mat &allowedMask,
                        std::mt19937 *rng,
                        const QVariantMap &params,
                        DefectPlacement *placement,
                        QString *errorMessage) const;
    QVariantMap normalizeParams(const QVariantMap &input) const;
};
