#include "searchdialog.h"

SearchDialog::SearchDialog(QWidget *parent)
    : QDialog(parent), edit(new QLineEdit), nextBtn(new QPushButton("Next")), prevBtn(new QPushButton("Previous")) {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle("Search");
    setModal(true);
    setFixedWidth(400);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(edit);
    layout->addWidget(prevBtn);
    layout->addWidget(nextBtn);

    connect(nextBtn, &QPushButton::clicked, this, &SearchDialog::searchNext);
    connect(prevBtn, &QPushButton::clicked, this, &SearchDialog::searchPrev);
    connect(edit, &QLineEdit::returnPressed, this, &SearchDialog::searchAll);
}

QString SearchDialog::searchText() const {
    return edit->text();
}
