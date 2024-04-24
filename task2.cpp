#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h>
#include "node-id.h"

#define NUM_SEND 2
#define MAX_READINGS 10

typedef struct {
    unsigned long src_id;
    unsigned long timestamp;
    unsigned long seq;
    uint16_t light_readings[MAX_READINGS];
    uint8_t num_readings;
} data_packet_struct;

static struct rtimer rt;
static struct pt pt;
static data_packet_struct data_packet;
unsigned long curr_timestamp;
linkaddr_t dest_addr;

#define RSSI_THRESHOLD -70

int is_good_link_quality(int rssi) {
    return rssi >= RSSI_THRESHOLD;
}

void transfer_light_readings() {
    printf("%lu TRANSFER %ld\n", curr_timestamp / CLOCK_SECOND, dest_addr.u8[0]);
    printf("Light: ");
    for(int i = 0; i < data_packet.num_readings; i++) {
        printf("%u%s", data_packet.light_readings[i], (i == data_packet.num_readings - 1) ? "\n" : ", ");
    }
}

void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
    if(len == sizeof(data_packet)) {
        static data_packet_struct received_packet_data;
        memcpy(&received_packet_data, data, len);
        printf("Received neighbour discovery packet %lu with rssi %d from %ld\n", received_packet_data.seq, (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), received_packet_data.src_id);
        // Check if link quality is good
        if(is_good_link_quality(packetbuf_attr(PACKETBUF_ATTR_RSSI))) {
            // Copy light readings for transfer
            memcpy(&data_packet.light_readings, &received_packet_data.light_readings, sizeof(received_packet_data.light_readings));
            data_packet.num_readings = received_packet_data.num_readings;
            transfer_light_readings();
        }
    }
}

char sender_scheduler(struct rtimer *t, void *ptr) {
    static uint16_t i = 0;
    static int NumSleep = 0;
    PT_BEGIN(&pt);
    curr_timestamp = clock_time();
    printf("Start clock %lu ticks, timestamp %3lu.%03lu\n", curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND) * 1000) / CLOCK_SECOND);
    while(1) {
        NETSTACK_RADIO.on();
        for(i = 0; i < NUM_SEND; i++) {
            nullnet_buf = (uint8_t *)&data_packet;
            nullnet_len = sizeof(data_packet);
            data_packet.seq++;
            curr_timestamp = clock_time();
            data_packet.timestamp = curr_timestamp;
            printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND) * 1000) / CLOCK_SECOND);
            NETSTACK_NETWORK.output(&dest_addr);
            if(i != (NUM_SEND - 1)) {
                rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
                PT_YIELD(&pt);
            }
        }
        if(SLEEP_CYCLE != 0) {
            NETSTACK_RADIO.off();
            NumSleep = random_rand() % (2 * SLEEP_CYCLE + 1);
            printf("Sleep for %d slots\n", NumSleep);
            for(i = 0; i < NumSleep; i++) {
                rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, (rtimer_callback_t)sender_scheduler, ptr);
                PT_YIELD(&pt);
            }
        }
    }
    PT_END(&pt);
}

PROCESS(nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&nbr_discovery_process);

PROCESS_THREAD(nbr_discovery_process, ev, data) {
    PROCESS_BEGIN();
    data_packet.src_id = node_id;
    data_packet.seq = 0;
    nullnet_set_input_callback(receive_packet_callback);
    linkaddr_copy(&dest_addr, &linkaddr_null);
    printf("CC2650 neighbour discovery\n");
    printf("Node %d will be sending packet of size %d Bytes\n", node_id, (int)sizeof(data_packet_struct));
    rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);
    PROCESS_END();
}
