#include <dummy.h>

// Identitas project untuk platform Blynk
#define BLYNK_TEMPLATE_ID "TMPL6_WQybm-C"
#define BLYNK_TEMPLATE_NAME "Monitoring kebakaran"
#define BLYNK_AUTH_TOKEN "8KFbpSsSd8FPdlF37Fdm1jSxwpysmejQ"

// Library sensor & koneksi
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

// ---------------- Telegram Configuration ----------------
#define BOT_TOKEN "8308546966:AAEPQMMXUkosNmG56aVMXazsTvwGBy04m68"  


String chatID[20];
int chatCount = 0;                                      

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
int botRequestDelay = 1000;  // jeda antar-polling pesan (ms)
unsigned long lastTimeBotRan = 0;


// Data WiFi untuk koneksi ke internet
char ssid[] = "Lab-Jarkom";
char pass[] = "";

// Konfigurasi sensor DHT
#define PIN_DHT D6       // Pin data sensor DHT dihubungkan ke pin D6 ESP8266
#define JENIS_DHT DHT11  // Tipe sensor DHT yang digunakan
DHT dht(PIN_DHT, JENIS_DHT);

const int JEDA_BACA = 4000;  // Interval baca sensor (ms)

// ---------------- State default ----------------
const float SUHU_DEFAULT = 25.0;    // Suhu awal default (°C)
const float LEMBAB_DEFAULT = 60.0;  // Kelembaban awal default (%)

// Variabel untuk menyimpan status sistem (default: AMAN)
String statusSekarang = "AMAN";

// ---------------- EMA (Exponential Moving Average) ----------------
float emaSuhu = NAN;      // Nilai awal suhu EMA (NAN = belum ada data)
float emaLembab = NAN;    // Nilai awal kelembaban EMA
const float ALPHA = 0.4;  // Bobot smoothing (0–1), makin besar makin responsif

// ---------------- Buffer Rolling ----------------
const int JUMLAH_SAMPEL = 5;  // Jumlah data terakhir yang disimpan di buffer
float bufferSuhu[JUMLAH_SAMPEL];
float bufferLembab[JUMLAH_SAMPEL];
int indexSampel = 0;
bool bufferPenuh = false;

// ---------------- Threshold (ambang status) ----------------
const float MASUK_TERBAKAR_SUHU = 45.0;
const float KELUAR_TERBAKAR_SUHU = 43.0;
const float MASUK_TERBAKAR_LEMBAB = 20.0;
const float KELUAR_TERBAKAR_LEMBAB = 25.0;


const float MASUK_WASPADA_SUHU = 35.0;
const float KELUAR_WASPADA_SUHU = 33.0;
const float MASUK_WASPADA_LEMBAB = 40.0;
const float KELUAR_WASPADA_LEMBAB = 45.0;

// ---------------- Konfirmasi status ----------------
int hitungTerbakar = 0;
int hitungWaspada = 0;
int hitungAman = 0;
const int JUMLAH_KONFIRMASI = 2;

// Timer untuk eksekusi fungsi berkala
BlynkTimer timer;

// ---------------- Fungsi update EMA ----------------
void perbaruiEMA(float suhu, float lembab) {
  // Hitung Exponential Moving Average (EMA) untuk suhu dan kelembaban
  emaSuhu = ALPHA * suhu + (1.0 - ALPHA) * emaSuhu;
  emaLembab = ALPHA * lembab + (1.0 - ALPHA) * emaLembab;

  // Menyimpan nilai EMA dalam buffer
  bufferSuhu[indexSampel] = emaSuhu;
  bufferLembab[indexSampel] = emaLembab;

  // Update index untuk buffer
  indexSampel = (indexSampel + 1) % JUMLAH_SAMPEL;

  // Jika buffer sudah penuh, set bufferPenuh menjadi true
  if (indexSampel == 0) {
    bufferPenuh = true;
  }
}

// ---------------- Fungsi rata-rata isi buffer ----------------
float rataBuffer(float *arr) {
  int n = bufferPenuh ? JUMLAH_SAMPEL : indexSampel;
  float total = 0;

  // Menjumlahkan data dalam buffer sampai n
  for (int i = 0; i < n; i++) total += arr[i];
  return total / n;
}

// ---------------- Fungsi pengecekan status ----------------
void cekStatus() {
  float nilaiSuhu = rataBuffer(bufferSuhu);
  float nilaiLembab = rataBuffer(bufferLembab);

  if (isnan(nilaiSuhu) || isnan(nilaiLembab)) return;

  String statusBaru = statusSekarang;

  // -------------------TERBAKAR -------------------
  bool masukTerbakar =
  ((nilaiSuhu >= MASUK_TERBAKAR_SUHU) && (nilaiLembab <= MASUK_TERBAKAR_LEMBAB)) || ((nilaiSuhu >= MASUK_WASPADA_SUHU) && (nilaiLembab <= 10.0));

  bool keluarTerbakar =
    (nilaiSuhu < KELUAR_TERBAKAR_SUHU) && (nilaiLembab > KELUAR_TERBAKAR_LEMBAB);

  if (statusSekarang != "TERBAKAR" && masukTerbakar) {
    statusBaru = "TERBAKAR";
  } else if (statusSekarang == "TERBAKAR" && !keluarTerbakar) {
    statusBaru = "TERBAKAR";  // tetap terbakar
  }

  // ------------------- WASPADA -------------------
  else {
    bool masukWaspada =
      (nilaiSuhu >= MASUK_WASPADA_SUHU) && (nilaiLembab <= MASUK_WASPADA_LEMBAB);

    bool keluarWaspada =
      (nilaiSuhu < KELUAR_WASPADA_SUHU) || (nilaiLembab >= KELUAR_WASPADA_LEMBAB);

    if (statusSekarang != "HATI-HATI" && masukWaspada) {
      statusBaru = "HATI-HATI";
    } else if (statusSekarang == "HATI-HATI" && !keluarWaspada) {
      statusBaru = "HATI-HATI";  // tetap waspada
    } else {
      statusBaru = "AMAN";
    }
  }

  if (statusBaru != statusSekarang) {
    if (statusBaru == "TERBAKAR") {
      hitungTerbakar++;
      if (hitungTerbakar >= JUMLAH_KONFIRMASI) {
        statusSekarang = "TERBAKAR";
        hitungTerbakar = 0;
      
        String pesan = " *PERINGATAN! BAHAYA-BAHAYA* Status: TERBAKAR!*\n";
        pesan += "Suhu tinggi: " + String(nilaiSuhu, 1) + " °C\n";
        pesan += "Kelembaban rendah: " + String(nilaiLembab, 1) + " %RH terdeteksi.";
        kirimTelegram(pesan);
        Blynk.logEvent("bahaya_alert", pesan);
      }
    } else if (statusBaru == "HATI-HATI") {
      hitungWaspada++;
      if (hitungWaspada >= JUMLAH_KONFIRMASI) {
        statusSekarang = "HATI-HATI";
        hitungWaspada = 0;
        

        String pesan = " * Waspada!* Suhu mulai meningkat & kelembaban menurun.\n";
        pesan += "Suhu saat ini: " + String(nilaiSuhu, 1) + " °C\n";
        pesan += "Kelembaban: " + String(nilaiLembab, 1) + " %RH";
        kirimTelegram(pesan);
        Blynk.logEvent("hatihati_alert", pesan);
      }
    } else if (statusBaru == "AMAN") {
      hitungAman++;
      if (hitungAman >= JUMLAH_KONFIRMASI) {
        statusSekarang = "AMAN";
        hitungAman = 0;
      
        String pesan = " *Status Aman.* Kondisi kembali normal.\n";
        pesan += "Suhu: " + String(nilaiSuhu, 1) + " °C\n";
        pesan += "Kelembaban: " + String(nilaiLembab, 1) + " %RH";
        kirimTelegram(pesan);
        Blynk.logEvent("aman_alert", pesan);
      }
    }
  }
}

void addChatID(String id) {
  for (int i = 0; i < chatCount; i++) {
    if (chatID[i] == id) return;  
  }
  chatID[chatCount++] = id;
  Serial.println("Chat ID baru ditambahkan: " + id);
}


// ---------------- Fungsi kirim data ke Telegram ----------------
void kirimTelegram(String pesan) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Gagal kirim Telegram: WiFi tidak terhubung");
    return;
  }
  for (int i = 0; i < chatCount; i++) {
    bot.sendMessage(chatID[i], pesan, "");
    delay(300);  
  }

  Serial.println("Broadcast Telegram terkirim ke  user");
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages: " + String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;

    Serial.println("Pesan diterima dari: " + from_name + " -> " + text);

    // ---------------------- Command /start ----------------------
    if (text == "/start") {
      addChatID(chat_id);
      String msg = " Halo, " + from_name + "!\n";
      msg += "Pilihlah perintah berikut untuk memantau sensor DHT11:\n\n";
      msg += "Perintah:\n";
      msg += " /Temperatur → Tampilkan suhu terkini\n";
      msg += " /Humidity → Tampilkan kelembapan terkini\n";
      msg += " /Status → Tampilkan status kondisi lahan\n";
      bot.sendMessage(chat_id, msg, "");
    }

    // ---------------------- Command /Temperatur ----------------------
    else if (text == "/Temperatur") {
      float suhu = dht.readTemperature();
      if (isnan(suhu)) {
        suhu = SUHU_DEFAULT;
      }
      String msg = " Suhu saat ini: ";
      msg += String(suhu, 1);
      msg += " °C";
      bot.sendMessage(chat_id, msg, "");
    }

    // ---------------------- Command /Humidity ----------------------
    else if (text == "/Humidity") {
      float lembab = dht.readHumidity();
      if (isnan(lembab)) {
        lembab = LEMBAB_DEFAULT;
      }
      String msg = " Kelembapan saat ini: ";
      msg += String(lembab, 1);
      msg += " %RH";
      bot.sendMessage(chat_id, msg, "");
    }

    // ---------------------- Command /Status ----------------------
    else if (text == "/Status") {
      String msg = " Status terakhir: " + statusSekarang + "\n";
      msg += "Suhu : " + String(emaSuhu, 1) + " °C\n";
      msg += "Kelembaban  : " + String(emaLembab, 1) + " %";
      bot.sendMessage(chat_id, msg, "");
    }

    // ---------------------- Command tidak dikenal ----------------------
    else {
      bot.sendMessage(chat_id, "❓ Perintah tidak dikenal. Gunakan /start untuk daftar perintah.", "");
    }
  }
}


// ---------------- Fungsi kirim data ke Blynk ----------------
void kirimDataBlynk() {
  float kelembaban = dht.readHumidity();
  float suhu = dht.readTemperature();

  if (isnan(kelembaban) || isnan(suhu)) {
    Serial.println("Sensor gagal dibaca... gunakan nilai default");
    suhu = SUHU_DEFAULT;
    kelembaban = LEMBAB_DEFAULT;
  }

  perbaruiEMA(suhu, kelembaban);
  cekStatus();

  float nilaiSuhu = rataBuffer(bufferSuhu);
  float nilaiLembab = rataBuffer(bufferLembab);

  Serial.print("Suhu: ");
  Serial.print(nilaiSuhu, 1);
  Serial.print(" °C, Kelembaban: ");
  Serial.print(nilaiLembab, 1);
  Serial.print(" % -> Status: ");
  Serial.println(statusSekarang);

  Blynk.virtualWrite(V0, nilaiSuhu);
  Blynk.virtualWrite(V1, nilaiLembab);
  Blynk.virtualWrite(V2, statusSekarang);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  dht.begin();
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  secured_client.setInsecure();  // Untuk menghindari verifikasi sertifikat SSL
  Serial.println("Terhubung ke Blynk & Telegram Bot siap digunakan...");

  // Isi buffer awal dengan nilai default
  for (int i = 0; i < JUMLAH_SAMPEL; i++) {
    bufferSuhu[i] = SUHU_DEFAULT;
    bufferLembab[i] = LEMBAB_DEFAULT;
  }
  emaSuhu = SUHU_DEFAULT;
  emaLembab = LEMBAB_DEFAULT;
  indexSampel = 0;
  bufferPenuh = true;

  timer.setInterval(JEDA_BACA, kirimDataBlynk);

  Serial.println("Monitoring Kebakaran Lahan di Riau Menggunakan DHT11");
}

// ---------------- Loop utama ----------------
void loop() {
  Blynk.run();
  timer.run();
  // ----------------- Periksa pesan baru Telegram -----------------
  if (millis() - lastTimeBotRan > botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}