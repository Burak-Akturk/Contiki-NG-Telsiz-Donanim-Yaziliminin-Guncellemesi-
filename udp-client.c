#include "contiki.h"
#include "net/routing/routing.h"
#include "net/ipv6/simple-udp.h"
#include "sys/log.h"
#include "sys/node-id.h"
#include "firmware_data.h" 

#define LOG_MODULE "OTA-Sender"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define CHUNK_SIZE 64
#define SEND_INTERVAL (CLOCK_SECOND * 2)

static struct simple_udp_connection udp_conn;
static uint16_t current_chunk = 0;
static uint16_t total_chunks = 0;
static bool ack_received = false;

typedef struct {
  uint16_t chunk_num;
  uint16_t total_chunks;
  uint8_t payload_len;
  uint8_t payload[CHUNK_SIZE];
  uint16_t checksum;
} ota_packet_t;

static ota_packet_t tx_packet;

uint16_t calculate_checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  int i; 
  for(i = 0; i < len; i++) sum += data[i];
  return sum;
}

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen) {
  
  uint16_t acked_chunk;
  memcpy(&acked_chunk, data, sizeof(uint16_t));
  
  if(acked_chunk == current_chunk) {
    LOG_INFO("ACK Alindi: Blok %u\n", acked_chunk);
    ack_received = true;
    current_chunk++; 
  }
}

PROCESS(ota_client_process, "OTA Sender Process");
AUTOSTART_PROCESSES(&ota_client_process);

PROCESS_THREAD(ota_client_process, ev, data) {
  static struct etimer timer;
  static uip_ipaddr_t dest_ipaddr;

  PROCESS_BEGIN();
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

  if(node_id != 2) {
    LOG_INFO("Ben %u numarali araci dugumum (Router), gonderim yapmiyorum.\n", node_id);
    PROCESS_EXIT();
  }

  total_chunks = (FIRMWARE_SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE;
  LOG_INFO("Basliyor... Toplam Blok: %u\n", total_chunks);

  while(current_chunk < total_chunks) {
    etimer_set(&timer, SEND_INTERVAL);

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      uint32_t offset = current_chunk * CHUNK_SIZE;
      tx_packet.chunk_num = current_chunk;
      tx_packet.total_chunks = total_chunks;
      tx_packet.payload_len = ((current_chunk == total_chunks - 1) && (FIRMWARE_SIZE % CHUNK_SIZE != 0)) ? FIRMWARE_SIZE % CHUNK_SIZE : CHUNK_SIZE;
      
      memcpy(tx_packet.payload, &firmware_data[offset], tx_packet.payload_len);
      tx_packet.checksum = calculate_checksum(tx_packet.payload, tx_packet.payload_len);

      LOG_INFO("Gonderiliyor: Blok %u/%u\n", current_chunk, tx_packet.total_chunks);
      simple_udp_sendto(&udp_conn, &tx_packet, sizeof(ota_packet_t), &dest_ipaddr);
      ack_received = false;
    }
    
    PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_TIMER);
  }

  LOG_INFO("OTA Gonderimi Tamamlandi!\n");
  PROCESS_END();
}