# DefectDataGenerator

Qt5/OpenCV defect image generation tool for industrial anomaly dataset creation.

## Build

Preferred local build on this machine:

```powershell
D:\AppInstall\Qt5.15.2\msvc2019_64\bin\qmake.exe DefectDataGenerator.pro
nmake
```

The qmake project uses `F:/QtProject/BaseLibX64/Opencv_full460` by default. If OpenCV is installed elsewhere, update `OPENCV_ROOT` in `DefectDataGenerator.pro` or use CMake with `OpenCV_DIR`.

## V1 workflow

1. Create or open a project.
2. Import OK images and defect source images.
3. Annotate defect masks.
4. Save defect assets into the project library.
5. Configure OpenCV defect transfer generation.
6. Review generated items.
7. Export approved items as `images`, `masks`, `metadata`, and `dataset.json`.
