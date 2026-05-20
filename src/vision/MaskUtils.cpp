#include "vision/MaskUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace {

std::string nativePath(const QString &path)
{
    return QFile::encodeName(QDir::toNativeSeparators(path)).toStdString();
}

} // namespace

namespace MaskUtils {

bool loadImage(const QString &path, cv::Mat *image, QString *errorMessage)
{
    if (!image) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Image output is null.");
        }
        return false;
    }
    if (!QFileInfo::exists(path)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Image file does not exist: %1").arg(path);
        }
        return false;
    }
    *image = cv::imread(nativePath(path), cv::IMREAD_COLOR);
    if (image->empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read image: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool loadGrayMask(const QString &path, cv::Mat *mask, QString *errorMessage)
{
    if (!mask) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask output is null.");
        }
        return false;
    }
    if (!QFileInfo::exists(path)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask file does not exist: %1").arg(path);
        }
        return false;
    }
    *mask = cv::imread(nativePath(path), cv::IMREAD_GRAYSCALE);
    if (mask->empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read mask: %1").arg(path);
        }
        return false;
    }
    cv::threshold(*mask, *mask, 0, 255, cv::THRESH_BINARY);
    return true;
}

bool saveImage(const QString &path, const cv::Mat &image, QString *errorMessage)
{
    if (image.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot save empty image: %1").arg(path);
        }
        return false;
    }
    QFileInfo info(path);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create image output directory: %1").arg(info.absolutePath());
        }
        return false;
    }
    if (!cv::imwrite(nativePath(path), image)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write image: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool saveMask(const QString &path, const cv::Mat &mask, QString *errorMessage)
{
    if (mask.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot save empty mask: %1").arg(path);
        }
        return false;
    }
    cv::Mat out;
    if (mask.channels() == 1) {
        cv::threshold(mask, out, 0, 255, cv::THRESH_BINARY);
    } else {
        cv::Mat gray;
        cv::cvtColor(mask, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, out, 0, 255, cv::THRESH_BINARY);
    }
    return saveImage(path, out, errorMessage);
}

bool maskStats(const cv::Mat &mask, QRect *bbox, int *area, QString *errorMessage)
{
    if (mask.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask is empty.");
        }
        return false;
    }
    cv::Mat gray;
    if (mask.channels() == 1) {
        gray = mask;
    } else {
        cv::cvtColor(mask, gray, cv::COLOR_BGR2GRAY);
    }
    cv::Mat binary;
    cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY);

    const int count = cv::countNonZero(binary);
    if (count <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask has no non-zero pixels.");
        }
        return false;
    }
    std::vector<cv::Point> points;
    cv::findNonZero(binary, points);
    const cv::Rect rect = cv::boundingRect(points);
    if (bbox) {
        *bbox = QRect(rect.x, rect.y, rect.width, rect.height);
    }
    if (area) {
        *area = count;
    }
    return true;
}

cv::Mat qImageToGrayMask(const QImage &image)
{
    QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat mat(gray.height(), gray.width(), CV_8UC1, const_cast<uchar *>(gray.bits()), gray.bytesPerLine());
    cv::Mat copy = mat.clone();
    cv::threshold(copy, copy, 0, 255, cv::THRESH_BINARY);
    return copy;
}

QImage matToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return QImage();
    }
    if (mat.type() == CV_8UC1) {
        QImage image(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return image.copy();
    }
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return image.copy();
    }
    if (mat.type() == CV_8UC4) {
        QImage image(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_ARGB32);
        return image.copy();
    }
    return QImage();
}

cv::Mat qImageToBgr(const QImage &image)
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.bits()), rgb.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

} // namespace MaskUtils
