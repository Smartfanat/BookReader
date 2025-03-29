#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>

class SearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit SearchDialog(QWidget *parent = nullptr);

    QString searchText() const;

signals:
    void searchNext();
    void searchPrev();
    void searchAll();

private:
    QLineEdit *edit;
    QPushButton *nextBtn;
    QPushButton *prevBtn;
};
