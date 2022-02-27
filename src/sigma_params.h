#ifndef SIGMA_PARAMS_H
#define SIGMA_PARAMS_H

// Block after which sigma mints are activated.
#define ZC_SIGMA_STARTING_BLOCK         156111

// limit of coins number per id in spend v3.0
#define ZC_SPEND_V3_COINSPERID_LIMIT    16000

// number of mint confirmations needed to spend coin
#define ZC_MINT_CONFIRMATIONS               6

// Value of sigma spends allowed per block
#define ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK  (6666 * COIN)

// Amount of sigma spends allowed per block
#define ZC_SIGMA_INPUT_LIMIT_PER_BLOCK         100

// Value of sigma spends allowed per transaction
#define ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION     (6666 * COIN)

// Amount of sigma spends allowed per transaction
#define ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION            50

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

#endif
