/* Copyright (c) 2020-2021, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * @file hs_metrics_entry.c
 * @brief Defines the metrics entry that are collected by an onion service.
 **/

#define HS_METRICS_ENTRY_PRIVATE

#include <stddef.h>

#include "orconfig.h"

#include "lib/cc/compat_compiler.h"
#include "lib/log/log.h"
#include "lib/log/util_bug.h"

#include "feature/hs/hs_metrics_entry.h"

/* Histogram time buckets (in milliseconds). */
static const int64_t hs_metrics_circ_build_time_buckets[] =
{
  1000,  /* 1s */
  5000,  /* 5s */
  10000, /* 10s */
  30000, /* 30s */
  60000  /* 60s */
};

#define hs_metrics_circ_build_time_buckets_size ARRAY_LENGTH(hs_metrics_circ_build_time_buckets)

/** The base metrics that is a static array of metrics that are added to every
 * single new stores.
 *
 * The key member MUST be also the index of the entry in the array. */
const hs_metrics_entry_t base_metrics[] =
{
  {
    .key = HS_METRICS_NUM_INTRODUCTIONS,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_intro_num_total),
    .help = "Total number of introduction received",
    .port_as_label = false,
  },
  {
    .key = HS_METRICS_APP_WRITE_BYTES,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_app_write_bytes_total),
    .help = "Total number of bytes written to the application",
    .port_as_label = true,
  },
  {
    .key = HS_METRICS_APP_READ_BYTES,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_app_read_bytes_total),
    .help = "Total number of bytes read from the application",
    .port_as_label = true,
  },
  {
    .key = HS_METRICS_NUM_ESTABLISHED_RDV,
    .type = METRICS_TYPE_GAUGE,
    .name = METRICS_NAME(hs_rdv_established_count),
    .help = "Total number of established rendezvous circuits",
  },
  {
    .key = HS_METRICS_NUM_RDV,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_rdv_num_total),
    .help = "Total number of rendezvous circuits created",
  },
  {
    .key = HS_METRICS_NUM_FAILED_RDV,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_rdv_error_count),
    .help = "Total number of rendezvous circuit errors",
  },
  {
    .key = HS_METRICS_NUM_ESTABLISHED_INTRO,
    .type = METRICS_TYPE_GAUGE,
    .name = METRICS_NAME(hs_intro_established_count),
    .help = "Total number of established introduction circuit",
  },
  {
    .key = HS_METRICS_NUM_REJECTED_INTRO_REQ,
    .type = METRICS_TYPE_COUNTER,
    .name = METRICS_NAME(hs_intro_rejected_intro_req_count),
    .help = "Total number of rejected introduction circuits",
  },
  {
    .key = HS_METRICS_INTRO_CIRC_BUILD_TIME,
    .type = METRICS_TYPE_HISTOGRAM,
    .name = METRICS_NAME(hs_intro_circ_build_time),
    .buckets = hs_metrics_circ_build_time_buckets,
    .bucket_count = hs_metrics_circ_build_time_buckets_size,
    .help = "The introduction circuit build time in milliseconds",
  },
  {
    .key = HS_METRICS_REND_CIRC_BUILD_TIME,
    .type = METRICS_TYPE_HISTOGRAM,
    .name = METRICS_NAME(hs_rend_circ_build_time),
    .buckets = hs_metrics_circ_build_time_buckets,
    .bucket_count = hs_metrics_circ_build_time_buckets_size,
    .help = "The rendezvous circuit build time in milliseconds",
  },
};

/** Size of base_metrics array that is number of entries. */
const size_t base_metrics_size = ARRAY_LENGTH(base_metrics);

/** Possible values for the reason label of the
 * hs_intro_rejected_intro_req_count metric. */
const char *hs_metrics_intro_req_error_reasons[] =
{
  HS_METRICS_ERR_INTRO_REQ_BAD_AUTH_KEY,
  HS_METRICS_ERR_INTRO_REQ_INTRODUCE2,
  HS_METRICS_ERR_INTRO_REQ_SUBCREDENTIAL,
  HS_METRICS_ERR_INTRO_REQ_INTRODUCE2_REPLAY,
};

/** The number of entries in the hs_metrics_intro_req_error_reasons array. */
const size_t hs_metrics_intro_req_error_reasons_size =
    ARRAY_LENGTH(hs_metrics_intro_req_error_reasons);

/** Possible values for the reason label of the hs_rdv_error_count metric. */
const char *hs_metrics_rend_error_reasons[] =
{
  HS_METRICS_ERR_RDV_RP_CONN_FAILURE,
  HS_METRICS_ERR_RDV_PATH,
  HS_METRICS_ERR_RDV_RENDEZVOUS1,
  HS_METRICS_ERR_RDV_E2E,
  HS_METRICS_ERR_RDV_RETRY,
};

/** The number of entries in the hs_metrics_rend_error_reasons array. */
const size_t hs_metrics_rend_error_reasons_size =
    ARRAY_LENGTH(hs_metrics_rend_error_reasons);
