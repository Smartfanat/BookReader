#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);

    app.setStyleSheet(R"(
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
    )");

    QApplication::setOrganizationName("MyCompany");
    QApplication::setApplicationName("BookReader");

    MainWindow w;
    w.resize(1024, 768);
    w.show();
    return app.exec();
}
