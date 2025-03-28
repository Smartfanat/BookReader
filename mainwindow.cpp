#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QFileInfo>
#include <QApplication>
#include <QPdfWriter>
#include <QPainter>
#include <QFileDialog>
#include <QProgressDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ctx(ddjvu_context_create("djvu_reader"))
{
    QWidget *central = new QWidget;
    this->setMinimumSize(800, 600);

    // === Menu Bar ===
    QMenuBar *menuBar = new QMenuBar(this);

    QMenu *fileMenu = menuBar->addMenu("File");
    fileMenu->addAction("Open", this, &MainWindow::openFile, QKeySequence("Ctrl+O"));

    fileMenu->addAction("File Info", this, [this]() {
        if (!doc) {
            QMessageBox::information(this, "File Info", "No file is currently open.");
            return;
        }

        QString info;
        info += "DjVu File Info\n\n";
        info += QString("File Path: %1\n").arg(currentFilePath);
        info += QString("Page Count: %1\n").arg(pageCount);

        if (pageCount > 0) {
            ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, 0);
            while (!ddjvu_page_decoding_done(page))
                ddjvu_message_wait(ctx);

            int width = ddjvu_page_get_width(page);
            int height = ddjvu_page_get_height(page);
            int dpi = ddjvu_page_get_resolution(page);
            ddjvu_page_release(page);

            info += QString("Page Size: %1 x %2 px\n").arg(width).arg(height);
            info += QString("DPI: %1\n").arg(dpi);
        }

        QMessageBox::information(this, "DjVu File Info", info);
    }, QKeySequence("Ctrl+I"));



    recentFilesMenu = fileMenu->addMenu("Open Recent");
    updateRecentFilesMenu();
    fileMenu->addAction("Export DjVu to PDF", this, &MainWindow::exportToPdf, QKeySequence("Ctrl+P"));

    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close, QKeySequence("Ctrl+Q"));

    QMenu *viewMenu = menuBar->addMenu("View");
    viewMenu->addAction("Zoom In", this, &MainWindow::zoomIn, QKeySequence("Ctrl++"));
    viewMenu->addAction("Zoom Out", this, &MainWindow::zoomOut, QKeySequence("Ctrl+-"));
    viewMenu->addSeparator();
    QAction *continuousScrollAction = viewMenu->addAction("Continuous Scroll");
    continuousScrollAction->setCheckable(true);
    continuousScrollAction->setChecked(false);
    connect(continuousScrollAction, &QAction::toggled, this, [this](bool enabled) {
        enableContinuousScroll(enabled);
    });

    QAction *facingPagesAction = viewMenu->addAction("Facing Pages");
    facingPagesAction->setCheckable(true);
    facingPagesAction->setChecked(false);
    connect(facingPagesAction, &QAction::toggled, this, [this](bool enabled) {
        enableFacingPages(enabled);
    });

    QAction *normalSizeAction = viewMenu->addAction("Normal Size");
    normalSizeAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(normalSizeAction, &QAction::triggered, this, [this]() {
        fitToWindow = true;
        scrollArea->setWidgetResizable(true);
        loadPage(currentPage);
        qDebug() << "Normal Size triggered";
    });


    QAction *toggleDarkAction = viewMenu->addAction("Dark Mode");
    toggleDarkAction->setCheckable(true);
    toggleDarkAction->setChecked(true);
    connect(toggleDarkAction, &QAction::toggled, this, &MainWindow::applyDarkMode);

    QAction *toggleFullScreenAction = viewMenu->addAction("Toggle Full Screen");
    toggleFullScreenAction->setShortcut(QKeySequence("F11"));
    connect(toggleFullScreenAction, &QAction::triggered, this, [this]() {
        isFullScreen = !isFullScreen;
        fitToWindow = true;
        scrollArea->setWidgetResizable(true);
        if (isFullScreen)
            showFullScreen();
        else
            showNormal();
    });

    QAction *fitToWindowAction = viewMenu->addAction("Fit to Window", [this]() {
        fitToWindow = true;
        loadPage(currentPage);
    });
    fitToWindowAction->setCheckable(true);
    fitToWindowAction->setChecked(true);

    QMenu *navMenu = menuBar->addMenu("Navigate");
    navMenu->addAction("Previous Page", this, &MainWindow::prevPage, QKeySequence("PgUp"));
    navMenu->addAction("Next Page", this, &MainWindow::nextPage, QKeySequence("PgDown"));

    QMenu *helpMenu = menuBar->addMenu("Help");
    helpMenu->addAction("About", this, [this]() {
        QMessageBox::about(this, "About Book Reader",
                           "🗂️ Book Reader\nBuilt with Qt, libdjvu, and poppler-qt\n© 2025 Eugene Dudnyk");
    });

    setMenuBar(menuBar);

    scrollArea = new QScrollArea;
    imageLabel = new ImageLabel;
    imageLabel->setText("Open a file");
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("background-color: #1a1a1a;");
    static_cast<ImageLabel *>(imageLabel)->setScrollArea(scrollArea);
    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(fitToWindow); // true or false depending on mode
    imageLabel->adjustSize();

    // === Controls ===
    QPushButton *openBtn = new QPushButton("Open");
    QPushButton *zoomInBtn = new QPushButton("Zoom In");
    QPushButton *zoomOutBtn = new QPushButton("Zoom Out");
    nextBtn = new QPushButton("Next");
    prevBtn = new QPushButton("Previous");

    pageLabel = new QLabel("Page 0 of 0");
    pageInput = new QSpinBox;
    pageInput->setMinimum(1);
    pageInput->setMaximum(1);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addSpacerItem(new QSpacerItem(scrollArea->width()/5, 0, QSizePolicy::Fixed));
    btnLayout->addWidget(prevBtn);
    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(nextBtn);
    btnLayout->addWidget(zoomOutBtn);
    btnLayout->addWidget(zoomInBtn);
    btnLayout->addWidget(pageLabel);
    btnLayout->addWidget(pageInput);
    btnLayout->addSpacerItem(new QSpacerItem(scrollArea->width()/5, 0, QSizePolicy::Fixed));
    thumbList = new QListWidget;
    thumbList->setIconSize(QSize(80, 100));
    thumbList->setFixedWidth(100);
    thumbList->setResizeMode(QListView::Adjust);
    thumbList->setViewMode(QListView::IconMode);
    thumbList->setMovement(QListView::Static);
    thumbList->setSpacing(5);
    thumbList->hide();

    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(scrollArea);
    rightLayout->addLayout(btnLayout);

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(thumbList);
    mainLayout->addLayout(rightLayout);

    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->addLayout(mainLayout);
    central->setLayout(centralLayout);
    setCentralWidget(central);
    setWindowTitle("Book Reader");

    // === Connections ===
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::nextPage);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::prevPage);
    connect(zoomInBtn, &QPushButton::clicked, this, &MainWindow::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, this, &MainWindow::zoomOut);

    auto goToPage = [this]() {
        int target = pageInput->value() - 1;
        if (target >= 0 && target < pageCount)
            loadPage(target);
    };
    connect(pageInput, &QSpinBox::editingFinished, this, goToPage);

    connect(thumbList, &QListWidget::currentRowChanged, this, [this](int index) {
        if (index >= 0 && index < pageCount && index != currentPage)
            loadPage(index);
    });
}

MainWindow::~MainWindow() {
    if (doc) ddjvu_document_release(doc);
    if (ctx) ddjvu_context_release(ctx);
}

void MainWindow::openFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open File", "", "DjVu or PDF Files (*.djvu *.pdf)");
    if (filePath.isEmpty())
        return;

    currentFilePath = filePath;

    if (filePath.endsWith(".djvu", Qt::CaseInsensitive)) {
        isPdf = false;
        openDjvuFile(filePath);
    } else if (filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        isPdf = true;
        openPdfFile(filePath);
    }
}

void MainWindow::openDjvuFile(const QString &filePath) {
    if (doc) ddjvu_document_release(doc);
    doc = ddjvu_document_create_by_filename(ctx, filePath.toUtf8().data(), TRUE);
    while (!ddjvu_document_decoding_done(doc)) {
        ddjvu_message_wait(ctx);
    }

    pageCount = ddjvu_document_get_pagenum(doc);
    if (pageCount <= 0) {
        QMessageBox::warning(this, "Error", "Failed to open DjVu file or no pages found.");
        thumbList->hide(); // Hide it if loading failed
        return;
    }

    // Update recent files
    QSettings settings("MyCompany", "BookReader");
    QStringList list = settings.value("recentFiles").toStringList();
    list.removeAll(filePath);
    list.prepend(filePath);
    while (list.size() > 5)
        list.removeLast();
    settings.setValue("recentFiles", list);
    updateRecentFilesMenu();

    currentPage = 0;
    zoom = 1.0;
    fitToWindow = true;

    pageInput->setMaximum(pageCount);

    thumbList->clear();
    thumbnails.clear();
    thumbList->blockSignals(true);

    for (int i = 0; i < pageCount; ++i) {
        ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, i);
        while (!ddjvu_page_decoding_done(page))
            ddjvu_message_wait(ctx);

        int w = ddjvu_page_get_width(page);
        int h = ddjvu_page_get_height(page);
        double thumbScale = 80.0 / w;
        int tw = static_cast<int>(w * thumbScale);
        int th = static_cast<int>(h * thumbScale);

        ddjvu_rect_t rrect = {0, 0, static_cast<unsigned int>(tw), static_cast<unsigned int>(th)};
        ddjvu_format_t *fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
        ddjvu_format_set_row_order(fmt, 1);
        QByteArray buffer(tw * th * 3, 0);
        ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rrect, &rrect, fmt, tw * 3, buffer.data());
        ddjvu_format_release(fmt);

        QImage thumbImg((uchar *)buffer.data(), tw, th, tw * 3, QImage::Format_RGB888);
        thumbnails.push_back(thumbImg.copy());

        QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnails[i])), "");
        thumbList->addItem(item);

        ddjvu_page_release(page);
    }

    thumbList->blockSignals(false);
    thumbList->setCurrentRow(0);
    // enableContinuousScroll(false);
    // enableFacingPages(false);
    loadSinglePage();
    thumbList->show();

    if (centralWidget())
        centralWidget()->setFocus(Qt::OtherFocusReason);
}

void MainWindow::loadPage(int pageNum)
{
    if (!doc && !pdfDoc) return;
    if (pageNum < 0 || pageNum >= pageCount) return;

    currentPage = pageNum;

    QImage image;
    if (isPdf) {
        double scale = fitToWindow ? scrollArea->viewport()->width() / 800.0 : zoom;
        image = renderPdfPage(pageNum, scale);
    } else {
        ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, pageNum);
        while (!ddjvu_page_decoding_done(page))
            ddjvu_message_wait(ctx);
        image = renderPage(page, -1);
        ddjvu_page_release(page);
    }

    if (imageLabel) {
        scrollArea->takeWidget();
        delete imageLabel;
        imageLabel = nullptr;
    }

    imageLabel = new ImageLabel;
    imageLabel->setPixmap(QPixmap::fromImage(image));
    imageLabel->resize(image.size());
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("background-color: #1a1a1a;");
    imageLabel->setScrollArea(scrollArea);

    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(fitToWindow);

    pageLabel->setText(QString("Page %1 of %2").arg(currentPage + 1).arg(pageCount));

    pageInput->blockSignals(true);
    pageInput->setValue(currentPage + 1);
    pageInput->blockSignals(false);

    if (centralWidget())
        centralWidget()->setFocus(Qt::OtherFocusReason);
}

QImage MainWindow::renderPage(ddjvu_page_t *page, double customScale) {
    int origWidth = ddjvu_page_get_width(page);
    int origHeight = ddjvu_page_get_height(page);

    double scale;
    if (customScale > 0) {
        scale = customScale;
        qDebug() << "[renderPage] customScale =" << scale;
    } else if (fitToWindow) {
        QSize areaSize = scrollArea->viewport()->size();
        double scaleW = static_cast<double>(areaSize.width()) / origWidth;
        double scaleH = static_cast<double>(areaSize.height()) / origHeight;
        scale = std::min(scaleW, scaleH);
        qDebug() << "[renderPage] fitToWindow scale =" << scale;
    } else {
        QSize areaSize = scrollArea->viewport()->size();
        double scaleW = static_cast<double>(areaSize.width()) / origWidth;
        double scaleH = static_cast<double>(areaSize.height()) / origHeight;
        scale = std::min(scaleW*zoom, scaleH*zoom);
    }

    int width = static_cast<int>(origWidth * scale);
    int height = static_cast<int>(origHeight * scale);

    qDebug() << "[renderPage] final image size =" << width << "x" << height;

    ddjvu_rect_t rrect = {0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
    ddjvu_format_t *fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
    ddjvu_format_set_row_order(fmt, 1);
    QByteArray buffer(width * height * 3, 0);
    ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rrect, &rrect, fmt, width * 3, buffer.data());
    ddjvu_format_release(fmt);

    return QImage((uchar *)buffer.data(), width, height, width * 3, QImage::Format_RGB888).copy();
}


void MainWindow::nextPage() {
    if (currentPage + 1 < pageCount)
        loadPage(currentPage + 1);
}

void MainWindow::prevPage() {
    if (currentPage > 0)
        loadPage(currentPage - 1);
}

void MainWindow::zoomIn() {
    fitToWindow = false;
    if (fitToWindowAction)
        fitToWindowAction->setChecked(false);
    // scrollArea->setWidgetResizable(false);

    // Store center position
    QPoint centerBefore = scrollArea->viewport()->rect().center();
    QPointF ratioCenter(
        static_cast<double>(scrollArea->horizontalScrollBar()->value() + centerBefore.x()) / scrollArea->widget()->width(),
        static_cast<double>(scrollArea->verticalScrollBar()->value() + centerBefore.y()) / scrollArea->widget()->height()
        );

    zoom *= 1.1;
    loadPage(currentPage);

    int hVal = static_cast<int>(scrollArea->widget()->width() * ratioCenter.x()) - scrollArea->viewport()->width() / 2;
    int vVal = static_cast<int>(scrollArea->widget()->height() * ratioCenter.y()) - scrollArea->viewport()->height() / 2;
    scrollArea->horizontalScrollBar()->setValue(hVal);
    scrollArea->verticalScrollBar()->setValue(vVal);
}

void MainWindow::zoomOut() {
    fitToWindow = false;
    if (fitToWindowAction)
        fitToWindowAction->setChecked(false);
    scrollArea->setWidgetResizable(false);

    // Store center
    QPoint centerBefore = scrollArea->viewport()->rect().center();
    QPointF ratioCenter(
        static_cast<double>(scrollArea->horizontalScrollBar()->value() + centerBefore.x()) / scrollArea->widget()->width(),
        static_cast<double>(scrollArea->verticalScrollBar()->value() + centerBefore.y()) / scrollArea->widget()->height()
        );

    zoom /= 1.1;
    loadPage(currentPage);

    int hVal = static_cast<int>(scrollArea->widget()->width() * ratioCenter.x()) - scrollArea->viewport()->width() / 2;
    int vVal = static_cast<int>(scrollArea->widget()->height() * ratioCenter.y()) - scrollArea->viewport()->height() / 2;
    scrollArea->horizontalScrollBar()->setValue(hVal);
    scrollArea->verticalScrollBar()->setValue(vVal);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (fitToWindow)
        loadPage(currentPage);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space) {
        if (continuousScrollMode) {
            int delta = scrollArea->viewport()->height();
            if (event->modifiers() & Qt::ShiftModifier)
                delta = -delta;

            QScrollBar *vScroll = scrollArea->verticalScrollBar();
            vScroll->setValue(vScroll->value() + delta);
        } else {
            if (event->modifiers() & Qt::ShiftModifier)
                prevPage();  // Shift+Space → Previous Page
            else
                nextPage();  // Space → Next Page
        }

        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event); // fallback
}

void MainWindow::updateRecentFilesMenu() {
    recentFilesMenu->clear();

    QSettings settings("MyCompany", "BookReader");
    QStringList files = settings.value("recentFiles").toStringList();

    if (files.isEmpty()) {
        QAction *empty = recentFilesMenu->addAction("(No recent files)");
        empty->setEnabled(false);
        return;
    }

    for (const QString &path : files) {
        QString label = QFileInfo(path).fileName();
        QAction *act = recentFilesMenu->addAction(label);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, [this, path]() {
            if (!QFile::exists(path)) {
                QMessageBox::warning(this, "File Not Found", "This file no longer exists.");
                QSettings settings("MyCompany", "BookReader");
                QStringList list = settings.value("recentFiles").toStringList();
                list.removeAll(path);
                settings.setValue("recentFiles", list);
                updateRecentFilesMenu();
                return;
            }
            currentFilePath = path;

            if (path.endsWith(".djvu", Qt::CaseInsensitive)) {
                isPdf = false;
                openDjvuFile(path);
            } else if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
                isPdf = true;
                openPdfFile(path);
            }
        });
    }
}

void MainWindow::applyDarkMode(bool enable) {
    isDarkMode = enable;
    qApp->setStyleSheet(enable ? R"(
        QWidget {
            background-color: #121212;
            color: #eeeeee;
        }

        QPushButton {
            background-color: #1e1e1e;
            color: #ffffff;
            border: 1px solid #333;
            padding: 4px;
        }

        QPushButton:hover {
            background-color: #2a2a2a;
        }

        QScrollArea {
            background-color: #1a1a1a;
        }

        QLabel {
            color: #eeeeee;
        }

        QMenuBar, QMenu {
            background-color: #1e1e1e;
            color: #ffffff;
        }

        QMenu::item:selected {
            background-color: #2a2a2a;
        }
    )" : "");
}

void MainWindow::loadSinglePage()
{
    if ( scrollArea->isEnabled() && imageLabel->isEnabled() )
    {
        scrollArea->takeWidget();
        scrollArea->setWidget(imageLabel);
        scrollArea->setWidgetResizable(true);
        loadPage(currentPage);
    }
}


void MainWindow::enableContinuousScroll(bool enabled) {
    continuousScrollMode = enabled;

    if (!enabled) {
        scrollArea->takeWidget();
        scrollArea->setWidget(imageLabel);
        scrollArea->setWidgetResizable(true);
        loadPage(currentPage);
        return;
    }

    multiPageWidget = new QWidget;
    multiPageLayout = new QVBoxLayout(multiPageWidget);
    multiPageLayout->setAlignment(Qt::AlignTop);
    multiPageWidget->setLayout(multiPageLayout);

    scrollArea->takeWidget();
    scrollArea->setWidget(multiPageWidget);
    scrollArea->setWidgetResizable(true);

    QSize areaSize = scrollArea->viewport()->size();
    int targetWidth = areaSize.width() - 20;

    QProgressDialog progress("Rendering all pages...", "Cancel", 0, pageCount, this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(200); // Show after short delay

    for (int i = 0; i < pageCount; ++i) {
        progress.setValue(i);
        QApplication::processEvents(); // Let the UI update

        if (progress.wasCanceled())
            break;

        ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, i);
        while (!ddjvu_page_decoding_done(page))
            ddjvu_message_wait(ctx);

        int origWidth = ddjvu_page_get_width(page);
        double scale = static_cast<double>(targetWidth) / origWidth;

        QImage image = renderPage(page, scale);
        ddjvu_page_release(page);

        QLabel *pageLabel = new QLabel;
        pageLabel->setPixmap(QPixmap::fromImage(image));
        pageLabel->setAlignment(Qt::AlignCenter);
        pageLabel->setStyleSheet("margin-bottom: 10px;");
        multiPageLayout->addWidget(pageLabel);
    }

    progress.setValue(pageCount);
}

void MainWindow::exportToPdf() {
    if (!doc && !pdfDoc) {
        QMessageBox::warning(this, "Export to PDF", "No file is currently open.");
        return;
    }

    if (isPdf || currentFilePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "Export to PDF", "File is not a Djvu document.");
        return;
    }

    QString suggestedName = QFileInfo(currentFilePath).completeBaseName() + ".pdf";
    QString suggestedPath = QFileInfo(currentFilePath).absolutePath() + "/" + suggestedName;

    QString pdfPath = QFileDialog::getSaveFileName(this,
                                                   "Export as PDF",
                                                   suggestedPath,
                                                   "PDF Files (*.pdf)");
    if (pdfPath.isEmpty())
        return;

    if (!pdfPath.endsWith(".pdf", Qt::CaseInsensitive))
        pdfPath += ".pdf";

    QPdfWriter pdfWriter(pdfPath);
    pdfWriter.setPageSize(QPageSize::A4);
    pdfWriter.setResolution(300);

    QPainter painter(&pdfWriter);

    for (int i = 0; i < pageCount; ++i) {
        ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, i);
        while (!ddjvu_page_decoding_done(page))
            ddjvu_message_wait(ctx);

        QImage image = renderPage(page, 1.0);
        ddjvu_page_release(page);

        QRect rect = painter.viewport();
        QSize imgSize = image.size();
        imgSize.scale(rect.size(), Qt::KeepAspectRatio);
        painter.drawImage(QRect(QPoint(0, 0), imgSize), image);

        if (i < pageCount - 1)
            pdfWriter.newPage();
    }

    painter.end();
    QMessageBox::information(this, "Export Complete", "Document exported as PDF successfully.");
}

void MainWindow::enableFacingPages(bool enabled) {
    facingPagesMode = enabled;

    if (continuousScrollMode) {
        QMessageBox::information(this, "Facing Pages",
                                 "Disable Continuous Scroll Mode first.");
        return;
    }

    if (!enabled) {
        scrollArea->takeWidget();
        scrollArea->setWidget(imageLabel);
        scrollArea->setWidgetResizable(true);
        loadPage(currentPage);
        return;
    }

    // Determine left/right page pair
    int leftPage = (currentPage % 2 == 0) ? currentPage : currentPage - 1;
    int rightPage = leftPage + 1;

    // Load both pages
    ddjvu_page_t *left = ddjvu_page_create_by_pageno(doc, leftPage);
    ddjvu_page_t *right = (rightPage < pageCount)
                              ? ddjvu_page_create_by_pageno(doc, rightPage)
                              : nullptr;

    while (!ddjvu_page_decoding_done(left))
        ddjvu_message_wait(ctx);
    if (right)
        while (!ddjvu_page_decoding_done(right))
            ddjvu_message_wait(ctx);

    QImage leftImg = renderPage(left, -1);  // use viewport-based scale
    QImage rightImg = right ? renderPage(right, -1) : QImage();

    ddjvu_page_release(left);
    if (right) ddjvu_page_release(right);

    // Combine images side-by-side
    int combinedWidth = leftImg.width() + (rightImg.isNull() ? 0 : rightImg.width());
    int combinedHeight = std::max(leftImg.height(), rightImg.height());

    QImage combined(combinedWidth, combinedHeight, QImage::Format_RGB888);
    combined.fill(Qt::black);  // background color

    QPainter p(&combined);
    p.drawImage(0, 0, leftImg);
    if (!rightImg.isNull())
        p.drawImage(leftImg.width(), 0, rightImg);
    p.end();

    // Scale to fit viewport
    QSize targetSize = scrollArea->viewport()->size();
    QImage scaled = combined.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QLabel *comboLabel = new QLabel;
    comboLabel->setPixmap(QPixmap::fromImage(scaled));
    comboLabel->setAlignment(Qt::AlignCenter);

    scrollArea->takeWidget();
    scrollArea->setWidget(comboLabel);
    scrollArea->setWidgetResizable(true);
}

void MainWindow::openPdfFile(const QString &filePath) {
    if (pdfDoc) {
        pdfDoc.reset();
        pdfDoc = nullptr;
    }

    pdfDoc = Poppler::Document::load(filePath);
    if (!pdfDoc || pdfDoc->isLocked()) {
        QMessageBox::warning(this, "Error", "Unable to open PDF or it's encrypted.");
        return;
    }

    pageCount = pdfDoc->numPages();
    currentPage = 0;
    zoom = 1.0;
    fitToWindow = true;

    pageInput->setMaximum(pageCount);
    thumbList->clear();
    thumbnails.clear();
    thumbList->hide();

    // Update recent files
    QSettings settings("MyCompany", "BookReader");
    QStringList list = settings.value("recentFiles").toStringList();
    list.removeAll(filePath);
    list.prepend(filePath);
    while (list.size() > 5)
        list.removeLast();
    settings.setValue("recentFiles", list);
    updateRecentFilesMenu();

    currentPage = 0;
    zoom = 1.0;
    fitToWindow = true;

    pageInput->setMaximum(pageCount);

    thumbList->clear();
    thumbnails.clear();
    thumbList->blockSignals(true);

    // for (int i = 0; i < pageCount; ++i) {
    //     auto page = pdfDoc->page(i);
    //     if (!page)
    //         continue;

    //     QSizeF size = page->pageSizeF();
    //     int w = static_cast<int>(size.width());
    //     int h = static_cast<int>(size.height());
    //     double thumbScale = 80.0 / w;
    //     int tw = static_cast<int>(w * thumbScale);
    //     int th = static_cast<int>(h * thumbScale);

    //     QImage thumbImg = page->renderToImage(thumbScale * 72.0, thumbScale * 72.0);

    //     if (!thumbImg.isNull()) {
    //         thumbnails.push_back(thumbImg.copy());
    //         QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbImg)), "");
    //         thumbList->addItem(item);
    //     }
    // }

    ThumbnailWorker *worker = new ThumbnailWorker(pdfDoc.get(), this);
    connect(worker, &ThumbnailWorker::thumbnailReady, this, [this](int i, QImage image) {
        if (i >= thumbnails.size())
            thumbnails.resize(i + 1);
        thumbnails[i] = image;

        QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(image)), "");
        thumbList->insertItem(i, item);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &QObject::destroyed, worker, &QThread::quit);
    connect(this, &QObject::destroyed, worker, &QObject::deleteLater);
    worker->start();

    thumbList->blockSignals(false);
    thumbList->setCurrentRow(0);
    thumbList->show();

    loadPage(currentPage);
}

QImage MainWindow::renderPdfPage(int pageNum, double scale) {
    if (!pdfDoc)
        return QImage();

    auto page = pdfDoc->page(pageNum);
    if (!page)
        return QImage();

    QImage image = page->renderToImage(scale * 150, scale * 150.0);
    QSize targetSize = scrollArea->viewport()->size()*1.6;
    image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return image;
}
