#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <opencv2/core.hpp>

namespace MaskUtils {

bool loadImage(const QString &path, cv::Mat *image, QString *errorMessage);
bool loadGrayMask(const QString &path, cv::Mat *mask, QString *errorMessage);
bool saveImage(const QString &path, const cv::Mat &image, QString *errorMessage);
bool saveMask(const QString &path, const cv::Mat &mask, QString *errorMessage);
bool maskStats(const cv::Mat &mask, QRect *bbox, int *area, QString *errorMessage);
cv::Mat qImageToGrayMask(const QImage &image);
QImage matToQImage(const cv::Mat &mat);
cv::Mat qImageToBgr(const QImage &image);

}
