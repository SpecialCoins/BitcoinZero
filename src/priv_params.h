#ifndef BZX_PARAMS_H
#define BZX_PARAMS_H

/** Dust Hard Limit, ignored as wallet inputs (mininput default) */
static const int64_t DUST_HARD_LIMIT = 1000;   // 0.00001 BZX mininput

// limit of coins number per id in Lelantus
#define ZC_LELANTUS_MAX_MINT_NUM    65000
#define ZC_LELANTUS_SET_START_SIZE  16000

// Version of the block index enty that introduces evo sporks
#define EVOSPORK_MIN_VERSION                5140904

// Version of the block index entry that introduces Lelantus protocol
#define LELANTUS_PROTOCOL_ENABLEMENT_VERSION	5140904

// number of mint confirmations needed to spend coin
#define ZC_MINT_CONFIRMATIONS               2

/** Maximum number of outbound peers designated as Dandelion destinations */
#define DANDELION_MAX_DESTINATIONS 2

/** Expected time between Dandelion routing shuffles (in seconds). */
#define DANDELION_SHUFFLE_INTERVAL 600

/** The minimum amount of time a Dandelion transaction is embargoed (seconds) */
#define DANDELION_EMBARGO_MINIMUM 10

/** The average additional embargo time beyond the minimum amount (seconds) */
#define DANDELION_EMBARGO_AVG_ADD 20

/** Probability (percentage) that a Dandelion transaction enters fluff phase */
#define DANDELION_FLUFF 10

// Versions of privcoin mint/spend transactions
#define LELANTUS_TX_VERSION_4               40
#define LELANTUS_TX_VERSION_4_5             45
#define LELANTUS_TX_TPAYLOAD                47

#define PRIVCOIN_PUBLICKEY_TO_SERIALNUMBER  "PUBLICKEY_TO_SERIALNUMBER"

#endif
