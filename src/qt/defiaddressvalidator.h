// Copyright (c) 2011-2014 The B_itcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_QT_DEFIADDRESSVALIDATOR_H
#define DEFI_QT_DEFIADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class DefiAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit DefiAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Defi address widget validator, checks for a valid defi address.
 */
class DefiAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit DefiAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // DEFI_QT_DEFIADDRESSVALIDATOR_H
