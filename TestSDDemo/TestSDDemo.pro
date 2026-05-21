QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
TEMPLATE = app
TARGET = TestSDDemo

DESTDIR = $$PWD/../BIN
OBJECTS_DIR = $$PWD/build/qmake/obj
MOC_DIR = $$PWD/build/qmake/moc
RCC_DIR = $$PWD/build/qmake/rcc
UI_DIR = $$PWD/build/qmake/ui

DEFINES += SD_BUILD_SHARED_LIB

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    StableDiffusionEngine.cpp \
    widget.cpp

HEADERS += \
    StableDiffusionEngine.h \
    widget.h

FORMS += \
    widget.ui

INCLUDEPATH += \
    $$PWD/third_party/stable-diffusion.cpp/include

SD_CUDA_BUILD_DIR = $$PWD/build/stable-diffusion-cuda-localenv
SD_CUDA_LIB = $$SD_CUDA_BUILD_DIR/stable-diffusion.lib
SD_CUDA_DLL = $$SD_CUDA_BUILD_DIR/bin/stable-diffusion.dll
CUDA_BIN = $$PWD/toolchains/CUDA118/bin

exists($$SD_CUDA_LIB) {
    LIBS += $$quote($$SD_CUDA_LIB)
} else {
    error("CUDA stable-diffusion.lib not found. Build stable-diffusion.cpp with SD_CUDA=ON first.")
}

win32:exists($$SD_CUDA_DLL) {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$SD_CUDA_DLL) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t)
}

win32:exists($$CUDA_BIN/cudart64_110.dll) {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$CUDA_BIN/cudart64_110.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t)
}

win32:exists($$CUDA_BIN/cublas64_11.dll) {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$CUDA_BIN/cublas64_11.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t)
}

win32:exists($$CUDA_BIN/cublasLt64_11.dll) {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$CUDA_BIN/cublasLt64_11.dll) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t)
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
