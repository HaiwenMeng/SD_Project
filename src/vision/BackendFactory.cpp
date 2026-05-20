#include "vision/BackendFactory.h"

#include "vision/OpenCvDefectTransferBackend.h"

std::unique_ptr<IGenerationBackend> BackendFactory::createBackend(const QString &backendName)
{
    if (backendName.compare(QStringLiteral("OpenCV"), Qt::CaseInsensitive) == 0 ||
        backendName.compare(QStringLiteral("OpenCVDefectTransfer"), Qt::CaseInsensitive) == 0) {
        return std::unique_ptr<IGenerationBackend>(new OpenCvDefectTransferBackend());
    }
    return nullptr;
}
