// Copyright (c) 2019-2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCKPUSHBUTTON_H
#define STOCKPUSHBUTTON_H

#include <QIcon>
#include <QPushButton>
#include <QPainter>
#include <QString>
#include <QWidget>

class StockPushButton : public QPushButton
{
    Q_OBJECT

public:
    StockPushButton(QString label);
    void paintEvent(QPaintEvent*);
    void setBadge(int nValue);

private:
    QIcon getBadgeIcon(int nValue);
};

#endif // STOCKPUSHBUTTON_H
