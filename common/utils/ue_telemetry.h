#ifndef UE_TELEMETRY_H
#define UE_TELEMETRY_H

#include <stdint.h>
#include <sys/un.h>
#include <sys/socket.h>

#define OAI_TELEMETRY_SOCKET_PATH "/tmp/oai_ue_telemetry.sock"
#define TELEMETRY_MAGIC 0xFECA0506

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint64_t timestamp_ns;

    // --- Identification ---
    uint16_t rnti;
    uint32_t frame;
    uint32_t slot;

    // --- PHY ---
    int16_t  ssb_rsrp_dbm; // beam
    int16_t  rssi_dbm;
    int16_t wideband_cqi_avg;
    int16_t  snr_db;

    // --- MAC BLER ---
    uint32_t dl_bler_ok;
    uint32_t dl_bler_err;
    uint32_t ul_bler_ok;
    uint32_t ul_bler_err;

} ue_telemetry_msg_t;

extern int g_telemetry_sock;
extern struct sockaddr_un g_telemetry_dest_addr;

#endif