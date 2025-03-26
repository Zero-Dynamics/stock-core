// Copyright (c) 2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWAPX0DYNS_H
#define SWAPX0DYNS_H

#include "aggregationsession.h"
#include "stockunits.h"
#include "optionsmodel.h"
#include "stockamountfield.h"
#include "blsct/key.h"
#include "wallet/wallet.h"
#include "clientmodel.h"
#include "walletmodel.h"

#include <QApplication>
#include <QPixmap>
#include <QBitmap>
#include <QPushButton>
#include <QPainter>
#include <QSpacerItem>
#include <QMessageBox>
#include <QDialog>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

#define DEFAULT_UNIT 0

class SwapX0DYNSDialog : public QDialog
{
    Q_OBJECT

public:
    SwapX0DYNSDialog(QWidget *parent);

    void setModel(WalletModel *model);
    void setClientModel(ClientModel *model);
    void SetPublicBalance(CAmount a);
    void SetPrivateBalance(CAmount a);

private:
    CAmount publicBalance;
    CAmount privateBalance;

    bool fMode;

    WalletModel *model;
    ClientModel *clientModel;
    QVBoxLayout* layout;
    QLabel *label1;
    QLabel *label2;
    QLabel *toplabel1;
    QLabel *toplabel2;
    QPushButton *swapButton;
    StockAmountField *amount;

    QLabel *icon1;
    QLabel *icon2;

private Q_SLOTS:
    void Swap();
    void Ok();
};

#endif // SWAPX0DYNS_H
