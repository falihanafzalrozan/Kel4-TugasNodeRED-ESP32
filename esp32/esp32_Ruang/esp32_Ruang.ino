
#include "EspMQTTClient.h"
#include <DHT.h>

// ===== GANTI SESUAI KELOMPOK, RUANGAN, DAN JARINGAN =====
#define KELOMPOK      "klp4"                     // nomor kelompok
#define ROOM          "ruangX"                   // ruang1 .. ruang4
#define SENSOR_DUMMY  false                       // true = data dummy, false = sensor DHT asli
#define WIFI_SSID     "SSID_LAB"                 // JANGAN push SSID asli ke GitHub
#define WIFI_PASS     "PASSWORD_LAB"             // JANGAN push password asli ke GitHub
#define BROKER        "broker.mqttdashboard.com"

// ===== TOPIC & CLIENT ID dibentuk dari KELOMPOK + ROOM (E2, E7) =====
#define PREFIX "pens/" KELOMPOK "/" ROOM

const char* T_TEMP   = PREFIX "/temperature";
const char* T_HUM    = PREFIX "/humidity";
const char* T_L1     = PREFIX "/lamp1";
const char* T_L2     = PREFIX "/lamp2";
const char* T_STATUS = PREFIX "/status";

// ===== PIN =====
#define DHTPIN  16          // DATA DHT (Robo ESP32 Cytron: port Grove 1)
#define DHTTYPE DHT22       // sensor yang dipakai kelompok: DHT22 (ganti DHT11 bila perlu)
const int L1_PIN = 25;      // LED lampu 1 + resistor 220 ohm ke GND
const int L2_PIN = 26;      // LED lampu 2 + resistor 220 ohm ke GND

// ===== INTERVAL PUBLISH SENSOR (E5) =====
const unsigned long PUBLISH_INTERVAL = 5000;   // 5 detik

DHT dht(DHTPIN, DHTTYPE);

// E1 + E2: satu objek mengurus Wi-Fi, MQTT, dan reconnect otomatis
EspMQTTClient client(
  WIFI_SSID, WIFI_PASS,
  BROKER,
  "", "",                          // user, pass (kosong untuk broker publik)
  "esp32-" KELOMPOK "-" ROOM,      // E2: Client ID unik, mis. esp32-klp4-ruang1
  1883);

// ===== DATA DUMMY =====
// Nilai awal dibuat sedikit berbeda untuk tiap ruangan agar 4 garis di chart
// tidak saling menumpuk, lalu berubah perlahan (random walk) agar terlihat alami.
float dummyTemp = 28.0;
float dummyHum  = 65.0;

float langkahAcak(float nilai, float langkahMaks, float minV, float maxV) {
  nilai += (random(-100, 101) / 100.0) * langkahMaks;   // -langkahMaks .. +langkahMaks
  if (nilai < minV) nilai = minV;
  if (nilai > maxV) nilai = maxV;
  return nilai;
}

// Mengisi t dan h dari sensor asli atau dari data dummy
void bacaSensor(float& t, float& h) {
#if SENSOR_DUMMY
  dummyTemp = langkahAcak(dummyTemp, 0.3, 24.0, 36.0);
  dummyHum  = langkahAcak(dummyHum,  1.0, 40.0, 90.0);
  t = dummyTemp;
  h = dummyHum;
#else
  t = dht.readTemperature();
  h = dht.readHumidity();
#endif
}

// E6: hanya "1" atau "0" yang diproses, payload lain diabaikan
void setLamp(int pin, const char* nama, const String& payload) {
  if (payload == "1") {
    digitalWrite(pin, HIGH);
    Serial.printf("%s NYALA\n", nama);
  } else if (payload == "0") {
    digitalWrite(pin, LOW);
    Serial.printf("%s MATI\n", nama);
  } else {
    Serial.printf("%s: payload \"%s\" diabaikan\n", nama, payload.c_str());
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(L1_PIN, OUTPUT);
  pinMode(L2_PIN, OUTPUT);
  digitalWrite(L1_PIN, LOW);       // lampu mati saat board baru menyala
  digitalWrite(L2_PIN, LOW);

#if SENSOR_DUMMY
  // Nomor ruangan diambil dari ROOM ("ruang1" -> 1) untuk nilai awal yang berbeda
  int nomorRuang = atoi(ROOM + 5);
  dummyTemp = 26.0 + nomorRuang * 1.5;   // ruang1 27.5, ruang2 29.0, ...
  dummyHum  = 55.0 + nomorRuang * 5.0;   // ruang1 60, ruang2 65, ...
  Serial.println("Mode sensor: DUMMY (data simulasi)");
#else
  dht.begin();
  Serial.println("Mode sensor: DHT asli");
#endif

  client.enableDebuggingMessages();   // status Wi-Fi/MQTT tampil di Serial Monitor
  client.setKeepAlive(15);            // broker kirim LWT ~1,5 x 15 s setelah ESP32 hilang

  // E3: Last Will -> broker publish "offline" (retain) bila ESP32 putus tiba-tiba.
  // Harus didaftarkan sebelum koneksi pertama, jadi diletakkan di setup().
  client.enableLastWillMessage(T_STATUS, "offline", true);

  Serial.printf("Ruangan: %s | Client ID: esp32-%s-%s\n", ROOM, KELOMPOK, ROOM);
}

// Dipanggil library setiap kali (ulang) tersambung ke broker (E4)
void onConnectionEstablished() {
  Serial.println("MQTT Connection established.");

  // Subscribe lampu ruangan sendiri, QoS 1
  client.subscribe(T_L1, [](const String& payload) {
    setLamp(L1_PIN, "Lampu 1", payload);
  }, 1);

  client.subscribe(T_L2, [](const String& payload) {
    setLamp(L2_PIN, "Lampu 2", payload);
  }, 1);

  // Tandai ruangan online (retain = true) agar Node-RED langsung tahu statusnya
  client.publish(T_STATUS, "online", true);
}

unsigned long lastPublishTime = 0;

void loop() {
  client.loop();   // E1: Wi-Fi, MQTT, dan reconnect diurus di sini (tanpa delay)

  // E5: baca dan publish tiap 5 detik memakai millis(), bukan delay()
  if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = millis();

    if (!client.isConnected()) return;   // tunggu tersambung dulu

    float temp, hum;
    bacaSensor(temp, hum);

    // Bacaan gagal (nan) tidak dikirim ke broker
    if (!isnan(temp)) {
      client.publish(T_TEMP, String(temp, 1));   // 1 desimal, mis. "28.5"
      Serial.printf("Suhu: %.1f C\n", temp);
    } else {
      Serial.println("Gagal membaca suhu");
    }

    if (!isnan(hum)) {
      client.publish(T_HUM, String(hum, 1));     // 1 desimal, mis. "65.0"
      Serial.printf("Kelembapan: %.1f %%\n", hum);
    } else {
      Serial.println("Gagal membaca kelembapan");
    }
  }
}
