#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ===================================
// 1. PIN DEFINITION & HARDWARE SETUP
// ===================================

// PIN ADS1115 (I2C)
// Wemos D1 Mini default I2C pins: D2 (SDA), D1 (SCL)
// #define APIN_DOUT   4 // D2 (GPIO4) = SDA  (Tidak perlu didefinisikan ulang jika menggunakan Wire.h default)
// #define APIN_SCLK   5 // D1 (GPIO5) = SCL  (Tidak perlu didefinisikan ulang jika menggunakan Wire.h default)
Adafruit_ADS1115 ads; // Gunakan ADS1115

// PIN LORA (SPI)
#define NSS_PIN     15 // D8 (GPIO15)
#define RST_PIN     0  // D3 (GPIO0)
#define DIO0_PIN    16 // D0 (GPIO16)
#define SCK_PIN     14 // D5 (GPIO14)
#define MISO_PIN    12 // D6 (GPIO12)
#define MOSI_PIN    13 // D7 (GPIO13)

// ===================================
// 2. LORA PARAMETER & CONFIGURATION
// ===================================
#define LORA_FREQ         433E6    // 433 MHz
#define LORA_SF           9        // Spreading Factor 9
#define LORA_BW           250E3    // Bandwidth 250 kHz
#define LORA_SYNC_WORD    0x52     // Sync Word

// ===================================
// 3. DATA STRUCTURE & TRANSMISSION
// ===================================
#define CHUNKSIZE         200      // Ukuran data aktual dalam body (200 byte)
#define DATASIZE          100      // 100 sampel (100 * 2 byte/sampel = 200 byte)
#define MSG_TYPE_HEAD     1
#define MSG_TYPE_BODY     2
#define MSG_TYPE_TAIL     3

// Buffer untuk menampung data sampel Geofon
int16_t geophone_samples[DATASIZE]; // 16-bit integer = 2 byte

// --- HEADER ---
struct head_message {
  uint8_t msgtype;     // 1
  uint8_t dID;         // Data ID: 1
  size_t length;       // Total data length (ukuran struct body + tail)
  uint8_t dtype;       // Data type (e.g., 2 for int16_t)
  uint32_t tsample;    // The sampling period (e.g., in ms)
  double timestamp;    // The time this data were sampled (diisi dengan millis())
  int32_t aux;         // Auxiliary data (e.g., battery level)
} head;

// --- CHUNK DATA BODY ---
struct body_message {
  uint8_t msgtype;     // 2
  uint8_t dID;         // Data ID: 1
  uint8_t length;      // Length of this chunk (CHUNKSIZE / 1 byte)
  uint16_t chunk;      // Chunk sequence number (e.g., 1)
  uint16_t checksum;   // Reserved for checksum
  uint8_t data[CHUNKSIZE]; // Data blob (200 byte)
} body;

// --- TAIL ---
struct tail_message {
  uint8_t msgtype;     // 3
  uint8_t dID;
  uint16_t chunk;      // Chunk sequence number (e.g., 1)
  size_t length;       // Reserved
} tail;

// ===================================
// 4. TIMING & CONTROL
// ===================================
const long TX_INTERVAL_MS = 60000; // Kirim paket setiap 60 detik (60.000 ms)
long lastTxTime = 0;
uint8_t data_id_counter = 0; // ID unik untuk setiap sesi kirim

// ===================================
// 5. SETUP FUNCTION
// ===================================

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Inisialisasi LoRa
  Serial.println("LoRa Sender Node");
  //SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, NSS_PIN);
  LoRa.setPins(NSS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  // Konfigurasi LoRa sesuai parameter
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  Serial.print("LoRa initialized with SF: "); Serial.println(LORA_SF);

  // Inisialisasi ADS1115
  Wire.begin();
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115.");
    while (1);
  }
  ads.setGain(GAIN_ONE); // Set Gain, sesuaikan dengan output Geofon Anda

  Serial.println("System Ready.");
}

// ===================================
// 6. LOOP FUNCTION
// ===================================

void loop() {
  if (millis() - lastTxTime > TX_INTERVAL_MS) {
    // 1. Ambil Data Sensor Geofon
    collect_geophone_data();

    // 2. Siapkan dan Kirim Paket (Header -> Body -> Tail)
    send_complete_packet();

    // 3. Update Waktu
    lastTxTime = millis();
  }
}

// ===================================
// 7. HELPER FUNCTIONS
// ===================================

/**
 * Mengumpulkan 100 sampel (200 byte) dari ADS1115
 */
void collect_geophone_data() {
  Serial.print("Collecting "); Serial.print(DATASIZE); Serial.println(" Geophone samples...");
  
  // Waktu mulai sampling untuk timestamp
  head.timestamp = (double)millis();
  
  for (int i = 0; i < DATASIZE; i++) {
    // Baca diferensial A0 dan A1 (asumsi Geofon terhubung ke A0/A1)
    geophone_samples[i] = ads.readADC_Differential_0_1();
    // Simulasi: menunda sedikit agar tidak terlalu cepat
    // Sesuaikan nilai delay ini dengan frekuensi sampling yang Anda butuhkan
    delay(5); 
  }
  Serial.println("Sampling complete.");

  // Memindahkan data Geofon ke dalam struct body (cast int16_t* ke uint8_t*)
  memcpy(body.data, (uint8_t*)geophone_samples, CHUNKSIZE);
}


/**
 * Mengisi struktur Header, Body, Tail dan mengirimkannya
 */
void send_complete_packet() {
  data_id_counter++;
  
  // --- A. PREPARE HEADER ---
  head.msgtype = MSG_TYPE_HEAD;
  head.dID = data_id_counter;
  head.length = sizeof(body_message) + sizeof(tail_message);
  head.dtype = 2; // int16_t is 2 bytes
  head.tsample = 5; // Simulasikan periode sampling 5 ms
  head.aux = ESP.getVcc(); // Contoh: Mengambil level tegangan Wemos D1 Mini
  
  Serial.print("TX Header (ID: "); Serial.print(head.dID); Serial.print(", Size: "); Serial.print(sizeof(head_message)); Serial.println(" bytes)");
  
  // --- B. PREPARE BODY ---
  body.msgtype = MSG_TYPE_BODY;
  body.dID = head.dID;
  body.length = CHUNKSIZE;
  body.chunk = 1; // Chunk pertama dan satu-satunya
  body.checksum = 0; // Abaikan checksum untuk simulasi ini

  Serial.print("TX Body (ID: "); Serial.print(body.dID); Serial.print(", Size: "); Serial.print(sizeof(body_message)); Serial.println(" bytes)");
  
  // --- C. PREPARE TAIL ---
  tail.msgtype = MSG_TYPE_TAIL;
  tail.dID = head.dID;
  tail.chunk = 1;
  tail.length = 0;
  
  Serial.print("TX Tail (ID: "); Serial.print(tail.dID); Serial.print(", Size: "); Serial.print(sizeof(tail_message)); Serial.println(" bytes)");
  
  // --- D. TRANSMIT THREE PACKETS ---
  
  // 1. Kirim HEADER
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&head, sizeof(head_message));
  LoRa.endPacket(true); // true = blokir hingga selesai

  // 2. Kirim BODY (Data Geofon)
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&body, sizeof(body_message));
  LoRa.endPacket(true);

  // 3. Kirim TAIL
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&tail, sizeof(tail_message));
  LoRa.endPacket(true);
  
  Serial.println("Total 3 packets sent successfully.");
}