# DefectDataGenerator

Qt5/OpenCV defect image generation tool for industrial anomaly dataset creation.

## Build

Preferred local build on this machine:

```powershell
cd F:\SD_Project
D:\AppInstall\Qt5.15.2\msvc2019_64\bin\qmake.exe SD_Project.pro
jom
```

The subdirs qmake project outputs `DefectDataGenerator.exe` to `F:/SD_Project/BIN`.
The qmake project uses `F:/QtProject/BaseLibX64/Opencv_full460` by default. If OpenCV is installed elsewhere, update `OPENCV_ROOT` in `DefectDataGenerator.pro`.
Runtime needs `F:/SD_Project/BIN/opencv_world460.dll` or `F:/QtProject/BaseLibX64/Opencv_full460/bin` in `PATH`.
This OpenCV package currently contains only `opencv_world460.lib`. Debug builds therefore define `CV_IGNORE_DEBUG_BUILD_GUARD` and link the release OpenCV library. For strict ABI matching, build Release or provide `opencv_world460d.lib`.

## V1 workflow

1. Create or open a project.
2. Import OK images and defect source images.
3. Annotate defect masks.
4. Save defect assets into the project library.
5. Configure OpenCV defect transfer generation.
6. Review generated items.
7. Export approved items as `images`, `masks`, `metadata`, and `dataset.json`.
