#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h>
#include "node-id.h"
#include "sys/rtimer.h"
#include "board-peripherals.h"
#include <stdint.h>

#define NUM_SEND 1
#define MAX_READINGS 256
#define SECOND (CLOCK_SECOND * 1)

typedef struct {
    unsigned long src_id;
    unsigned long timestamp;
    unsigned long seq;
    uint16_t light_reading;
    bool has_read;
} data_packet_struct;

uint16_t captured_light_readings[MAX_READINGS];
static struct rtimer rt;
static struct pt pt;
static data_packet_struct data_packet;
static void init_opt_reading(void);
static void capture_light_reading(void);
unsigned long curr_timestamp;
int curr_light_reading_index = 0;
linkaddr_t dest_addr;

#define RSSI_THRESHOLD -70
#define WAKE_TIME RTIMER_SECOND/10 
#define SLEEP_CYCLE  9
#define SLEEP_SLOT RTIMER_SECOND/10

// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

// Capture light readings at 1HZ (tbc)
static void capture_light_reading() {
    //static struct etimer timer_etimer;
    int value = opt_3001_sensor.value(0);
    if (value != CC26XX_SENSOR_READING_ERROR) {
        captured_light_readings[curr_light_reading_index] = value;
        curr_light_reading_index += 1;
    } else {
        printf("OPT: Light Sensor's Warming Up\n\n");
    }
    //etimer_set(&timer_etimer, SECOND);
    //PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);
}

static void
init_opt_reading(void)
{
  SENSORS_ACTIVATE(opt_3001_sensor);
}

int is_good_link_quality(int rssi) {
    return rssi >= RSSI_THRESHOLD;
}

void print_received_light_reading() {
    printf("OPT: Light=%d.%02d lux\n", data_packet.light_reading / 100, data_packet.light_reading % 100);
}

void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
    if(len == sizeof(data_packet)) {
        static data_packet_struct received_packet_data;
        memcpy(&received_packet_data, data, len);
        printf("Received neighbour discovery packet %lu with rssi %d from %ld\n", received_packet_data.seq, (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI), received_packet_data.src_id);
        // Check if link quality is good
        if(is_good_link_quality(packetbuf_attr(PACKETBUF_ATTR_RSSI))) {
            memcpy(&data_packet.light_reading, &received_packet_data.light_reading, sizeof(uint16_t));
            print_received_light_reading();
            // So that sender will know if the receiver has read it
            data_packet.has_read = true;
        } else {
            // So that sender will know if the receiver has not read it
            data_packet.has_read = false;
        }

        // Remove the sent light reading
        if (received_packet_data.has_read) {
            // Remove the sent light reading from the array by shifting elements
            for (int i = 0; i < curr_light_reading_index - 1; i++) {
                captured_light_readings[i] = captured_light_readings[i + 1];
            }
            curr_light_reading_index--;
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
        capture_light_reading();
        NETSTACK_RADIO.on();
        for(i = 0; i < NUM_SEND; i++) {
            nullnet_buf = (uint8_t *)&data_packet;
            nullnet_len = sizeof(data_packet);
            
            data_packet.seq++;
            curr_timestamp = clock_time();
            data_packet.timestamp = curr_timestamp;
            data_packet.light_reading = captured_light_readings[curr_light_reading_index - 1];

            printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu with light reading %d \n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND) * 1000) / CLOCK_SECOND, data_packet.light_reading);
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
    init_opt_reading();
    data_packet.src_id = node_id;
    data_packet.seq = 0;
    nullnet_set_input_callback(receive_packet_callback);
    linkaddr_copy(&dest_addr, &linkaddr_null);
    printf("CC2650 neighbour discovery\n");
    printf("Node %d will be sending packet of size %d Bytes\n", node_id, (int)sizeof(data_packet_struct));
    rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);
    PROCESS_END();
}