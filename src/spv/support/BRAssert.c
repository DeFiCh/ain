//
//  BRAssert.c
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

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>         // sleep
#include <limits.h>         // UINT_MAX
#include <assert.h>
#include "BRAssert.h"
#include "BRArray.h"

#define PTHREAD_NULL            ((pthread_t) NULL)

#if defined(TARGET_OS_MAC)
#include <Foundation/Foundation.h>
#define assert_log(...) NSLog(__VA_ARGS__)
#elif defined(__ANDROID__)
#include <android/log.h>
#define assert_log(...) __android_log_print(ANDROID_LOG_INFO, "bread", __VA_ARGS__)
#else
#include <stdio.h>
#define assert_log(...) printf(__VA_ARGS__)
#endif

#define ASSERT_THREAD_NAME      "Core Assert Handler"
#define ASSERT_DEFAULT_RECOVERIES_COUNT         (5)

/**
 * A Recovery Context.  For example, used to invoke BRPeerManagerDisconnect on a BRPeerManager.
 */
typedef struct {
    BRAssertRecoveryInfo info;
    BRAssertRecoveryHandler handler;
} BRAssertRecoveryContext;


static void
BRAssertRecoveryInvoke (BRAssertRecoveryContext *context) {
    if (NULL != context->handler)
        context->handler (context->info);
}

/**
 * The Context - our assert handler context.
 */
typedef struct {
    BRAssertInfo info;
    BRAssertHandler handler;
    BRArrayOf(BRAssertRecoveryContext) recoveries;

    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;

    int timeToQuit;
} BRAssertContext;

/**
 * Allocate a singleton instance of BRAssertContext
 */
static BRAssertContext context = {
    NULL, NULL, NULL,
    PTHREAD_NULL,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    0
};

static void
BRAssertEnsureRecoveries (BRAssertContext *context) {
    if (NULL == context->recoveries)
        array_new (context->recoveries, ASSERT_DEFAULT_RECOVERIES_COUNT);
}

static void
BRAssertReleaseRecoveries (BRAssertContext *context) {
    if (NULL != context->recoveries) {
        array_free (context->recoveries);
        context->recoveries = NULL;
    }
}

/**
 * Invoke all the context's recoveries.
 */
static void
BRAssertInvokeRecoveries (BRAssertContext *context) {
    BRAssertEnsureRecoveries(context);
    size_t count = array_count(context->recoveries);
    for (size_t index = 0; index < count; index++)
        BRAssertRecoveryInvoke (&context->recoveries[index]);
}

/**
 * Invoke the context's handler or it it doesn't exist then optionally exit.
 */
static void
BRAssertInvokeHandler (BRAssertContext *context, int exitIfNoHandler) {
    if (NULL != context->handler)
        context->handler (context->info);
    else if (exitIfNoHandler) {
        assert_log ("AssertThread: no handler - will exit()\n");
        exit (EXIT_FAILURE);
    }
}


typedef void* (*ThreadRoutine) (void*);         // pthread_create

static void *
BRAssertThread (BRAssertContext *context) {
#if defined (__ANDROID__)
    pthread_setname_np (context->thread, ASSERT_THREAD_NAME);
#else
    pthread_setname_np (ASSERT_THREAD_NAME);
#endif

    pthread_mutex_lock(&context->lock);

    // Handle an immediate BRAssertUnintall if it happens before we even 'cond wait'
    if (!context->timeToQuit)
        // Wait forever on a 'cond signal'
        while (0 == pthread_cond_wait(&context->cond, &context->lock)) {
            if (context->timeToQuit) break;

            assert_log ("AssertThread: Caught\n");

            // Invoke recovery methods
            BRAssertInvokeRecoveries (context);

            // Invoke (top-level) handler.  If there is no handler, we will exit()
            BRAssertInvokeHandler(context, 1);
        }

    assert_log ("AssertThread: Quit\n");

    context->info = NULL;
    context->handler = NULL;
    BRAssertReleaseRecoveries(context);
    context->timeToQuit = 0;
    // Note: We must not clear context->thread; otherwise a pthread_join() won't have the thread.

    // Required as pthread_cont_wait() takes the lock.
    pthread_mutex_unlock(&context->lock);

    // Leave 'lock' and 'cond' untouched.  At pthread_join() we'll destroy them.

    // done
    pthread_exit(0);
}

extern void
BRAssertInstall (BRAssertInfo info, BRAssertHandler handler) {
    pthread_mutex_lock (&context.lock);
    if (PTHREAD_NULL != context.thread) { pthread_mutex_unlock(&context.lock); return; }

    context.info = info;
    context.handler = handler;
    BRAssertEnsureRecoveries (&context);
    context.timeToQuit = 0;

    {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_cond_init(&context.cond, &attr);
        pthread_condattr_destroy(&attr);
    }

    {
        // The cacheLock is a normal, non-recursive lock
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&context.lock, &attr);
        pthread_mutexattr_destroy(&attr);
    }


    {
        pthread_attr_t attr;
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);
        pthread_attr_setstacksize (&attr, 1024 * 1024);
        pthread_create (&context.thread, &attr, (ThreadRoutine) BRAssertThread, &context);
        pthread_attr_destroy(&attr);
    }

    pthread_mutex_unlock(&context.lock);
}

extern void
BRAssertUninstall (void) {
    pthread_mutex_lock (&context.lock);
    if (PTHREAD_NULL == context.thread) { pthread_mutex_unlock (&context.lock); return; }
    if (pthread_self() == context.thread) {
        assert_log ("%s:%u: BRAssertUninstall called within assert handler or assert recovery: exiting.\n",
                    __FILE__, __LINE__);
        exit (0);
    }

    // Keep
    pthread_t thread = context.thread;

    // Set this flag so that the assert handler thread *will avoid* running recoveries.
    context.timeToQuit = 1;

    // Signal the assert handler thread; it will wakeup, observe `timeToQuit` and then exit.
    pthread_cond_signal (&context.cond);

    // Allow the assert handler to run and quit.
    pthread_mutex_unlock (&context.lock);

    pthread_join (thread, NULL);

    // Only now are these safe to destroy and, for context.thread, safe to clear.
    pthread_mutex_destroy(&context.lock);
    pthread_cond_destroy (&context.cond);
    context.thread = PTHREAD_NULL;
}

extern int
BRAssertIsInstalled (void) {
    int isConnected = 0;

    pthread_mutex_lock (&context.lock);
    isConnected = PTHREAD_NULL != context.thread;
    pthread_mutex_unlock (&context.lock);

    return isConnected;
}

extern void
__BRFail (const char *file, int line, const char *exp) {
    assert_log ("%s:%u: failed assertion `%s'\n", file, line, exp);

    // If BRAssertInstall() has not been called, then there is no thread and we should exit().
    // But that requires testing context state (like context->thread), which requires a lock and
    // which results in a deadlock.
    //
    // The the mutex/lock defined to be recursive? Or just simple?

    // Signal the handler thread.
    pthread_cond_signal (&context.cond);
#if defined(DEBUG)
//    assert (0);
#endif

    // This thread is dead.
    pthread_exit (NULL);
}

extern void
BRAssertDefineRecovery (BRAssertRecoveryInfo info,
                        BRAssertRecoveryHandler handler) {
    int needRecovery = 1;

    pthread_mutex_lock (&context.lock);
    BRAssertEnsureRecoveries (&context);

    for (size_t index = 0; index < array_count(context.recoveries); index++)
        if (info == context.recoveries[index].info) {
            context.recoveries[index].handler = handler;
            needRecovery = 0;
            break; // for
        }

    if (needRecovery) {
        BRAssertRecoveryContext recovery = { info, handler };
        array_add (context.recoveries, recovery);
    }
    pthread_mutex_unlock(&context.lock);
}

extern int
BRAssertRemoveRecovery (BRAssertRecoveryInfo info) {
    int removedRecovery = 0;

    pthread_mutex_lock (&context.lock);
    BRAssertEnsureRecoveries (&context);

    for (size_t index = 0; index < array_count(context.recoveries); index++)
        if (info == context.recoveries[index].info) {
            array_rm(context.recoveries, index);
            removedRecovery = 1;
            break; // for
        }
    pthread_mutex_unlock (&context.lock);
    return removedRecovery;
}
