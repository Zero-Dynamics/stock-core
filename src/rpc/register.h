// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_RPCREGISTER_H
#define STOCK_RPCREGISTER_H

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
void RegisterMiningRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllCoreRPCCommands(CRPCTable &tableRPC)
{
    RegisterBlockchainRPCCommands(tableRPC);
    RegisterNetRPCCommands(tableRPC);
    RegisterMiscRPCCommands(tableRPC);
    RegisterRawTransactionRPCCommands(tableRPC);
    RegisterMiningRPCCommands(tableRPC);
}

#endif
