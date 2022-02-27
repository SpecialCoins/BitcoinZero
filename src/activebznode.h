// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEBZNODE_H
#define ACTIVEBZNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveBznode;

static const int ACTIVE_BZNODE_INITIAL          = 0; // initial state
static const int ACTIVE_BZNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_BZNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_BZNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_BZNODE_STARTED          = 4;

extern CActiveBznode activeBznode;

// Responsible for activating the Bznode and pinging the network
class CActiveBznode
{
public:
    enum bznode_type_enum_t {
        BZNODE_UNKNOWN = 0,
        BZNODE_REMOTE  = 1,
        BZNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    bznode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Bznode
    bool SendBznodePing();

public:
    // Keys for the active Bznode
    CPubKey pubKeyBznode;
    CKey keyBznode;

    // Initialized while registering Bznode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_BZNODE_XXXX
    std::string strNotCapableReason;

    CActiveBznode()
        : eType(BZNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyBznode(),
          keyBznode(),
          vin(),
          service(),
          nState(ACTIVE_BZNODE_INITIAL)
    {}

    /// Manage state of active Bznode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
