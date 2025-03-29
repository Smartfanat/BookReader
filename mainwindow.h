#pragma once

#include <QMainWindow>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QListWidget>
#include <qboxlayout.h>

#include "imagelabel.h"

#include <QThread>
#include <QMutex>
#include <QWaitCondition>


extern "C" {
#include <libdjvu/ddjvuapi.h>
}

#include <poppler-qt6.h>

class ThumbnailWorker : public QThread {
    Q_OBJECT
public:
    ThumbnailWorker(Poppler::Document *doc, QObject *parent = nullptr)
        : QThread(parent), doc(doc) {}

    void run() override {
        if (!doc) return;

        for (int i = 0; i < doc->numPages(); ++i) {
            auto page = doc->page(i);
            if (!page) continue;

            QImage image = page->renderToImage(36, 36); // Low DPI thumbnail

            if (!image.isNull()) {
                emit thumbnailReady(i, image);
            }
        }
    }

signals:
    void thumbnailReady(int index, QImage image);

private:
    Poppler::Document *doc;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

    enum class Theme {
        Light,
        Dark,
        Sepia
    };

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void openFile();
    void nextPage();
    void prevPage();
    void zoomIn();
    void zoomOut();
    void exportToPdf();

private:
    void loadPage(int pageNum);
    QImage renderPage(ddjvu_page_t *page, double customScale);
    void openDjvuFile(const QString &filePath);
    void openPdfFile(const QString &filePath);

    ddjvu_context_t *ctx = nullptr;
    ddjvu_document_t *doc = nullptr;
    int pageCount = 0;
    int currentPage = 0;
    double zoom = 1.0;

    bool fitToWindow = true;

    QScrollArea *scrollArea;
    ImageLabel *imageLabel;
    QPushButton *nextBtn;
    QPushButton *prevBtn;

    QLabel *pageLabel;
    QSpinBox *pageInput;

    QListWidget *thumbList;
    QVector<QImage> thumbnails;

    QStringList recentFiles;
    QMenu *recentFilesMenu = nullptr;
    void updateRecentFilesMenu();

    bool isDarkMode = true;
    bool isFullScreen = false;
    QString currentFilePath;

    bool continuousScrollMode = false;
    QWidget *multiPageWidget = nullptr;
    QVBoxLayout *multiPageLayout = nullptr;

    bool facingPagesMode = false;
    QWidget *dualPageWidget = nullptr;
    QHBoxLayout *dualPageLayout = nullptr;

    QAction *fitToWindowAction = nullptr;

    std::unique_ptr<Poppler::Document > pdfDoc = nullptr;
    bool isPdf = false;

    Theme currentTheme = Theme::Dark; // default

    bool showThumbnails = true;

    void applyTheme(Theme theme);
    void enableContinuousScroll(bool enabled);
    void enableFacingPages(bool enabled);
    void loadSinglePage();
    QImage renderPdfPage(int pageNum, double scale);
};
