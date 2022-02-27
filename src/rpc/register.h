// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCREGISTER_H
#define BITCOIN_RPCREGISTER_H

#if defined(HAVE_CONFIG_H)
#include "../config/bitcoin-config.h"
#endif

#include "util.h"
#include "../exodus/exodus.h"

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);

/** Register Exodus data retrieval RPC commands */
void RegisterExodusDataRetrievalRPCCommands(CRPCTable &tableRPC);
#ifdef ENABLE_WALLET
/** Register Exodus transaction creation RPC commands */
void RegisterExodusTransactionCreationRPCCommands(CRPCTable &tableRPC);
#endif
/** Register Exodus payload creation RPC commands */
void RegisterExodusPayloadCreationRPCCommands(CRPCTable &tableRPC);
/** Register Exodus raw transaction RPC commands */
void RegisterExodusRawTransactionRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllCoreRPCCommands(CRPCTable &tableRPC)
{
    RegisterBlockchainRPCCommands(tableRPC);
    RegisterNetRPCCommands(tableRPC);
    RegisterMiscRPCCommands(tableRPC);
    RegisterMiningRPCCommands(tableRPC);
    RegisterRawTransactionRPCCommands(tableRPC);

    if (isExodusEnabled()) {
        RegisterExodusDataRetrievalRPCCommands(tableRPC);
        RegisterExodusPayloadCreationRPCCommands(tableRPC);
        RegisterExodusRawTransactionRPCCommands(tableRPC);

#ifdef ENABLE_WALLET
        RegisterExodusTransactionCreationRPCCommands(tableRPC);
#endif
    }
}

#endif
