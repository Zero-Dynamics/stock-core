// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_QT_STOCKADDRESSVALIDATOR_H
#define STOCK_QT_STOCKADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class StockAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit StockAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Stock address widget validator, checks for a valid stock address.
 */
class StockAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit StockAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // STOCK_QT_STOCKADDRESSVALIDATOR_H
