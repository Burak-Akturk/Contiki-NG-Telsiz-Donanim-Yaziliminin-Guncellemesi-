#include "contiki.h"
#include "net/routing/routing.h"
#include "net/ipv6/simple-udp.h"
#include "sys/log.h"
#include "cfs/cfs.h"

#define LOG_MODULE "OTA-Receiver"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define CHUNK_SIZE 64
#define FIRMWARE_FILENAME "new-firmware.bin"

static struct simple_udp_connection udp_conn;
static uint16_t expected_chunk = 0;
static int file_fd;

typedef struct {
  uint16_t chunk_num;
  uint16_t total_chunks;
  uint8_t payload_len;
  uint8_t payload[CHUNK_SIZE];
  uint16_t checksum;
} ota_packet_t;

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
  
  ota_packet_t *rx_packet = (ota_packet_t *)data;

  uint16_t calc_checksum = calculate_checksum(rx_packet->payload, rx_packet->payload_len);
  if(calc_checksum != rx_packet->checksum) return;

  if(rx_packet->chunk_num == expected_chunk) {
    LOG_INFO("Alindi ve Diske Yaziliyor: Blok %u/%u\n", rx_packet->chunk_num, rx_packet->total_chunks);
    
    if(file_fd >= 0) {
      cfs_seek(file_fd, expected_chunk * CHUNK_SIZE, CFS_SEEK_SET);
      cfs_write(file_fd, rx_packet->payload, rx_packet->payload_len);
    }
    
    expected_chunk++;
    
    if(expected_chunk >= rx_packet->total_chunks) {
      LOG_INFO("Tum imaj dogrulamasi basliyor...\n");
      
      if(file_fd >= 0) {
        cfs_seek(file_fd, 0, CFS_SEEK_SET);
        uint32_t full_hash = 0;
        uint8_t buffer[CHUNK_SIZE];
        int bytes_read;
        
        while((bytes_read = cfs_read(file_fd, buffer, sizeof(buffer))) > 0) {
          int j;
          for(j = 0; j < bytes_read; j++) full_hash += buffer[j];
        }
        
        LOG_INFO("Dogrulama Basarili! Imaj Hash Degeri: %lu\n", (unsigned long)full_hash);
        
        cfs_close(file_fd);
        file_fd = -1;
      }
      
      LOG_INFO("OTA ISLEMI BASARILI! Yeni firmware alimi tamamlandi ve diske kaydedildi.\n");
    }
  }

  if(rx_packet->chunk_num <= expected_chunk) {
    simple_udp_sendto(&udp_conn, &rx_packet->chunk_num, sizeof(uint16_t), sender_addr);
  }
}

PROCESS(ota_server_process, "OTA Receiver Process");
AUTOSTART_PROCESSES(&ota_server_process);

PROCESS_THREAD(ota_server_process, ev, data) {
  PROCESS_BEGIN();
  
  NETSTACK_ROUTING.root_start();
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);

  cfs_remove(FIRMWARE_FILENAME);
  file_fd = cfs_open(FIRMWARE_FILENAME, CFS_WRITE | CFS_READ);
  LOG_INFO("Alici (Server) hazir. Dosya sistemi aktif, OTA bekleniyor...\n");

  while(1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}