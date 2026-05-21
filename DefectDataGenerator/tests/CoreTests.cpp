#include "storage/ProjectStore.h"
#include "vision/MaskUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

namespace {

bool check(bool condition, const QString &message)
{
    if (!condition) {
        std::cerr << message.toStdString() << std::endl;
        return false;
    }
    return true;
}

bool testMaskStats()
{
    cv::Mat mask(32, 48, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(10, 5, 12, 8), cv::Scalar(255), cv::FILLED);
    QRect bbox;
    int area = 0;
    QString error;
    if (!MaskUtils::maskStats(mask, &bbox, &area, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    return check(bbox == QRect(10, 5, 12, 8), QStringLiteral("bbox mismatch")) &&
           check(area == 96, QStringLiteral("area mismatch"));
}

bool testEmptyMaskFails()
{
    cv::Mat mask(16, 16, CV_8UC1, cv::Scalar(0));
    QRect bbox;
    int area = 0;
    QString error;
    const bool ok = MaskUtils::maskStats(mask, &bbox, &area, &error);
    return check(!ok, QStringLiteral("empty mask should fail")) &&
           check(!error.isEmpty(), QStringLiteral("empty mask should provide error"));
}

bool testProjectRoundTrip()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return false;
    }
    ProjectStore store;
    QString error;
    if (!store.createProject(tempDir.path(), QStringLiteral("PartA"), &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    ProjectData data;
    QStringList validation;
    if (!store.loadProject(tempDir.path(), &data, &validation, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!check(data.productName == QStringLiteral("PartA"), QStringLiteral("product name mismatch"))) {
        return false;
    }
    for (const QString &subDir : ProjectStore::requiredDirs()) {
        if (!check(QDir(tempDir.path()).exists(subDir), QStringLiteral("missing project dir: %1").arg(subDir))) {
            return false;
        }
    }
    return true;
}

bool testMaskedCopyDoesNotWriteOutsideMask()
{
    cv::Mat base(32, 32, CV_8UC3, cv::Scalar(128, 128, 128));
    cv::Mat defect(10, 12, CV_8UC3, cv::Scalar(255, 0, 0));
    cv::Mat mask(10, 12, CV_8UC1, cv::Scalar(0));
    cv::line(mask, cv::Point(1, 1), cv::Point(10, 8), cv::Scalar(255), 2);

    cv::Rect roi(8, 9, defect.cols, defect.rows);
    cv::Mat canvasRoi = base(roi);
    defect.copyTo(canvasRoi, mask);

    for (int y = 0; y < mask.rows; ++y) {
        for (int x = 0; x < mask.cols; ++x) {
            const cv::Vec3b pixel = canvasRoi.at<cv::Vec3b>(y, x);
            if (mask.at<uchar>(y, x) == 0 && pixel != cv::Vec3b(128, 128, 128)) {
                return check(false, QStringLiteral("masked copy wrote outside mask"));
            }
        }
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (!testMaskStats()) {
        return 1;
    }
    if (!testEmptyMaskFails()) {
        return 1;
    }
    if (!testProjectRoundTrip()) {
        return 1;
    }
    if (!testMaskedCopyDoesNotWriteOutsideMask()) {
        return 1;
    }
    return 0;
}
