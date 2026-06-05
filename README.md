# BİL 304 İşletim Sistemleri - OTA Firmware Güncelleme Projesi

**Proje Ekibi:**
* Burak Aktürk 23060484
* Kadir Kopuz 23060165
* Lachin Ibishov 23060004

🎥 **Proje Sunum ve Simülasyon Videosu :** https://www.youtube.com/watch?v=QJC-aDK6uZc

---

## 1. Gerçeklenen Yöntemler ve Haberleşme Mimarisi

Bu projede, Contiki-NG işletim sistemi üzerinde çalışan IoT düğümleri arasında güvenilir bir "Havadan Güncelleme" (Over-The-Air - OTA) sistemi tasarlanmıştır.

Gönderici düğüm (Node 2), yaklaşık 130 KB'lık firmware imajını kablosuz IPv6/RPL ağı üzerinden Kök düğüme (Node 1) aktarır. Ağdaki paket kayıplarını ve yığılmaları önlemek amacıyla **"Stop-and-Wait" (Dur-Bekle) ARQ protokolü** gerçeklenmiştir. Gönderici, gönderdiği her veri bloğu için alıcıdan bir `ACK` (Onay) sinyali bekler.

Alıcı düğüm ise gelen parçaları uçucu bellekte (RAM) tutmak yerine, Contiki'nin **Coffee File System (CFS)** arayüzünü kullanarak kalıcı sanal diske (`new-firmware.bin`) sıralı bir şekilde kaydeder.

**Örnek Kod - Gönderici Tarafında Dur-Bekle Döngüsü:**
```c
// Gönderici 2 saniyelik zamanlayıcı kurar ve beklemeye geçer
etimer_set(&timer, SEND_INTERVAL);
simple_udp_sendto(&udp_conn, &tx_packet, sizeof(ota_packet_t), &dest_ipaddr);

// Ya ACK gelir ya da zamanlayıcı (2 sn) dolar
PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_TIMER);
```
## 2. Paket Uzunlukları ve Veri Yapısı

IEEE 802.15.4 tabanlı telsiz ağlarında MTU (Maksimum Aktarım Birimi) kısıtlıdır. Bu nedenle 130 KB'lık firmware dosyası, parçalanarak aktarılır. Sistemin taşıma yükü (payload) sabit olarak 64 Bayt olarak belirlenmiştir. Ancak havada sadece ham veri değil, sıranın karışmaması için yönlendirme bilgilerini içeren bir başlık (Header) yapısı da gönderilir.

```c
#define CHUNK_SIZE 64 // Belirlenen sabit paket uzunluğu

typedef struct {
  uint16_t chunk_num;          // O anki paketin sıra numarası
  uint16_t total_chunks;       // İmajın bölündüğü toplam blok sayısı
  uint8_t payload_len;         // Verinin gerçek uzunluğu (Son paket 64'ten küçük olabilir)
  uint8_t payload[CHUNK_SIZE]; // 64 Baytlık asıl makine kodu verisi
  uint16_t checksum;           // Hata kontrolü için 16-bit sağlama toplamı
} ota_packet_t;
```
## 3. Alınan Önlemler ve Hata Kontrol Mekanizmaları
Kablosuz ortamdaki veri bozulmalarına ve paket kayıplarına karşı 4 aşamalı bir güvenlik önlemi alınmıştır:

**A. Bit Bozulmalarına Karşı "Checksum"
Havada seken paketlerin içerisindeki tek bir bitin bile değişmesi firmware imajını bozacaktır. Buna önlem olarak her paket için 16-bitlik bir Checksum hesaplanır. Alıcı bu toplamı doğrular, uyuşmazlık varsa paketi düşürür.
```c
uint16_t calc_checksum = calculate_checksum(rx_packet->payload, rx_packet->payload_len);
if(calc_checksum != rx_packet->checksum) {
    return; // Doğrulama başarısızsa işlem yapmadan fonksiyonu sonlandır
}
```
**B. Paket Kayıplarına Karşı "Timeout"
Gönderici düğüm, paketi yolladıktan sonra 2 saniye içinde ACK alamazsa paketin veya onayın havada kaybolduğunu varsayar. Blok numarasını ilerletmeden aynı paketi yeniden göndererek veri kaybını sıfıra indirir.

**C. Sırasız/Tekrarlı Paket Engelleme
Ağ gecikmeleri nedeniyle daha önce onaylanmış eski bir paket alıcıya tekrar ulaşabilir. Alıcı, bellekteki sırayı bozmamak için sadece beklediği sıradaki paketi (expected_chunk) diske yazar.
```c
if(rx_packet->chunk_num == expected_chunk) {
    // Sadece beklenen sıradaki paket diske yazılır
    cfs_seek(file_fd, expected_chunk * CHUNK_SIZE, CFS_SEEK_SET);
    cfs_write(file_fd, rx_packet->payload, rx_packet->payload_len);
    expected_chunk++;
}
// Eski/tekrarlı bir paket geldiyse bile döngünün tıkanmaması için ACK dönülür
if(rx_packet->chunk_num <= expected_chunk) {
    simple_udp_sendto(&udp_conn, &rx_packet->chunk_num, sizeof(uint16_t), sender_addr);
}
```

**D. Tüm-İmaj Doğrulama (Full Hash) Önlemi
Paketler tek tek sağlam gitse bile, cihaz diske yazarken donanımsal bir hata oluşabilir. Bu riske karşı; aktarım bittikten sonra diskteki dosya baştan sona tekrar okunarak 32-bitlik "Additive Hash" çıkarılır. Bu, dosyanın %100 oranında sağlam çalıştığının nihai garantisidir.
```c
// Tüm parçalar alındığında diskteki imajın kümülatif hash'i alınır
if(expected_chunk >= rx_packet->total_chunks) {
    cfs_seek(file_fd, 0, CFS_SEEK_SET); // Dosya başa sarılır
    uint32_t full_hash = 0;
    
    while((bytes_read = cfs_read(file_fd, buffer, sizeof(buffer))) > 0) {
        for(int j = 0; j < bytes_read; j++) full_hash += buffer[j];
    }
    LOG_INFO("Dogrulama Basarili! Imaj Hash Degeri: %lu\n", full_hash);
}
```

