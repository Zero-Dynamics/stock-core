// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_QT_SENDCOINSENTRY_H
#define STOCK_QT_SENDCOINSENTRY_H

#include <stockunits.h>
#include <qt/walletmodel.h>

#include <QStackedWidget>
#include <QMessageBox>

class WalletModel;
class PlatformStyle;

namespace Ui {
    class SendCoinsEntry;
}

/**
 * A single entry in the dialog for sending stocks.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity.
 */
class SendCoinsEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit SendCoinsEntry(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SendCoinsEntry();

    void setModel(WalletModel *model);
    bool validate();
    SendCoinsRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCoinsRecipient &value);
    void setAddress(const QString &address);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
//    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

    void setTotalAmount(const CAmount& amount);
    void setTotalPrivateAmount(const CAmount& amount);

    CAmount totalAmount;
    CAmount totalPrivateAmount;

    int unit;

    bool fPrivate;

public Q_SLOTS:
    void clear();

Q_SIGNALS:
    void removeEntry(SendCoinsEntry *entry);
    void payAmountChanged();
    void subtractFeeFromAmountChanged();
    void privateOrPublicChanged(bool fPrivate);
    void openCoinControl();
    void customChangeChanged(QString);
    void coinControlChangeChecked(int state);

private Q_SLOTS:
    void deleteClicked();
    void on_payTo_textChanged(const QString &address);
    void on_addressBookButton_clicked();
    void updateDisplayUnit();
    void updateAddressBook();
    void fromChanged(int);
    void useFullAmount();
    void coinControlChangeCheckedSlot(int state);

private:
    SendCoinsRecipient recipient;
    Ui::SendCoinsEntry *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;

    bool updateLabel(const QString &address);
};

#endif // STOCK_QT_SENDCOINSENTRY_H
