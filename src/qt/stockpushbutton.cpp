// Copyright (c) 2019-2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stockpushbutton.h"

StockPushButton::StockPushButton(QString label) :
    QPushButton(label)
{
    this->setLayoutDirection(Qt::RightToLeft);
}

void StockPushButton::setBadge(int nValue)
{
    this->setIcon(nValue <= 0 ? QIcon() : getBadgeIcon(nValue));
}

void StockPushButton::paintEvent(QPaintEvent *e)
{
    QPushButton::paintEvent(e);
}

QIcon StockPushButton::getBadgeIcon(int nValue)
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0, 0));
    QPainter *p = new QPainter(&img);
    p->setBrush(QColor(0xff, 0x66, 0x66));
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    QFont f("courier new");
    f.setPixelSize(16);
    f.setBold(true);
    p->setPen(Qt::white);
    p->drawEllipse(img.rect());
    p->setFont(f);
    p->drawText(img.rect(), Qt::AlignCenter, nValue > 9 ? "9+" : QString::number(nValue));
    delete p;
    return QIcon(QPixmap::fromImage(img));
}
