// https://lang-ship.com/blog/?p=946
// https://gist.github.com/ykst/6e80e3566bd6b9d63d19
// https://blog.jxck.io/entries/2016-07-21/fetch-progress-cancel.html

#include <M5StickC.h>
#include <driver/i2s.h>
#include "esp_http_server.h"
#include <WiFi.h>

#define PIN_CLK          0
#define PIN_DATA         34
#define SAMPLING_RATE    44100 // 44100 22050 11025 8000
#define RING_BUFFER_SIZE (50000)
#define DMA_BUFFER_LEN   1024
#define WAIT_MS 100

uint8_t tmpBuffer[DMA_BUFFER_LEN];
uint8_t ringBuffer[RING_BUFFER_SIZE];
int     ringBufferPos = 0;

unsigned long totalReadBytes = 0;
unsigned long totalReadCount = 0;
unsigned long totalSendBytes = 0;

void i2sRecord()
{
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate          = SAMPLING_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ALL_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 2,
    .dma_buf_len          = DMA_BUFFER_LEN,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0,
  };
 
  i2s_pin_config_t pin_config;
  pin_config.bck_io_num   = I2S_PIN_NO_CHANGE;
  pin_config.ws_io_num    = PIN_CLK;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num  = PIN_DATA;
 
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, SAMPLING_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
 
  xTaskCreatePinnedToCore(i2sRecordTask, "i2sRecordTask", 2048, NULL, 1, NULL, 1);
}

void i2sRecordTask(void* arg)
{
  while(1){
    size_t bytes_read = 0;
    i2s_read(I2S_NUM_0, tmpBuffer, DMA_BUFFER_LEN, &bytes_read, (WAIT_MS / portTICK_RATE_MS));
    totalReadBytes += bytes_read;
    totalReadCount += 1;

    // int16_t(12bit精度)をuint8_tに変換
    for(int i=0; i<bytes_read ; i+=2){
      int16_t* val = (int16_t*)&tmpBuffer[i];
      ringBuffer[ringBufferPos] = ( *val + 32768 ) / 256;
      ringBufferPos++;
      if(RING_BUFFER_SIZE == ringBufferPos){
        ringBufferPos = 0;
        printf("ringBufferPos=0\n");
      }
    }

    vTaskDelay(1 / portTICK_RATE_MS);
  }

  //i2s_driver_uninstall(I2S_NUM_0);
  //vTaskDelete(NULL);
}
 
void setup() {
  memset(ringBuffer, 0, sizeof(ringBuffer));

  M5.begin();
  i2sRecord();

  while(1){
    Serial.printf("Connecting...\n");
    bool ok = wifi_connect(10*1000);
    if(ok){
      IPAddress ipAddress = WiFi.localIP();
      Serial.printf("%s\n", ipAddress.toString().c_str());
      break;
    }
  }
  startHttpd();
}
 
void loop() {
//  M5.update();
//  if(M5.BtnA.wasPressed()){
//  }else
//  if(M5.BtnA.wasReleased()){
//  }else
//  if(M5.BtnB.wasPressed()){
//  }else
//  if(M5.BtnB.wasReleased()){
//  }

  unsigned long ms = millis();
  static unsigned long prevMs = 0;
  if(5000 < ms-prevMs){
    static unsigned long prevTotalReadBytes = 0;
    static unsigned long prevTotalReadCount = 0;
    static unsigned long prevTotalSendBytes = 0;
    if(prevMs){
      printf("readBytes=%d(%d/s) readCount=%d(%d/s) sendBytes=%d(%d/s)\n",
        totalReadBytes,
        1000*(totalReadBytes-prevTotalReadBytes)/(ms-prevMs),
        totalReadCount,
        1000*(totalReadCount-prevTotalReadCount)/(ms-prevMs),
        totalSendBytes,
        1000*(totalSendBytes-prevTotalSendBytes)/(ms-prevMs)
      );
    }
    prevTotalReadBytes = totalReadBytes;
    prevTotalReadCount = totalReadCount;
    prevTotalSendBytes = totalSendBytes;
    prevMs = ms;
    update_display();
  }
  
  delay(1);
}

//static const char* _INDEX_HTML = "<audio autoplay controls src='/wav'></audio>";
#include "index_html.h"

httpd_handle_t httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req){
  printf("index_handler\n");
  esp_err_t res = ESP_OK;
  res = httpd_resp_set_type(req, "text/html");
  if(res != ESP_OK){
    return res;
  }
  res = httpd_resp_send(req, _INDEX_HTML, strlen(_INDEX_HTML));
  return res;
}

static esp_err_t wav_handler(httpd_req_t *req){
  esp_err_t res = ESP_OK;
  int buf_size = 44;
  char buf[buf_size];

  res = httpd_resp_set_type(req, "audio/wav");
  memcpy(buf,
    "\x52\x49\x46\x46"  // RIFFヘッダ
    "\xFF\xFF\xFF\xFF"  // 総データサイズ+44(チャンクサイズ)-8(ヘッダサイズ)
    "\x57\x41\x56\x45"  // WAVEヘッダ
    "\x66\x6D\x74\x20"  // フォーマットチャンク
    "\x10\x00\x00\x00"  // フォーマットサイズ
    "\x01\x00"          // フォーマットコード
    "\x01\x00",         // チャンネル数
    24
  );
  *((uint32_t*)(buf+24)) = SAMPLING_RATE; // サンプリングレート
  *((uint32_t*)(buf+28)) = SAMPLING_RATE; // バイト／秒
  memcpy(buf+32,
    "\x02\x00"          // ブロック境界
    "\x08\x00"          // ビット／サンプル
    "\x64\x61\x74\x61"  // dataチャンク
    "\xFF\xFF\xFF\xFF", // 総データサイズ
    12
  );
  if(res == ESP_OK){
    res = httpd_resp_send_chunk(req, buf, 44);
  }

  int pos = ringBufferPos;
  while(true){
    if(res == ESP_OK){
      vTaskDelay(WAIT_MS / portTICK_RATE_MS);
      int len = ringBufferPos - pos;
      if(len<0){
        len = RING_BUFFER_SIZE - pos;
      }
      if(0<len){
        printf("httpd_resp_send_chunk pos=%d len=%d\n", pos, len);
        res = httpd_resp_send_chunk(req, (const char *)(ringBuffer+pos), len);
        pos += len;
        totalSendBytes += len;
      }
      if(pos==RING_BUFFER_SIZE){
        pos = 0;
        printf("pos=0\n");
      }
    }
    if(res != ESP_OK){
      break;
    }
  }
  if(res == ESP_OK){
    res = httpd_resp_send_chunk(req, NULL, 0);
  }
  return res;
}

void startHttpd(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t wav_uri = {
    .uri       = "/wav",
    .method    = HTTP_GET,
    .handler   = wav_handler,
    .user_ctx  = NULL
  };
  if (httpd_start(&httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(httpd, &index_uri);
    httpd_register_uri_handler(httpd, &wav_uri);
  }
}

bool wifi_connect(int timeout_ms)
{
  if(WiFi.status() == WL_CONNECTED){
    return true;
  }
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("mac: %s\n", mac_string(mac).c_str());

  WiFi.begin();
  unsigned long start_ms = millis();
  bool connected = false;
  while(1){
    connected = WiFi.status() == WL_CONNECTED;
    if(connected || (start_ms+timeout_ms)<millis()){
      break;
    }
    delay(1);
  }
  return connected;
}

void wifi_disconnect(){
  if(WiFi.status() != WL_DISCONNECTED){
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
  }
}

String mac_string(const uint8_t *mac)
{
  char macStr[18] = { 0 };
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

void update_display()
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(3);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(1, 1);
  M5.Lcd.printf("AudioStreamingC\n");

  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  M5.Rtc.GetTime(&time);
  M5.Rtc.GetData(&date);
  M5.Lcd.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
    date.Year, date.Month, date.Date,
    time.Hours, time.Minutes, time.Seconds
  );

  IPAddress ipAddress = WiFi.localIP();
  M5.Lcd.printf("%s\n", ipAddress.toString().c_str());
}
