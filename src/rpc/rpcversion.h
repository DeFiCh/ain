#ifndef DEFI_RPCVERSION_H
#define DEFI_RPCVERSION_H

/* RPC Version Build */
#define RPC_VERSION_BUILD 0

/* Major RPC version */
#define RPC_VERSION_MAJOR 1

/* Minor RPC version */
#define RPC_VERSION_MINOR 0

/* Build RPC revision */
#define RPC_VERSION_REVISION 0

/* RPC version */
static const int RPC_VERSION =
        1000000 * RPC_VERSION_MAJOR
        +   10000 * RPC_VERSION_MINOR
        +     100 * RPC_VERSION_REVISION
        +       1 * RPC_VERSION_BUILD;
#endif