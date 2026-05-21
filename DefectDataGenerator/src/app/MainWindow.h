#pragma once

#include "storage/ProjectStore.h"

#include <QMainWindow>
#include <QStringListModel>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void newProject();
    void openProject();
    void importOkImages();
    void importDefectImages();
    void saveCurrentMaskAsAsset();
    void generateImages();
    void approveSelectedGenerated();
    void rejectSelectedGenerated();
    void exportApprovedDataset();
    void onOkImageSelected();
    void onDefectSourceSelected();
    void onGeneratedSelected();
    void showOkContextMenu(const QPoint &pos);
    void showDefectSourceContextMenu(const QPoint &pos);
    void showAssetContextMenu(const QPoint &pos);
    void showGeneratedContextMenu(const QPoint &pos);
    void deleteSelectedOkImage();
    void clearOkImages();
    void deleteSelectedDefectSource();
    void clearDefectSources();
    void deleteSelectedAsset();
    void clearAssets();
    void deleteSelectedGenerated();
    void clearGeneratedImages();
    void setToolView();
    void setToolRect();
    void setToolPolygon();
    void setToolBrush();
    void setToolEraser();
    void clearMask();
    void appendLog(const QString &line);

private:
    void setupConnections();
    void refreshProjectViews();
    void refreshGeneratedList();
    bool ensureProjectLoaded() const;
    bool saveProject();
    QString selectedOkImagePath() const;
    QString selectedGeneratedPath() const;
    QString currentDefectSourceRelativePath() const;
    QString makeTempMaskPath() const;
    void setProjectDir(const QString &dir);
    void showError(const QString &message);
    void moveReviewedFileSet(const QString &imagePath, const QString &targetSubDir);
    bool removeFileIfExists(const QString &path, QString *errorMessage);
    bool removeOkImageAt(int row, QString *errorMessage);
    bool removeAssetAt(int row, QString *errorMessage);
    bool removeDefectSourceByRelativePath(const QString &relativePath, QString *errorMessage);
    bool removeGeneratedFileSet(const QString &imagePath, QString *errorMessage);
    bool confirmDelete(const QString &message) const;

    Ui::MainWindow *ui = nullptr;
    ProjectStore m_store;
    ProjectData m_project;
    QString m_projectDir;
    QStringListModel m_okModel;
    QStringListModel m_defectModel;
    QStringListModel m_assetModel;
    QStringListModel m_generatedModel;
};
