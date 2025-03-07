// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletview.h>

#include <qt/addressbookpage.h>
#include <qt/askpassphrasedialog.h>
#include <qt/clientmodel.h>
#include <qt/communityfundpage.h>
#include <qt/daopage.h>
#include <qt/getaddresstoreceive.h>
#include <qt/guiutil.h>
#include <qt/stockgui.h>
#include <qt/optionsdialog.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/signverifymessagedialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>

#include <ui_interface.h>

#include <main.h>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

WalletView::WalletView(const PlatformStyle *platformStyle, QWidget *parent):
    QStackedWidget(parent),
    clientModel(0),
    walletModel(0),
    platformStyle(platformStyle)
{
    // Create tabs
    overviewPage = new OverviewPage(platformStyle);

    settingsPage = new OptionsDialog(platformStyle);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    exportButton->setDefault(true);
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);

    daoPage = new DaoPage(platformStyle);

    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    sendCoinsPage = new SendCoinsDialog(platformStyle);
    requestPaymentPage = new getAddressToReceive();

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    addWidget(overviewPage);
    addWidget(settingsPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(requestPaymentPage);
    addWidget(daoPage);

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Clicking on the progressBar on the overview will show the modaloverlay ui
    connect(overviewPage, &OverviewPage::outOfSyncWarningClicked, this, &WalletView::requestedSyncWarningInfo);

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(requestPaymentPage, SIGNAL(requestPayment()), this, SLOT(gotoReceiveCoinsPage()));
    connect(requestPaymentPage, SIGNAL(requestAddressHistory()), this, SLOT(requestAddressHistory()));

    connect(daoPage, SIGNAL(daoEntriesChanged(int)), this, SLOT(onDaoEntriesChanged(int)));
}

WalletView::~WalletView()
{
}

void WalletView::setStockGUI(StockGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionTxStatusChanged(bool)), gui, SLOT(setEncryptionTxStatus(bool)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString,int,CAmount,QString,QString,QString)), gui, SLOT(incomingTransaction(QString,int,CAmount,QString,QString,QString)));
    }
}

void WalletView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;

    overviewPage->setClientModel(clientModel);
    sendCoinsPage->setClientModel(clientModel);
    daoPage->setClientModel(clientModel);

    settingsPage->setModel(clientModel->getOptionsModel());
}

void WalletView::requestAddressHistory()
{
    Q_EMIT openAddressHistory();
}

void WalletView::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;

    // Put transaction list in tabs
    transactionView->setModel(walletModel);
    overviewPage->setWalletModel(walletModel);
    daoPage->setWalletModel(walletModel);
    receiveCoinsPage->setModel(walletModel);
    requestPaymentPage->setModel(walletModel);
    requestPaymentPage->showPrivateAddress(0);
    sendCoinsPage->setModel(walletModel);
    usedReceivingAddressesPage->setModel(walletModel->getAddressTableModel());
    usedSendingAddressesPage->setModel(walletModel->getAddressTableModel());

    if (walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

        // Handle changes in encryption status
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // Handle changes in encryption status
        connect(walletModel, SIGNAL(encryptionTxStatusChanged(bool)), this, SIGNAL(encryptionTxStatusChanged(bool)));
        updateEncryptionTxStatus();

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(processNewTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Show progress dialog
        connect(walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        if (walletModel->NeedsBLSCTGeneration())
        {
            GenerateBLSCT();
        }
    }
}

void WalletView::GenerateBLSCT()
{
    if(!walletModel)
        return;

    QMessageBox::information(this, tr("Generation of x0DYNS keys"),
        tr("In order to generate your x0DYNS keys, you will be asked to unlock your wallet"));

    bool fShouldLockAfter = false;

    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();

        fShouldLockAfter = true;
    }

    if (!walletModel->GenerateBLSCT())
    {
        QMessageBox::information(this, tr("Generation of x0DYNS keys"),
            tr("x0DYNS keys could not be generated."));
    }
    else
    {
        QMessageBox::information(this, tr("Generation of x0DYNS keys"),
            tr("x0DYNS keys have been generated."));
    }

    if (fShouldLockAfter)
    {
        walletModel->setWalletLocked(true);
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QModelIndex index = ttm->index(start, 0, parent);
    QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
    QString label = ttm->data(index, TransactionTableModel::LabelRole).toString();

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label);
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
    daoPage->setActive(false);
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
    daoPage->setActive(false);
}

void WalletView::gotoSettingsPage()
{
    // We need to update the settings if it was modified externally
    // This fixes a bug where coin control was enabled on the send page
    // but was not shown as enabled on the setting spage
    settingsPage->setModel(this->clientModel->getOptionsModel());

    setCurrentWidget(settingsPage);
    daoPage->setActive(false);
}

void WalletView::gotoCommunityFundPage()
{
    // Change to CommunityFund UI
    setCurrentWidget(daoPage);
    daoPage->setActive(true);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
    daoPage->setActive(false);
}

void WalletView::gotoRequestPaymentPage(){
    requestPaymentPage->showPrivateAddress(0);
    setCurrentWidget(requestPaymentPage);
    daoPage->setActive(false);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);

    daoPage->setActive(false);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);

    daoPage->setActive(false);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::updateEncryptionTxStatus()
{
    Q_EMIT encryptionTxStatusChanged(walletModel->getEncryptionTxStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::encryptTx()
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(AskPassphraseDialog::EncryptTx, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionTxStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->backupWallet(filename)) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::setStakingStats(QString day, QString week, QString month, QString year, QString all)
{
    overviewPage->setStakingStats(day,week,month,year,all);
}

void WalletView::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::splitRewards()
{
    SplitRewardsDialog dlg(this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::exportMasterPrivateKeyAction()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if(!ctx.isValid())
    {
      // Unlock wallet was cancelled
      return;
    }

    CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
     if (!pwalletMain->IsHDEnabled())
     {
         QMessageBox::critical(0, tr(PACKAGE_NAME),
             tr("Wallet is not a HD wallet."));
     }
     CKey key;
     if (pwalletMain->GetKey(masterKeyID, key))
     {
         CExtKey masterKey;
         masterKey.SetMaster(key.begin(), key.size());

         CStockExtKey b58extkey;
         b58extkey.SetKey(masterKey);

         QMessageBox::information(this, tr("Show Master Private Key"),
             tr("Master Private Key:<br><br>%1").arg(QString::fromStdString(b58extkey.ToString())));
     }
     else
     {
         QMessageBox::critical(0, tr(PACKAGE_NAME),
             tr("Unable to retrieve HD master private key"));
     }

}

void WalletView::exportMnemonicAction()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        return;
    }

    CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
    if (!pwalletMain->IsHDEnabled())
    {
        QMessageBox::critical(0, tr(PACKAGE_NAME),
                tr("Wallet is not a HD wallet."));
    }

    CKey key;
    if (pwalletMain->GetKey(masterKeyID, key))
    {
        std::vector<unsigned char> keyData;
        const unsigned char* ptrKeyData = key.begin();
        for (int i = 0; ptrKeyData != key.end(); i++) {
            unsigned char byte = *ptrKeyData;
            keyData.push_back(byte);
            ptrKeyData++;
        }

        std::string mnemonic;

        // TODO: Add language support for the key
        //       Currently will only export the key in english
        word_list mnemonic_words = create_mnemonic(keyData, string_to_lexicon("english"));
        for (auto it = mnemonic_words.begin(); it != mnemonic_words.end();) {
            const auto word = *it;
            mnemonic += word;
            ++it;
            if (it == mnemonic_words.end())
                break;
            mnemonic += " ";
        }

        QMessageBox::information(this, tr("Show Mnemonic"),
                tr("Mnemonic:<br><br>%1").arg(QString::fromStdString(mnemonic)));
    } else // Could not get the master key
    {
        QMessageBox::critical(0, tr(PACKAGE_NAME),
                tr("Unable to retrieve mnemonic"));
    }
}

void WalletView::importPrivateKey()
{
    bool ok;
    QString privKey = QInputDialog::getText(this, tr("Import Private Key"),
                                            tr("Private Key:"), QLineEdit::Normal,
                                            "", &ok);
    if (ok && !privKey.isEmpty())
    {
      LOCK2(cs_main, pwalletMain->cs_wallet);

      WalletModel::UnlockContext ctx(walletModel->requestUnlock());
      if(!ctx.isValid())
      {
        // Unlock wallet was cancelled
        return;
      }

      CStockSecret vchSecret;
      bool fGood = vchSecret.SetString(privKey.toStdString());

      if (!fGood)
      {
          QMessageBox::critical(0, tr(PACKAGE_NAME),
              tr("Invalid private key encoding."));
          return;
      }

      CKey key = vchSecret.GetKey();
      if (!key.IsValid())
      {
          QMessageBox::critical(0, tr(PACKAGE_NAME),
              tr("Private key outside allowed range."));
          return;
      }

      CPubKey pubkey = key.GetPubKey();
      assert(key.VerifyPubKey(pubkey));
      CKeyID vchAddress = pubkey.GetID();
      {
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, "", "receive");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
        {
            QMessageBox::critical(0, tr(PACKAGE_NAME),
                tr("Address already added."));
            return;
        }

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
        {
            QMessageBox::critical(0, tr(PACKAGE_NAME),
                tr("Error adding key to wallet."));
            return;
        }

        QMessageBox::information(0, tr(PACKAGE_NAME),
            tr("Stock needs to scan the chain... Please, wait."));

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);

      }

      QMessageBox::information(0, tr(PACKAGE_NAME),
          tr("Private key correctly added!"));
      return;

    }
}

void WalletView::unlockWalletStaking()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::UnlockStaking, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;

    usedSendingAddressesPage->show();
    usedSendingAddressesPage->raise();
    usedSendingAddressesPage->activateWindow();
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;

    usedReceivingAddressesPage->show();
    usedReceivingAddressesPage->raise();
    usedReceivingAddressesPage->activateWindow();
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
        progressDialog->setRange(0,100);
    }
    else if (nProgress == -1)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setRange(0,0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void WalletView::requestedSyncWarningInfo()
{
    Q_EMIT outOfSyncWarningClicked();
}

void WalletView::onDaoEntriesChanged(int count)
{
    Q_EMIT daoEntriesChanged(count);
}
