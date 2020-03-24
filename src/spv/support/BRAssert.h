//
//  BRAssert.h
//  BRCore
//
//  Created by Ed Gamble on 2/4/19.
//  Copyright © 2019 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//

#ifndef BRAssert_h
#define BRAssert_h

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BRAssert() is meant to replace assert() so as to provide a meaningful assert in 'release'
 * builds. Normally assert() is disabled in a 'release' build - the condition is evaluated and a
 * log message is produce, but the program doesn't exit.  Thus to program continues only to fail
 * later, unceremoniously killing the App in some way.  In Core code a failed assertion is truely
 * fatal - it is not meaningful for Core to continue.
 *
 * When `condition` is false(0), BRAssert() invokes __BRFail().
 *
 * @param condition if false, assert.
 */
#define BRAssert(condition)   \
    do { if (!(condition)) { __BRFail(__FILE__, __LINE__, #condition); } } while (0)

/**
 * BRFail() calls BRAssert(0) to surely fail Core.  (See above for BRAssert(condition)
 */
#define BRFail()              BRAssert(0)

/**
 * (__BRFail is effectively internal) Fail Core and provide some log output with the location.
 * This function is annotated to 'never return'
 */
extern void
__BRFail (const char *file, int line, const char *exp) __attribute__((__noreturn__));

typedef void *BRAssertInfo;
typedef void (*BRAssertHandler) (BRAssertInfo info);

/**
 * Install a handler for a BRAssert failure.  If `BRAssert(0)` occurs, then the provided handler
 * will be invokdd as `handler(info)`.  The handler runs in a pthread (not in a Unix signal
 * context) and thus it can do anything.
 *
 * Invocation of the handler implies that Core has failed.  The appropriate response is to
 * delete/release all Core resources (Bitcoin/Ethereum Wallets/Transfer) and to restart Core w/
 * a FULL-SYNC for all blockchains.
 *
 * BRAssertInstall() should be called before Core is used.  Thus before wallets, peer managers,
 * wallet managers, etc are created.
 *
 * If BRAssertInstall() is not called before BRFail() is invoked, then Core will fail and the App
 * will crash, as if `exit()` were invoked.
 *
 * Once BRAssertInstall() is called, it runs continuously - no matter how many failures and no
 * matter how many recoveries.  It thus need not be called again.  When the App will quit, a call
 * to BRAssertUninstall() should be made to cleanly stop.  If BRAssertInstall() needs to be called
 * again, say to install a different 'handler', then BRAssertUninstall() should be called.
 *
 * The `handle` CAN NOT call BRAssertUninstall() any of BRAssertRemoveRecovery().  Consequences
 * are undetermined (an exit(), a deadlock).
 *
 * @param info some handler context
 * @param handler some handler
 */
extern void
BRAssertInstall (BRAssertInfo info, BRAssertHandler handler);

/**
 * Uninstall BRAssert.  Will kill the pthread waiting on BRAssert failures.
 */
extern void
BRAssertUninstall (void);

/**
 * Return true (1) if BRAssert is installed, false (0) otherwise.

 @return true if connected
 */
extern int
BRAssertIsInstalled (void);

/// MARK: Private-ish

/**
 * Define recovery context and a recovery handler types
 */
typedef void *BRAssertRecoveryInfo;
typedef void (*BRAssertRecoveryHandler) (BRAssertRecoveryInfo info);

/**
 * Define a recovery handler.  On a BRAssert(0) the recovery handlers are invoked to clean up any
 * Core threads, essentially to shut down Core.  Multiple recovery handlers can be installed and
 * all will be run to completion before the BRAssertHandler is invoked.
 *
 * This function installs one handler for each `info` value.  If invoked multiple times with the
 * same `info`, then the handler will be replaced.
 *
 * This is `semi-private` because it should only be used internally for Core shutdown.  A typical
 * use would be for BRPeerManager or BREthereumEWM where the recovery handler would be
 * BRPeerManagerDisconnect() or BREthereumEWMDisconnect().  If some process invoked BRFail(), like
 * one BRPeer thread, then BRPeerManagerDisconnect would be called to stop the peer manager and all
 * remaining peer threads.
 *
 * Not every pthread needs to define a recovery; only the top-level thread(s), responsible for
 * all sub-threads need a recovery handler.  For example BREthereumEWM manages BREthereumBCS which
 * itself manages BREthereumLES.  A recovery for BREthereumEWM of 'disconnect' will disconnect
 * BCS which will disconnect LES - leading to a circumstance where all threads have stopped.
 *
 * Once all recoveries run there should be no Core threads running.
 *
 * The 'handler' cannot call BRAssertRemoveRecovery(); the consequences are undetermined.  If a
 * remove is necessary, do it in the clean up part of the pthread routine as:
 *
 * void *someThread (void *ignore) {
 *   BRAssertDefineRecovery (...)
 *
 *   while (!quit) { }
 *
 *   BRAssertRemoveRecovery (...)
 *   pthread_exit(0);
 * }
 *
 * @param info some handler context.
 * @param handler some handler
 */
extern void
BRAssertDefineRecovery (BRAssertRecoveryInfo info,
                        BRAssertRecoveryHandler handler);

/**
 * Remove the recovery handler associated with `info`.  This function CAN NOT be called by a
 * recovery handler.  See `BRAssertDefineRecover()` description.
 *
 * @param info
 *
 * @return true (1) if removed, false (0) if no handler for `info` existed.
 */
extern int
BRAssertRemoveRecovery (BRAssertRecoveryInfo info);

#ifdef __cplusplus
}
#endif

#include <stdio.h>

#endif /* BRAssert_h */
