// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "lookuptxdialog.h"
#include "ui_lookuptxdialog.h"

#include "exodus/errors.h"
#include "exodus/rpc.h"
#include "exodus/rpctxobject.h"

#include "exodus_qtutils.h"

#include "uint256.h"

#include <string>

#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

using std::string;
using namespace exodus;

LookupTXDialog::LookupTXDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LookupTXDialog)
{
    ui->setupUi(this);

#if QT_VERSION >= 0x040700
    ui->searchLineEdit->setPlaceholderText("Search transaction");
#endif

    // connect actions
    connect(ui->searchButton, SIGNAL(clicked()), this, SLOT(searchButtonClicked()));
}

LookupTXDialog::~LookupTXDialog()
{
    delete ui;
}

void LookupTXDialog::searchTX()
{
    // search function to lookup address
    string searchText = ui->searchLineEdit->text().toStdString();

    // first let's check if we have a searchText, if not do nothing
    if (searchText.empty()) return;

    uint256 hash;
    hash.SetHex(searchText);
    UniValue txobj(UniValue::VOBJ);
    std::string strTXText;
    // make a request to new RPC populator function to populate a transaction object
    int populateResult = populateRPCTransactionObject(hash, txobj, "", true);
    if (0<=populateResult) {
        strTXText = txobj.write(true);
        if (!strTXText.empty()) PopulateSimpleDialog(strTXText, "Transaction Information", "Transaction Information");
    } else {
        // show error message
        std::string strText = "The transaction hash entered is ";
        switch(populateResult) {
            case MP_TX_NOT_FOUND:
                strText += "not a valid BitcoinZero or Exodus transaction.  Please check the transaction hash "
                           "entered and try again.";
            break;
            case MP_TX_UNCONFIRMED:
                strText += "unconfirmed.  Toolbox lookup of transactions is currently only available for "
                           "confirmed transactions.\n\nTip: You can view your own outgoing unconfirmed "
                           "transactions in the transactions tab.";
            break;
            case MP_TX_IS_NOT_EXODUS_PROTOCOL:
                strText += "a BitcoinZero transaction only.\n\nTip: You can use the debug console "
                           "'gettransaction' command to lookup specific BitcoinZero transactions.";
            break;

            default:
                strText += "of an unknown type.  If you are seeing this message please raise a bug report "
                           "with the transaction hash at github.com/BitcoinZeroOfficial/BitcoinZero//issues.";
            break;
        }
        QString strQText = QString::fromStdString(strText);
        QMessageBox errorDialog;
        errorDialog.setIcon(QMessageBox::Critical);
        errorDialog.setWindowTitle("TXID error");
        errorDialog.setText(strQText);
        errorDialog.setStandardButtons(QMessageBox::Ok);
        errorDialog.setDefaultButton(QMessageBox::Ok);
        if(errorDialog.exec() == QMessageBox::Ok) { } // no other button to choose, acknowledged
    }
}

void LookupTXDialog::searchButtonClicked()
{
    searchTX();
}
