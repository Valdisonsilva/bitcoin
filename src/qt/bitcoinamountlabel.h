// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_BITCOINAMOUNTLABEL_H
#define BITCOIN_QT_BITCOINAMOUNTLABEL_H

#include <amount.h>

#include <QLabel>
#include <QObject>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/**
 * Widget for displaying bitcoin amounts with privacy facilities
 */
class BitcoinAmountLabel : public QLabel
{
    Q_OBJECT

public:
    explicit BitcoinAmountLabel(QWidget* parent = nullptr);
    void setValue(const CAmount& amount, int unit, bool privacy);
};

#endif // BITCOIN_QT_BITCOINAMOUNTLABEL_H
