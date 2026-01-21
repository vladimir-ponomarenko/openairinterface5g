/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 * contact@openairinterface.org
 */

/*! \file ue_telemetry.h
 * \brief Telemetry subsystem for 5G UE metrics export to WebUI
 * \author Vladimir-Ponomarenko
 * \date 2025
 * \version 0.1
 * \note
 * \warning
 */

#ifndef UE_TELEMETRY_H
#define UE_TELEMETRY_H

#include <stdint.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "common/utils/threadPool/notified_fifo.h"
#include "common/utils/LOG/log.h"
#include "PHY/defs_nr_UE.h"

#define OAI_TELEMETRY_SOCKET_PATH "/tmp/oai_ue_telemetry.sock"
#define TELEMETRY_MAGIC 0xFECA0506
#define MAX_HARQ_ROUNDS 4
#define TEL_MAX_BEAMS 4

extern PHY_VARS_NR_UE ***PHY_vars_UE_g;

/*! \struct ue_telemetry_msg_t
 *  \brief Telemetry structure.
 *  \note Packed attribute ensures consistent binary layout for receiver.
 */
typedef struct __attribute__((packed)) {
  uint32_t magic; ///< Magic verification number
  uint64_t timestamp_ns; ///< Timestamp in nanoseconds

  // --- Identification ---
  uint16_t rnti; ///< C-RNTI
  uint32_t frame; ///< System Frame Number
  uint32_t slot; ///< Slot Number
  uint16_t phys_cell_id;       ///< Physical Cell ID

  // --- PHY ---
  int16_t ssb_rsrp_dbm; ///< RSRP of the synchronized SSB
  int16_t rssi_dbm; ///< Received Signal Strength Indicator
  int16_t wideband_cqi_avg; ///< Average Wideband CQI
  int16_t snr_db; ///< Signal-to-Noise Ratio
  int16_t  n0_power_tot_dbm;   ///< Total estimated noise power (dBm)
  uint8_t  rank;               ///< Rank indication
  uint8_t  nb_antennas_rx;     ///< Number of RX antennas
  uint32_t rx_total_gain_db; ///< Total gain of the RX chain
  int32_t  n_ta_offset;        ///< Timing advance offset in TDD
  int16_t  ssb_rsrp_beams[TEL_MAX_BEAMS]; ///< RSRP for first 4 beams
  float    ssb_sinr_beams[TEL_MAX_BEAMS]; ///< SINR for first 4 beams

  // --- MAC BLER ---
  uint32_t dl_bler_ok; ///< Successful DL TBs
  uint32_t dl_bler_err; ///< Erroneous DL TBs
  uint32_t ul_bler_ok; ///< Successful UL TBs
  uint32_t ul_bler_err; ///< Erroneous UL TBs
  uint32_t bad_dci; ///< Cumulated Bad DCI count
  float ul_avg_code_rate; ///< Average UL code rate
  float ul_avg_bps; ///< Average bits per symbol
  float ul_avg_rb_per_tb; ///< Average RBs per TB
  float ul_avg_sym_per_tb; ///< Average symbols per TB

} ue_telemetry_msg_t;

/*! \struct ue_telemetry_context_t
 *  \brief Internal state for the telemetry subsystem.
 */
typedef struct {
  notifiedFIFO_t telemetry_fifo; ///< Thread-safe queue between MAC and Worker
  pthread_t thread_id; ///< Worker thread ID
  int socket_fd; ///< UDP/Unix socket descriptor
  struct sockaddr_un dest_addr; ///< Target address
  bool initialized; ///< Flag to prevent double init
} ue_telemetry_context_t;

// Singleton instance
static ue_telemetry_context_t g_telemetry_ctx = {0};

/*! \brief Worker thread function (Consumer).
 *  \details Waits for messages in FIFO and sends them via Socket.
 *           If FIFO is empty, this thread sleeps.
 */
static void *telemetry_thread_entry(void *arg)
{
  (void)arg;
  pthread_setname_np(pthread_self(), "UE_Telemetry");

  while (1) {
    notifiedFIFO_elt_t *elt = pullNotifiedFIFO(&g_telemetry_ctx.telemetry_fifo);

    if (elt == NULL) {
      break;
    }

    ue_telemetry_msg_t *msg = (ue_telemetry_msg_t *)elt->msgData;
    if (g_telemetry_ctx.socket_fd >= 0) {
      sendto(g_telemetry_ctx.socket_fd,
             msg,
             sizeof(ue_telemetry_msg_t),
             MSG_DONTWAIT,
             (struct sockaddr *)&g_telemetry_ctx.dest_addr,
             sizeof(g_telemetry_ctx.dest_addr));
    }

    delNotifiedFIFO_elt(elt);
  }
  return NULL;
}

/*! \brief Initializes the telemetry subsystem.
 *  \details Should be called once from main() or lazy-loaded.
 */
static void init_telemetry_subsystem(void)
{
  if (g_telemetry_ctx.initialized)
    return;

  initNotifiedFIFO(&g_telemetry_ctx.telemetry_fifo);

  g_telemetry_ctx.socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (g_telemetry_ctx.socket_fd < 0) {
    LOG_E(NR_MAC, "[Telemetry] Failed to create socket: %s\n", strerror(errno));
    return;
  }

  memset(&g_telemetry_ctx.dest_addr, 0, sizeof(g_telemetry_ctx.dest_addr));
  g_telemetry_ctx.dest_addr.sun_family = AF_UNIX;
  strncpy(g_telemetry_ctx.dest_addr.sun_path, OAI_TELEMETRY_SOCKET_PATH, sizeof(g_telemetry_ctx.dest_addr.sun_path) - 1);

  if (pthread_create(&g_telemetry_ctx.thread_id, NULL, telemetry_thread_entry, NULL) != 0) {
    LOG_E(NR_MAC, "[Telemetry] Failed to create thread\n");
    close(g_telemetry_ctx.socket_fd);
    g_telemetry_ctx.socket_fd = -1;
    return;
  }

  g_telemetry_ctx.initialized = true;
  LOG_I(NR_MAC, "[Telemetry] Subsystem initialized. Worker thread running.\n");
}

/*! \brief Collects metrics and pushes to FIFO (Producer).
 *  \details This function is executed in the MAC layer.
 *           It performs ONLY memory copy operations. No I/O.
 */
static void queue_telemetry_packet(NR_UE_MAC_INST_t *mac, int frame, int slot)
{
  if (!g_telemetry_ctx.initialized) {
    init_telemetry_subsystem();
  }

  notifiedFIFO_elt_t *elt = newNotifiedFIFO_elt(sizeof(ue_telemetry_msg_t), 0, NULL, NULL);
  if (!elt) {
    LOG_E(NR_MAC, "[Telemetry] FIFO allocation failed\n");
    return;
  }

  ue_telemetry_msg_t *msg = (ue_telemetry_msg_t *)elt->msgData;

  msg->magic = TELEMETRY_MAGIC;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  msg->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;

  msg->rnti = mac->crnti;
  msg->frame = frame;
  msg->slot = slot;

  // --- PHY Metrics ---
  // Single UE instance (idx [0][0]).
  if (PHY_vars_UE_g && PHY_vars_UE_g[mac->ue_id] && PHY_vars_UE_g[mac->ue_id][0]) {
    PHY_VARS_NR_UE *phy = PHY_vars_UE_g[mac->ue_id][0];
    PHY_NR_MEASUREMENTS *m = &phy->measurements;
    msg->ssb_rsrp_dbm = (int16_t)m->ssb_rsrp_dBm[0];
    msg->rssi_dbm = (int16_t)m->rx_rssi_dBm[0];
    msg->wideband_cqi_avg = (int16_t)m->wideband_cqi_avg[0];
    msg->snr_db = (int16_t)m->ssb_sinr_dB[0];
    msg->phys_cell_id = phy->frame_parms.Nid_cell;
    msg->n0_power_tot_dbm = m->n0_power_tot_dBm;
    msg->rank = m->rank[0];
    msg->nb_antennas_rx = m->nb_antennas_rx;
    msg->rx_total_gain_db = phy->rx_total_gain_dB;
    msg->n_ta_offset = phy->N_TA_offset;
    for (int i = 0; i < TEL_MAX_BEAMS; i++) {
        msg->ssb_rsrp_beams[i] = (int16_t)m->ssb_rsrp_dBm[i];
        msg->ssb_sinr_beams[i] = m->ssb_sinr_dB[i];
    }
  } else {
    msg->ssb_rsrp_dbm = 0;
    msg->rssi_dbm = 0;
    msg->wideband_cqi_avg = 0;
    msg->snr_db = 0;
  }

  // --- MAC Metrics ---
  msg->dl_bler_ok = mac->stats.dl.rounds[0];
  msg->dl_bler_err = 0;
  for (int i = 1; i < MAX_HARQ_ROUNDS; i++) {
    msg->dl_bler_err += mac->stats.dl.rounds[i];
  }

  msg->ul_bler_ok = mac->stats.ul.rounds[0];
  msg->ul_bler_err = 0;
  for (int i = 1; i < MAX_HARQ_ROUNDS; i++) {
    msg->ul_bler_err += mac->stats.ul.rounds[i];
  }

  msg->bad_dci = mac->stats.bad_dci;

  float nbul = 0;
  for (int i = 0; i < MAX_HARQ_ROUNDS; i++) {
    nbul += mac->stats.ul.rounds[i];
  }
  if (nbul < 1)
    nbul = 1;

  // Average Code Rate
  if (mac->stats.ul.total_bits > 0) {
    msg->ul_avg_code_rate = (float)mac->stats.ul.target_code_rate / (mac->stats.ul.total_bits * 1024 * 10);
  } else {
    msg->ul_avg_code_rate = 0.0f;
  }

  // Bits per Symbol
  if (mac->stats.ul.total_symbols > 0) {
    msg->ul_avg_bps = (float)mac->stats.ul.total_bits / mac->stats.ul.total_symbols;
  } else {
    msg->ul_avg_bps = 0.0f;
  }

  // Avg per TB
  msg->ul_avg_rb_per_tb = (float)mac->stats.ul.rb_size / nbul;
  msg->ul_avg_sym_per_tb = (float)mac->stats.ul.nr_of_symbols / nbul;

  pushNotifiedFIFO(&g_telemetry_ctx.telemetry_fifo, elt);
}

#endif // UE_TELEMETRY_H