// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZOXIDCONTROLDIALOG_H
#define ZOXIDCONTROLDIALOG_H

#include "primitives/zerocoin.h"
#include "privacydialog.h"
#include <QDialog>
#include <QTreeWidgetItem>

class CZerocoinMint;
class WalletModel;

namespace Ui {
class ZOxidControlDialog;
}

class ZOxidControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ZOxidControlDialog(QWidget *parent);
    ~ZOxidControlDialog();

    void setModel(WalletModel* model);

    static std::list<std::string> listSelectedMints;
    static std::list<CZerocoinMint> listMints;
    static std::vector<CZerocoinMint> GetSelectedMints();

private:
    Ui::ZOxidControlDialog *ui;
    WalletModel* model;
    PrivacyDialog* privacyDialog;

    void updateList();
    void updateLabels();

    enum {
        COLUMN_CHECKBOX,
        COLUMN_DENOMINATION,
        COLUMN_PUBCOIN,
        COLUMN_CONFIRMATIONS,
        COLUMN_ISSPENDABLE
    };

private slots:
    void updateSelection(QTreeWidgetItem* item, int column);
    void ButtonAllClicked();
};

#endif // ZOXIDCONTROLDIALOG_H
