// Copyright (c) 2015-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef DEFI_TORCONTROL_H
#define DEFI_TORCONTROL_H

#include <scheduler.h>

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

void StartTorControl();
void InterruptTorControl();
void StopTorControl();

#endif /* DEFI_TORCONTROL_H */
