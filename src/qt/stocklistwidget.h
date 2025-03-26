// Copyright (c) 2019-2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCKLISTWIDGET_H
#define STOCKLISTWIDGET_H

#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

typedef std::function<bool(QString)> ValidatorFunc;

class StockListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StockListWidget(QWidget *parent, QString title, ValidatorFunc validator);

    QStringList getEntries();

private:
    QListWidget* listWidget;
    QLineEdit* addInput;
    QPushButton* removeBtn;
    QLabel* warningLbl;

    ValidatorFunc validatorFunc;

private Q_SLOTS:
    void onInsert();
    void onRemove();
    void onSelect(QListWidgetItem*);
};

#endif // STOCKLISTWIDGET_H
