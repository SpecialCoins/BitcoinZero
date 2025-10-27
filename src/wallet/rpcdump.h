#ifndef BZX_RPCDUMP_H
#define BZX_RPCDUMP_H

#include <univalue.h>

#include "rpcwallet.h"

UniValue dumpprivkey_bzx(const JSONRPCRequest& request);
UniValue dumpwallet_bzx(const JSONRPCRequest& request);
UniValue dumpprivkey(const JSONRPCRequest& request);
UniValue dumpsparkviewkey(const JSONRPCRequest& request);
UniValue importprivkey(const JSONRPCRequest& request);
UniValue importaddress(const JSONRPCRequest& request);
UniValue importpubkey(const JSONRPCRequest& request);
UniValue dumpwallet(const JSONRPCRequest& request);
UniValue importwallet(const JSONRPCRequest& request);
UniValue importprunedfunds(const JSONRPCRequest& request);
UniValue removeprunedfunds(const JSONRPCRequest& request);
UniValue importmulti(const JSONRPCRequest& request);

#endif // BZX_RPCDUMP_H