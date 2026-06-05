#ifndef FIRMWARE_DATA_H
#define FIRMWARE_DATA_H

// Test amaçlı küçük bir boyut (Hocanın verdiği dosya için bunu gerçek boyuta ve byte dizisine çevireceğiz)
#define FIRMWARE_SIZE 512

static const uint8_t firmware_data[FIRMWARE_SIZE] = {
  0x7F, 0x45, 0x4C, 0x46, 0x01, 0x01, 0x01, 0x00, // ELF Sihirli Baytları
  // Geri kalan alanı simüle etmek için rastgele veriler...
  [8 ... 511] = 0xAA 
};

#endif /* FIRMWARE_DATA_H */