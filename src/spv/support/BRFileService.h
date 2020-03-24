//
//  BRFileService.h
//  Core
//
//  Created by Richard Evers on 1/4/19.
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

#ifndef BRFileService_h
#define BRFileService_h

#include <stdlib.h>
#include "BRSet.h"
#include "BRInt.h"

//Both Bitcoin and Ethereum Wallet Managers include the ability to save and load peers, block,
//transactions and logs (for Ethereum) to the file system.  But, they both implement the file
//operations independently.  Pull out the implementation into BRFileService.
//
// Allow each WalletManager (Bitcoin, Bitcash, Ethereum) to create their own BRFileService (storing
// stuff in a subdirectory specific to the manager+network+whatever for a given base path).  Allow
// each WalletManager to further define 'persistent types' (peers, blocks, etc) saved.  Allow for
// a versioning system (at least on read; everything gets written/saved w/ the latest version).
// Allow an upgrade path from existing IOS/Andriod Sqlite3 databases.
//
typedef struct BRFileServiceRecord *BRFileService;

/// A context used in callbacks
typedef void* BRFileServiceContext;

typedef enum {
    FILE_SERVICE_IMPL,              // generally a fatal condition
    FILE_SERVICE_UNIX,              // something in the file system (fopen, fwrite, ... errorred)
    FILE_SERVICE_ENTITY             // entity read/write (parse/serialize) error
} BRFileServiceErrorType;

typedef struct {
    BRFileServiceErrorType type;
    union {
        struct {
            const char *reason;
        } impl;

        struct {
            int error;
        } unixerror;

        struct {
            const char *type;
            const char *reason;
        } entity;
    } u;
} BRFileServiceError;

typedef void
(*BRFileServiceErrorHandler) (BRFileServiceContext context,
                              BRFileService fs,
                              BRFileServiceError error);


/// This *must* be the same fixed size type forever.  It is uint8_t.
typedef uint8_t BRFileServiceVersion;

extern BRFileService
fileServiceCreate (const char *basePath,
                   const char *currency,
                   const char *network,
                   BRFileServiceContext context,
                   BRFileServiceErrorHandler handler);

extern void
fileServiceRelease (BRFileService fs);

extern void
fileServiceSetErrorHandler (BRFileService fs,
                            BRFileServiceContext context,
                            BRFileServiceErrorHandler handler);

/**
 * Load all entities of `type` adding each to `results`.  If there is an error then the
 * fileServices' error handler is invoked and 0 is returned
 *
 * @param fs The fileServie
 * @param results A BRSet within which to store the results.  The type stored in the BRSet must
 *     be consistent with type recovered from the file system.
 * @param type The type to restore
 * @param updateVersion If true (1) update old versions with newer ones.
 *
 * @return true (1) if success, false (0) otherwise;
 */
extern int
fileServiceLoad (BRFileService fs,
                 BRSet *results,
                 const char *type,   /* blocks, peers, transactions, logs, ... */
                 int updateVersion);

extern void /* error code? */
fileServiceSave (BRFileService fs,
                 const char *type,  /* block, peers, transactions, logs, ... */
                 const void *entity);     /* BRMerkleBlock*, BRTransaction, BREthereumTransaction, ... */

extern void
fileServiceRemove (BRFileService fs,
                   const char *type,
                   UInt256 identifier);

extern void
fileServiceClear (BRFileService fs,
                  const char *type);

extern void
fileServiceClearAll (BRFileService fs);

/**
 * A function type to produce an identifer from an entity.  The identifer must be constant for
 * a particulary entity through time.  The identifier is used to derive a filename (more generally
 * a path and filename).
 */
typedef UInt256
(*BRFileServiceIdentifier) (BRFileServiceContext context,
                            BRFileService fs,
                            const void* entity);

/**
 * A function type to read an entity from a byte array.  You own the entity.
 */
typedef void*
(*BRFileServiceReader) (BRFileServiceContext context,
                        BRFileService fs,
                        uint8_t *bytes,
                        uint32_t bytesCount);

/**
 * A function type to write an entity to a byte array.  You own the byte array.
 */
typedef uint8_t*
(*BRFileServiceWriter) (BRFileServiceContext context,
                        BRFileService fs,
                        const void* entity,
                        uint32_t *bytesCount);

/**
 * Define a 'type', such as {block, peer, transaction, logs, etc}, that is to be stored in the
 * file system.
 *
 * @param fs the file service
 * @param type the type
 * @param context an arbitrary value to be passed to the type-specific functions.
 * @param version the entity version handled by the type-specific functions.
 * @param identifier the function that produces the identifier
 * @param reader the function the produces an entity from a byte array
 * @param writer the function that produces a byte array from an entity.
 *
 * @return true (1) if success, false (0) otherwise
 */
extern int
fileServiceDefineType (BRFileService fs,
                       const char *type,
                       BRFileServiceVersion version,
                       BRFileServiceContext context,
                       BRFileServiceIdentifier identifier,
                       BRFileServiceReader reader,
                       BRFileServiceWriter writer);

extern int
fileServiceDefineCurrentVersion (BRFileService fs,
                                 const char *type,
                                 BRFileServiceVersion version);

#endif /* BRFileService_h */
