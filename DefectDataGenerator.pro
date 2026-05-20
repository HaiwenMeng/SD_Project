QT += widgets
CONFIG += c++17
TEMPLATE = app
TARGET = DefectDataGenerator

SOURCES += \
    src/app/main.cpp \
    src/app/MainWindow.cpp \
    src/core/Logger.cpp \
    src/storage/ProjectStore.cpp \
    src/ui/ImageView.cpp \
    src/vision/BackendFactory.cpp \
    src/vision/MaskUtils.cpp \
    src/vision/OpenCvDefectTransferBackend.cpp

HEADERS += \
    src/app/MainWindow.h \
    src/core/GenerateTypes.h \
    src/core/Logger.h \
    src/storage/ProjectStore.h \
    src/ui/ImageView.h \
    src/vision/BackendFactory.h \
    src/vision/IGenerationBackend.h \
    src/vision/MaskUtils.h \
    src/vision/OpenCvDefectTransferBackend.h

FORMS += \
    src/app/MainWindow.ui

INCLUDEPATH += $$PWD/src

OPENCV_ROOT = F:/QtProject/BaseLibX64/Opencv_full460
exists($$OPENCV_ROOT/include/opencv2/opencv.hpp) {
    INCLUDEPATH += $$OPENCV_ROOT/include
    LIBS += -L$$OPENCV_ROOT/lib
    CONFIG(debug, debug|release) {
        exists($$OPENCV_ROOT/lib/opencv_world460d.lib) {
            LIBS += -lopencv_world460d
        } else {
            LIBS += -lopencv_world460
        }
    } else {
        LIBS += -lopencv_world460
    }
}

DESTDIR = ./BIN
