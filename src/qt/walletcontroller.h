// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_QT_WALLETCONTROLLER_H
#define DEFI_QT_WALLETCONTROLLER_H

#include <qt/walletmodel.h>
#include <sync.h>

#include <map>
#include <memory>
#include <vector>

#include <QMessageBox>
#include <QMutex>
#include <QThread>

class OptionsModel;
class PlatformStyle;

namespace interfaces {
class Handler;
class Node;
} // namespace interfaces

class OpenWalletActivity;

/**
 * Controller between interfaces::Node, WalletModel instances and the GUI.
 */
class WalletController : public QObject
{
    Q_OBJECT

    WalletModel* getOrCreateWallet(std::unique_ptr<interfaces::Wallet> wallet);
    void removeAndDeleteWallet(WalletModel* wallet_model);

public:
    WalletController(interfaces::Node& node, const PlatformStyle* platform_style, OptionsModel* options_model, QObject* parent);
    ~WalletController();

    //! Returns wallet models currently open.
    std::vector<WalletModel*> getOpenWallets() const;

    //! Returns all wallet names in the wallet dir mapped to whether the wallet
    //! is loaded.
    std::map<std::string, bool> listWalletDir() const;

    OpenWalletActivity* openWallet(const std::string& name, QWidget* parent = nullptr);
    void closeWallet(WalletModel* wallet_model, QWidget* parent = nullptr);

Q_SIGNALS:
    void walletAdded(WalletModel* wallet_model);
    void walletRemoved(WalletModel* wallet_model);

    void coinsSent(WalletModel* wallet_model, SendCoinsRecipient recipient, QByteArray transaction);

private:
    QThread m_activity_thread;
    interfaces::Node& m_node;
    const PlatformStyle* const m_platform_style;
    OptionsModel* const m_options_model;
    mutable QMutex m_mutex;
    std::vector<WalletModel*> m_wallets;
    std::unique_ptr<interfaces::Handler> m_handler_load_wallet;

    friend class OpenWalletActivity;
};

class OpenWalletActivity : public QObject
{
    Q_OBJECT

public:
    OpenWalletActivity(WalletController* wallet_controller, const std::string& name);

public Q_SLOTS:
    void open();

Q_SIGNALS:
    void message(QMessageBox::Icon icon, const QString text);
    void finished();
    void opened(WalletModel* wallet_model);

private:
    WalletController* const m_wallet_controller;
    std::string const m_name;
};

#endif // DEFI_QT_WALLETCONTROLLER_H
