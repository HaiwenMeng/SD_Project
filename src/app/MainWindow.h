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

    Ui::MainWindow *ui = nullptr;
    ProjectStore m_store;
    ProjectData m_project;
    QString m_projectDir;
    QStringListModel m_okModel;
    QStringListModel m_defectModel;
    QStringListModel m_assetModel;
    QStringListModel m_generatedModel;
};
