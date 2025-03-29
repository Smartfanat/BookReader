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
#include <QActionGroup>
#include <QCheckBox>
#include <QMimeData>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ctx(ddjvu_context_create("djvu_reader"))
{
    setAcceptDrops(true);

    QWidget *central = new QWidget;
    this->setMinimumSize(800, 600);

    QSettings settings("MyCompany", "BookReader");
    nightMode = settings.value("nightMode", false).toBool();
    warmthLevel = settings.value("warmthLevel", 20).toInt();
    autoNightMode = settings.value("autoNightMode", true).toBool();

    QTime now = QTime::currentTime();
    if (autoNightMode && now.hour() >= 20 && !nightMode) {
        nightMode = true;
    }

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
    QAction *toggleThumbnailsAction = viewMenu->addAction("Show Thumbnails");
    toggleThumbnailsAction->setCheckable(true);
    toggleThumbnailsAction->setChecked(true); // default on
    connect(toggleThumbnailsAction, &QAction::toggled, this, [this](bool enabled) {
        showThumbnails = enabled;
        thumbList->setVisible(enabled);
    });
    QAction *toggleNightMode = viewMenu->addAction("Night Mode");
    toggleNightMode->setCheckable(true);

    toggleNightMode->setChecked(nightMode);

    connect(toggleNightMode, &QAction::toggled, this, [this](bool enabled) {
        nightMode = enabled;
        QSettings settings("MyCompany", "BookReader");
        settings.setValue("nightMode", nightMode);

        refreshThumbnails();
        loadPage(currentPage);
    });
    viewMenu->addAction("Adjust Night Mode Warmth", this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle("Night Mode Settings");

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        QLabel *label = new QLabel("Warmth Level (0–100):");
        QSlider *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(warmthLevel);

        QCheckBox *autoNightBox = new QCheckBox("Enable Night Mode Automatically After 8PM");
        autoNightBox->setChecked(autoNightMode);

        layout->addWidget(label);
        layout->addWidget(slider);
        layout->addWidget(autoNightBox);

        connect(slider, &QSlider::valueChanged, this, [this](int value) {
            warmthLevel = value;
            QSettings settings("MyCompany", "BookReader");
            settings.setValue("warmthLevel", warmthLevel);
            if (nightMode)
                loadPage(currentPage);
        });

        connect(autoNightBox, &QCheckBox::toggled, this, [this](bool enabled) {
            autoNightMode = enabled;
            QSettings settings("MyCompany", "BookReader");
            settings.setValue("autoNightMode", autoNightMode);
        });

        dialog.exec();
    });


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


    QMenu *themeMenu = viewMenu->addMenu("Theme");

    QAction *lightTheme = themeMenu->addAction("Light");
    QAction *darkTheme = themeMenu->addAction("Dark");
    QAction *sepiaTheme = themeMenu->addAction("Sepia");

    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->addAction(lightTheme);
    themeGroup->addAction(darkTheme);
    themeGroup->addAction(sepiaTheme);

    lightTheme->setCheckable(true);
    darkTheme->setCheckable(true);
    sepiaTheme->setCheckable(true);
    darkTheme->setChecked(true); // default

    connect(lightTheme, &QAction::triggered, this, [this]() {
        applyTheme(Theme::Light);
    });
    connect(darkTheme, &QAction::triggered, this, [this]() {
        applyTheme(Theme::Dark);
    });
    connect(sepiaTheme, &QAction::triggered, this, [this]() {
        applyTheme(Theme::Sepia);
    });

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
    saveLastReadState();
    if (doc) ddjvu_document_release(doc);
    if (ctx) ddjvu_context_release(ctx);
}

void MainWindow::openFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open File", "", "DjVu or PDF Files (*.djvu *.pdf)");
    if (filePath.isEmpty())
        return;

    saveLastReadState();

    currentFilePath = filePath;

    if (filePath.endsWith(".djvu", Qt::CaseInsensitive)) {
        isPdf = false;
        openDjvuFile(filePath);
    } else if (filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        isPdf = true;
        openPdfFile(filePath);
    }

    setWindowTitle(tr("Book Reader") + " - " +QFileInfo(filePath).fileName());
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

    thumbList->blockSignals(true);
    thumbList->clear();
    thumbList->blockSignals(false);

    thumbnails.clear();
    originalThumbnails.clear();

    if (!showThumbnails) {
        thumbList->hide();
    } else {
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

            if (nightMode)
                thumbImg = applyNightMode(thumbImg);

            thumbnails.push_back(thumbImg.copy());
            originalThumbnails.push_back(thumbImg.copy());

            QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnails[i])), "");
            thumbList->addItem(item);

            ddjvu_page_release(page);
        }
        thumbList->blockSignals(false);
        thumbList->setCurrentRow(0);
        // enableContinuousScroll(false);
        // enableFacingPages(false);
    }

    loadLastReadState(currentFilePath);

    loadSinglePage();
    if (showThumbnails)
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
        if (nightMode && !image.isNull()) {
            image = applyNightMode(image);
        }
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

    if (showThumbnails) {
        thumbList->blockSignals(true);
        thumbList->setCurrentRow(currentPage);
        thumbList->blockSignals(false);
    }

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
    int step = facingPagesMode ? 2 : 1;
    if (currentPage + step < pageCount)
        currentPage += step;

    if (facingPagesMode)
        enableFacingPages(true);
    else
        loadPage(currentPage);
}

void MainWindow::prevPage() {
    int step = facingPagesMode ? 2 : 1;
    if (currentPage - step >= 0)
        currentPage -= step;

    if (facingPagesMode)
        enableFacingPages(true);
    else
        loadPage(currentPage);
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

            setWindowTitle(tr("Book Reader") + " - " +QFileInfo(path).fileName());
        });
    }
}

void MainWindow::applyTheme(Theme theme) {
    currentTheme = theme;

    switch (theme) {
    case Theme::Dark:
        qApp->setStyleSheet(R"(
            QWidget { background-color: #121212; color: #eeeeee; }
            QPushButton {
                background-color: #1e1e1e; color: #ffffff;
                border: 1px solid #333; padding: 4px;
            }
            QPushButton:hover { background-color: #2a2a2a; }
            QScrollArea { background-color: #1a1a1a; }
            QLabel { color: #eeeeee; }
            QMenuBar, QMenu {
                background-color: #1e1e1e; color: #ffffff;
            }
            QMenu::item:selected { background-color: #2a2a2a; }
        )");
        break;

    case Theme::Light:
        qApp->setStyleSheet(""); // Default Qt style
        break;

    case Theme::Sepia:
        qApp->setStyleSheet(R"(
            QWidget { background-color: #f4ecd8; color: #5b4636; }
            QPushButton {
                background-color: #e6d3b3; color: #5b4636;
                border: 1px solid #b39b74; padding: 4px;
            }
            QPushButton:hover { background-color: #ddc9a6; }
            QScrollArea { background-color: #f4ecd8; }
            QLabel { color: #5b4636; }
            QMenuBar, QMenu {
                background-color: #e8dcc2; color: #5b4636;
            }
            QMenu::item:selected { background-color: #d2c1a4; }
        )");
        break;
    }
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

void MainWindow::refreshThumbnails() {
    if (!showThumbnails || originalThumbnails.isEmpty()) return;

    thumbList->blockSignals(true);
    thumbList->clear();
    thumbnails.clear();

    for (int i = 0; i < originalThumbnails.size(); ++i) {
        QImage img = nightMode ? applyNightMode(originalThumbnails[i]) : originalThumbnails[i];
        thumbnails.push_back(img);

        QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(img)), "");
        thumbList->addItem(item);
    }

    thumbList->setCurrentRow(currentPage);
    thumbList->scrollToItem(thumbList->currentItem(), QAbstractItemView::PositionAtCenter);
    thumbList->blockSignals(false);
}

QImage MainWindow::applyNightMode(const QImage &input) {
    QImage img = input.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QColor color = QColor::fromRgb(line[x]);

            int h, s, v;
            color.getHsv(&h, &s, &v);

            // Invert brightness
            v = 255 - v;

            // Add warmth by shifting hue slightly toward red/yellow
            h = (h + warmthLevel) % 360;

            QColor newColor;
            newColor.setHsv(h, s, v);
            line[x] = newColor.rgba();
        }
    }
    return img;
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
    progress.setMinimumDuration(200);

    for (int i = 0; i < pageCount; ++i) {
        progress.setValue(i);
        QApplication::processEvents();

        if (progress.wasCanceled())
            break;

        QImage image;

        if (!isPdf) {
            ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, i);
            while (!ddjvu_page_decoding_done(page))
                ddjvu_message_wait(ctx);

            int origWidth = ddjvu_page_get_width(page);
            double scale = static_cast<double>(targetWidth) / origWidth;

            image = renderPage(page, scale);
            ddjvu_page_release(page);
        } else {
            auto page = pdfDoc->page(i);
            if (!page)
                continue;

            image = page->renderToImage(150, 150); // 150 DPI
            if (image.isNull())
                continue;

            image = image.scaledToWidth(targetWidth, Qt::SmoothTransformation);
        }

        if (nightMode && !image.isNull())
            image = applyNightMode(image);

        QLabel *pageLabel = new QLabel;
        pageLabel->setPixmap(QPixmap::fromImage(image));
        pageLabel->setAlignment(Qt::AlignCenter);
        pageLabel->setStyleSheet("margin-bottom: 10px;");
        pageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        multiPageLayout->addWidget(pageLabel);
    }

    multiPageLayout->addStretch();

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
        QMessageBox::information(this, "Facing Pages", "Disable Continuous Scroll Mode first.");
        return;
    }

    if (!enabled) {
        scrollArea->takeWidget();
        scrollArea->setWidget(imageLabel);
        scrollArea->setWidgetResizable(true);
        loadPage(currentPage);
        return;
    }

    int leftPage = (currentPage % 2 == 0) ? currentPage : currentPage - 1;
    int rightPage = leftPage + 1;

    QImage leftImg, rightImg;

    if (isPdf) {
        auto left = pdfDoc->page(leftPage);
        if (left)
            leftImg = left->renderToImage(150, 150).convertToFormat(QImage::Format_RGB888);

        if (rightPage < pageCount) {
            auto right = pdfDoc->page(rightPage);
            if (right)
                rightImg = right->renderToImage(150, 150).convertToFormat(QImage::Format_RGB888);
        }
    } else {
        ddjvu_page_t *left = ddjvu_page_create_by_pageno(doc, leftPage);
        ddjvu_page_t *right = (rightPage < pageCount)
                                  ? ddjvu_page_create_by_pageno(doc, rightPage)
                                  : nullptr;

        while (!ddjvu_page_decoding_done(left)) ddjvu_message_wait(ctx);
        if (right) while (!ddjvu_page_decoding_done(right)) ddjvu_message_wait(ctx);

        leftImg = renderPage(left, -1);
        if (right)
            rightImg = renderPage(right, -1);

        ddjvu_page_release(left);
        if (right) ddjvu_page_release(right);
    }

    // Ensure we have at least one valid image
    if (leftImg.isNull()) {
        QMessageBox::warning(this, "Facing Pages", "Cannot render left page.");
        return;
    }

    if (nightMode) leftImg = applyNightMode(leftImg);
    if (nightMode && !rightImg.isNull()) rightImg = applyNightMode(rightImg);

    int combinedWidth = leftImg.width() + (rightImg.isNull() ? 0 : rightImg.width());
    int combinedHeight = std::max(leftImg.height(), rightImg.height());

    QImage combined(combinedWidth, combinedHeight, QImage::Format_RGB888);
    combined.fill(Qt::black);

    QPainter painter(&combined);
    painter.drawImage(0, 0, leftImg);
    if (!rightImg.isNull())
        painter.drawImage(leftImg.width(), 0, rightImg);
    painter.end();

    QSize targetSize = scrollArea->viewport()->size();
    QImage scaled = combined.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QLabel *comboLabel = new QLabel;
    comboLabel->setPixmap(QPixmap::fromImage(scaled));
    comboLabel->setAlignment(Qt::AlignCenter);

    scrollArea->takeWidget();
    scrollArea->setWidget(comboLabel);
    scrollArea->setWidgetResizable(true);

    thumbList->blockSignals(true);
    thumbList->setCurrentRow(currentPage);
    thumbList->scrollToItem(thumbList->currentItem(), QAbstractItemView::PositionAtCenter);
    thumbList->blockSignals(false);
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

    // Update recent files
    QSettings settings("MyCompany", "BookReader");
    QStringList list = settings.value("recentFiles").toStringList();
    list.removeAll(filePath);
    list.prepend(filePath);
    while (list.size() > 5)
        list.removeLast();
    settings.setValue("recentFiles", list);
    updateRecentFilesMenu();

    pageInput->setMaximum(pageCount);

    thumbList->blockSignals(true);
    thumbList->clear();
    thumbList->blockSignals(false);

    thumbnails.clear();
    originalThumbnails.clear();

    if (!showThumbnails) {
        thumbList->hide();
    } else {
        ThumbnailWorker *worker = new ThumbnailWorker(pdfDoc.get(), this);
        connect(worker, &ThumbnailWorker::thumbnailReady, this, [this](int i, QImage image) {
            if (i >= thumbnails.size()) {
                thumbnails.resize(i + 1);
                originalThumbnails.resize(i + 1);
            }

            originalThumbnails[i] = image;

            if (nightMode)
                image = applyNightMode(image);

            thumbnails[i] = image;

            QListWidgetItem *item = new QListWidgetItem(QIcon(QPixmap::fromImage(image)), "");
            thumbList->insertItem(i, item);
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(this, &QObject::destroyed, worker, &QThread::quit);
        connect(this, &QObject::destroyed, worker, &QObject::deleteLater);
        worker->start();
        thumbList->show();
    }

    loadLastReadState(currentFilePath);

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

    if (nightMode && !image.isNull()) {
        image = applyNightMode(image);
    }

    return image;
}

void MainWindow::saveLastReadState() {
    if (currentFilePath.isEmpty()) return;

    QSettings settings("MyCompany", "BookReader");
    QString key = "lastState/" + currentFilePath;

    settings.setValue(key + "/page", currentPage);
    settings.setValue(key + "/zoom", zoom);
    settings.setValue(key + "/fitToWindow", fitToWindow);
    settings.setValue(key + "/nightMode", nightMode);
    settings.setValue(key + "/facingPagesMode", facingPagesMode);
    settings.setValue(key + "/continuousScrollMode", continuousScrollMode);

    // Save last opened file
    settings.setValue("lastOpenedFile", currentFilePath);
}

void MainWindow::loadLastReadState(const QString &filePath) {
    QSettings settings("MyCompany", "BookReader");
    QString key = "lastState/" + filePath;

    currentPage = settings.value(key + "/page", 0).toInt();
    zoom = settings.value(key + "/zoom", 1.0).toDouble();
    fitToWindow = settings.value(key + "/fitToWindow", true).toBool();
    nightMode = settings.value(key + "/nightMode", nightMode).toBool();
    facingPagesMode = settings.value(key + "/facingPagesMode", false).toBool();
    continuousScrollMode = settings.value(key + "/continuousScrollMode", false).toBool();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString file = urls.first().toLocalFile().toLower();
            if (file.endsWith(".pdf") || file.endsWith(".djvu")) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    QString filePath = urls.first().toLocalFile();
    if (filePath.isEmpty()) return;

    currentFilePath = filePath;

    if (filePath.endsWith(".djvu", Qt::CaseInsensitive)) {
        isPdf = false;
        openDjvuFile(filePath);
    } else if (filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        isPdf = true;
        openPdfFile(filePath);
    }

    event->acceptProposedAction();
}

