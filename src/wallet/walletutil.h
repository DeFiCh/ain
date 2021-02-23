// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_WALLET_WALLETUTIL_H
#define DEFI_WALLET_WALLETUTIL_H

#include <fs.h>

#include <string>
#include <vector>

//! Get the path of the wallet directory.
fs::path GetWalletDir();

//! Get wallets in wallet directory.
std::vector<fs::path> ListWalletDir();

//! The WalletLocation class provides wallet information.
class WalletLocation final
{
    std::string m_file_path;
    std::string m_name;
    fs::path m_path;

public:
    WalletLocation() = default;
    explicit WalletLocation(const std::string& name);

    //! Get wallet name.
    const std::string& GetName() const { return m_name; }

    //! Get wallet absolute path.
    const fs::path& GetPath() const { return m_path; }

    //! Get wallet absolute file path.
    const std::string& GetFilePath() const { return m_file_path; }

    //! Return whether the wallet exists.
    bool Exists() const;
};

#endif // DEFI_WALLET_WALLETUTIL_H
