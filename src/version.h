// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */

static const int PROTOCOL_VERSION = 5141006;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 5140904;

//! disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = 5141000;
//! disconnect from peers older than this proto version2
static const int MIN_PEER_PROTO_VERSION2 = 5141003;

#endif // BITCOIN_VERSION_H
