
#include <TimeLib.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <esp_camera.h>
#include <Arduino.h>
#include <BME280.h>


#include <soc/rtc_cntl_reg.h>
#include <esp_task_wdt.h>

#include "ap_settings.h"
#include "twitter_settings.h"
#include "camera_pins.h"



// Abilita o meno la visualizzazione di alcuni messaggi di Debug
#define DEBUG_MODE false


// Abilita o meno il Flash a termine del Twitt
#define FLASH_AT_END true



RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int NTP_set = 0;




/* Fattore di conversione micro secondi a seconds */
#define uS_TO_S_FACTOR 1000000ULL

// Durata del periodo in  modalità Low Power in Secondi max 7200s
long SleepDuration = 60 ;

// Ora di risveglio dalla modalità Low Power
int  ShootAt     =  15;






unsigned long StartTime  = 0;



struct CurTime {
  int  CurrentHour = 0;
  int  CurrentMin = 0;
  int  CurrentSec = 0;
};



#define P_BUTTON GPIO_NUM_13
#define I2C_SDA  GPIO_NUM_16
#define I2C_SCL  GPIO_NUM_14




#define SHA1_SIZE 20

extern "C" {
  typedef struct {
    uint32_t Intermediate_Hash[SHA1_SIZE / 4]; /* Message Digest */
    uint32_t Length_Low;            /* Message length in bits */
    uint32_t Length_High;           /* Message length in bits */
    uint16_t Message_Block_Index;   /* Index into message block array   */
    uint8_t Message_Block[64];      /* 512-bit message blocks */
  } SHA1_CTX;

  void SHA1Init(SHA1_CTX *);
  void SHA1Update(SHA1_CTX *, const uint8_t * msg, int len);
  void SHA1Final(uint8_t *digest, SHA1_CTX *);
}




// Parametri Wi-Fi
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;



// Parametri Twittert
char consumer_key[]     = API_KEY;
char consumer_secret[]  = API_KEY_SECRET;
char access_token[]     = ACCESS_TOKEN;
char access_secret[]    = ACCESS_TOKEN_SECRET;
char base_host[]        = API_BASE_HOST;
char base_url[]         = API_BASE_URL;
char base_uri[]         = API_BASE_URI;

char message_array[141];



// Porta comunicazione Https
int  httpsPort          = 443;

// Timeout comunicazione con server Twitter
int  connection_timeout = 35000;



char* tweeter[6] = { consumer_key, consumer_secret, access_token, access_secret, base_host, base_url };

const char key_http_method[]        = "POST";
const char key_consumer_key[]       = "oauth_consumer_key";
const char key_nonce[]              = "oauth_nonce";
const char key_signature_method[]   = "oauth_signature_method";
const char key_timestamp[]          = "oauth_timestamp";
const char key_token[]              = "oauth_token";
const char key_version[]            = "oauth_version";
const char key_status[]             = "status";
const char key_signature[]          = "oauth_signature";
const char value_signature_method[] = "HMAC-SHA1";
const char value_version[]          = "1.0";
const char key_media_ids[]          = "media_ids";


const char* keys[12] = {key_http_method, key_consumer_key, key_nonce, key_signature_method, key_timestamp, key_token, key_version, key_status, key_signature, value_signature_method, value_version, key_media_ids};



char upload_base_host[]    = UPLOAD_BASE_HOST;
char upload_base_url[]     = UPLOAD_BASE_URL;
char upload_base_uri[]     = UPLOAD_BASE_URI;

char upload_oauth_key[]    = "oauth_body_hash";
char* imagearray[4]        = { upload_base_host, upload_base_url, upload_base_uri, upload_oauth_key };


bool twitt_done = false;



// Buffer per immagine Camera
camera_fb_t * fb = NULL;

uint8_t * fbBuf = NULL;
size_t fb_len = 0;

Bme280 bme280;

WiFiClientSecure client;





void setup() {

  bool result = false;

  Serial.begin(115200);

  pinMode(P_BUTTON, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  esp_sleep_enable_ext0_wakeup(P_BUTTON, 0);

  ++bootCount;

  Serial.println("Boot number: " + String(bootCount));
  Serial.println("NTP_set: " + String(NTP_set));

  print_wakeup_reason();

  if (NTP_set == 0 || bootCount%5== 0) {
   wifi_connect();
   Sync_With_NTP();
   ShowNtpTime();
  }

}




void loop() {
  // Acquisisce l'ora corrente
  CurTime ct = getTime();
  /*
    Serial.print("CurrentHour:");
    Serial.println(ct.CurrentHour);
    Serial.print("CurrentMinutes:");
    Serial.println(ct.CurrentMin);
    Serial.print("CurrentSeconds:");
    Serial.println(ct.CurrentSec);
  */
  // Comparo l'ora corrente con l'ora di invio Twitt
  if (ct.CurrentHour == ShootAt) {
    if (WiFi.status() == WL_CONNECTED) {
      if (! twitt_done ) {
        inizialize_CAM();
        twitt_done = send_Twitt();
        BeginSleep();
      }
    } else {
      wifi_connect();
    }
  } else {
    Serial.println("NOT IN TIME WINDOW");
    BeginSleep();
  }
}





CurTime getTime() {

  CurTime ct;

  struct tm timeinfo;

  setenv("TZ", "CET-1", 1);
  //setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);


  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  } else {
    ct.CurrentHour = timeinfo.tm_hour;
    ct.CurrentMin  = timeinfo.tm_min;
    ct.CurrentSec  = timeinfo.tm_sec;
  }

  //Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");


  return ct;

}




void setupBME280() {

  Serial.println();
  Serial.println("############################################");
  Serial.println("START SETUP BME280");

  if (!bme280.begin(I2C_SDA, I2C_SCL)) {
    Serial.printf("BME280 init failed.\n");
  } else {
    Serial.println("Sensor BME280 Inizilized");
  }

  Serial.println("STOP SETUP BME280");
  Serial.println("############################################");
  Serial.println();
}

char* readBME280() {
  Serial.println();
  Serial.println("############################################");
  Serial.println("START READ BME280 DATA");

  float temp, hum, pres;

  if (bme280.measure(temp, pres, hum)) {

    Serial.printf("T = %0.2f C, P = %0.2f Pa, H = %0.2f%%\n", temp, pres, hum);

    snprintf(message_array, sizeof(message_array), "Buongiorno dall'orto di Mario! T = %0.2f C, P = %0.2f Pa, H = %0.2f%%\n", temp, pres, hum);

  } else {
    Serial.printf("Measurement failed.\n");
  }

  Serial.println("STOP READ BME280 DATA");
  Serial.println("############################################");
  Serial.println();

  return message_array;

}




char* getMessage() {
  String message = "TEST Message: ";
  message = message + now();
  int message_len = message.length() + 1;
  message.toCharArray(message_array, message_len);
  return message_array;
}


void disable_CAM() {
  esp_camera_deinit();
  void gpio_uninstall_isr_service(void);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(500);
}




bool send_Twitt() {
  bool result = false;

  if (!takeSnapShot() == ESP_OK ) {
    Serial.println("Failed To Take SnapShot");
    result = false;
  } else {
    result = true;
  }

  return result;
}




static esp_err_t takeSnapShot() {
  Serial.println();
  Serial.println("############################################");
  Serial.println("START TAKING SNAPSHOT");
  Serial.println();

  // Get the contents of the camera frame buffer
  fb = esp_camera_fb_get();

  fb_len = fb->len;
  Serial.printf("Image buffer size: %u Byte \n", (uint32_t)(fb_len) ) ;

  fbBuf = fb->buf;

  if (!fb) {
    Serial.println("Camera capture failed");
    return ESP_FAIL;
  }


  disable_CAM();

  delay(1000);

  setupBME280();

  do_tweet(readBME280());

  //do_tweet(getMessage());

  esp_camera_fb_return(fb);


  Serial.println();
  Serial.println("END TAKING SNAPSHOT");
  Serial.println("############################################");
  Serial.println();

  return ESP_OK;

}






void do_tweet(char* message_array) {
  String media_id_to_associate = tweeting(message_array , "0");

  if (media_id_to_associate != "0") {
    media_id_to_associate = tweeting(message_array, media_id_to_associate);
  }
}





String tweeting( char* Twitt_Text, String media_id_to_associate) {
  bool post_image = false;

  size_t fb_len = 0;
  size_t filesize = fb->len;

  if (media_id_to_associate == "0" ) {
    post_image = true;
  }

  time_t now;

  uint32_t value_timestamp  = time(&now);

  //Serial.println("---------------------------------------------");
  //Serial.println(value_timestamp);
  //Serial.println("---------------------------------------------");


  uint32_t value_nonce = *(volatile uint32_t *)0x3FF20E44;

  String status_all = make_status_all(Twitt_Text);

  if (post_image) {

    Serial.println();
    Serial.println("############################################");
    Serial.println("START TWEETING IMAGE");


    String content_more = "--00Twurl15632260651985405lruwT99\r\n" ;
    content_more += "Content-Disposition: form-data; name=\"media\"; filename=\"" + String(value_timestamp) + ".jpg\"\r\n" ;
    content_more += "Content-Type: application/octet-stream\r\n\r\n";
    //content_more += "Content-Type: image/jpeg\r\n\r\n";
    String content_last = "\r\n--00Twurl15632260651985405lruwT99--\r\n";
    int content_length = filesize + content_more.length() + content_last.length();

    String hashStr  = get_hash_post (true, content_more, content_last);

    String para_string     = make_para_string(status_all, value_nonce, value_timestamp, hashStr, "0");
    String base_string     = make_base_string(para_string, hashStr);
    String oauth_signature = make_signature(tweeter[1], tweeter[3], base_string);
    String OAuth_header    = make_OAuth_header(oauth_signature, value_nonce, value_timestamp, hashStr);

#if (DEBUG_MODE)
    Serial.println();
    Serial.print("[Oauth] para_string : ");
    Serial.println(para_string);
    Serial.print("[Oauth] base_string : ");
    Serial.println(base_string);
    Serial.print("[Oauth] hashStr : ");
    Serial.println(hashStr);
    Serial.print("[Oauth] oauth_signature : ");
    Serial.println(oauth_signature);
    Serial.print("[Oauth] OAuth_header : ");
    Serial.println(OAuth_header);
    Serial.println();
#endif

    String content_header = "POST " + String(imagearray[2]) + " HTTP/1.1\r\n";
    content_header += "Accept-Encoding: identity;q=1,chunked;q=0.1,*;q=0\r\n";
    content_header += "User-Agent: ESP32CAM\r\n";
    content_header += "Content-Type: multipart/form-data, boundary=\"00Twurl15632260651985405lruwT99\"\r\n";
    content_header += "Authorization: " + OAuth_header + "\r\n";
    content_header += "Connection: close\r\n";
    content_header += "Host: " + String(imagearray[0]) + "\r\n";
    content_header += "Content-Length: " + String(content_length) + "\r\n\r\n";

#if (DEBUG_MODE)
    Serial.println();
    Serial.println("content_header: ");
    Serial.println(content_header);

    Serial.println("content_more: ");
    Serial.println(content_more);

    Serial.println("content_last: ");
    Serial.println(content_last);
#endif


    String media_id =  do_http_image_post(tweeter[4], OAuth_header, content_header, content_more, content_last);


    Serial.println();
    Serial.println("END START TWEETING IMAGE");
    Serial.println("############################################");
    Serial.println();

    return media_id;

  } else {

    Serial.println();
    Serial.println("############################################");
    Serial.println("START TWEETING TEXT");

    String para_string     = make_para_string(status_all, value_nonce, value_timestamp, "", media_id_to_associate);
    String base_string     = make_base_string(para_string, "");
    String oauth_signature = make_signature(tweeter[1], tweeter[3], base_string);
    String OAuth_header    = make_OAuth_header(oauth_signature, value_nonce, value_timestamp, "");

#if (DEBUG_MODE)
    Serial.println();
    Serial.print("[Oauth] request_body: ");
    Serial.println(status_all);

    Serial.print("[Oauth] para_string: ");
    Serial.println(para_string);

    Serial.print("[Oauth] base_string: ");
    Serial.println(base_string);

    Serial.print("[Oauth] oauth_signature: ");
    Serial.println(oauth_signature);

    Serial.print("[Oauth] OAuth_header: ");
    Serial.println(OAuth_header);
#endif


    String result = do_http_text_post(tweeter[4], OAuth_header, status_all, media_id_to_associate);

    Serial.println();
    Serial.println("END TWEETING TEXT");
    Serial.println("############################################");
    Serial.println();

    return result;

  }

}




String get_hash_post( bool body_hash, String content_more, String content_last) {

  int chunk_size = 1024;
  int remainder = 0;
  int counter = 0;

  //printf("fbBuf address is %d \n",&fbBuf);

  if (body_hash) {

    uint8_t digestkey[32];
    SHA1_CTX context;
    SHA1Init(&context);

    SHA1Update(&context, (uint8_t*) content_more.c_str(), content_more.length());

    for (size_t n = 0; n < fb_len; n = n + chunk_size) {

      if (n + chunk_size < fb_len) {

        SHA1Update(&context, (uint8_t*) fbBuf, chunk_size);
        fbBuf += chunk_size;
        counter += chunk_size;

      } else if (fb_len - n + chunk_size > 0) {

        remainder = fb_len - n ;
        SHA1Update(&context, (uint8_t*) fbBuf, remainder);

      }
    }


    SHA1Update(&context, (uint8_t*) content_last.c_str(), content_last.length());
    SHA1Final(digestkey, &context);


    // Faccio puntare il Puntatore al Buffer alla sua posizione di partenza
    fbBuf = fbBuf - counter;
    //printf("fbBuf address is %d \n",&fbBuf);


    return URLEncode(base64::encode(digestkey, 20).c_str());

  } else {

    client.print(content_more);

    for (size_t n = 0; n < fb_len; n = n + chunk_size) {

      if (n + chunk_size < fb_len) {

        client.write((uint8_t *)fbBuf , chunk_size);
        fbBuf += chunk_size;

      } else if (fb_len - n + chunk_size > 0) {

        remainder = fb_len - n ;
        client.write((uint8_t *)fbBuf, remainder);

      }
    }

    client.print(content_last);

    return "1";

  }

}





String do_http_image_post( const char* basehost, String OAuth_header, String content_header, String content_more, String content_last) {
  int r = 0;
  client.setTimeout(connection_timeout);
  client.setCACert(root_ca);
  client.setNoDelay(1);

  Serial.println();
  Serial.println("Connecting to: ");
  Serial.print(imagearray[0]);
  Serial.println();

  while ((!client.connect(imagearray[0], httpsPort)) && (r < 30)) {
    delay(100);
    Serial.print(".");
    r++;
  }

  if (r == 30) {
    Serial.println();
    Serial.println("Connection Failed");
    Serial.println();
    return "0";
  } else {
    Serial.println();
    Serial.println("Connected");
    Serial.println();
  }

  client.print(content_header);

  get_hash_post( false, content_more, content_last);


  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      #if (DEBUG_MODE)
        Serial.println("headers received");
      #endif
      break;
    }
  }

  

  String line = client.readStringUntil('\n');

  #if (DEBUG_MODE)
    Serial.print("Response: [");
    Serial.print(line);
    Serial.println("]");
    Serial.println();
  #endif

  StaticJsonDocument<300> doc;
  auto error = deserializeJson(doc, line);
  if (error) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
    client.stop();
    return "0";
  }


  const char* media_id = doc["media_id_string"];


  Serial.print("MEDIA ID: ");
  Serial.println(media_id);


  if (error) {
    Serial.println(error.c_str());
    Serial.println();
    client.stop();
    return "0";
  } else {

    if (isNumeric(media_id)) {
      Serial.println("POST Done!");
    } else {
      client.stop();
      return "0";
    }
    
    return media_id;
    
  }


  client.stop();

  return "0";
}




String do_http_text_post(const char* basehost, String OAuth_header, String message, String media_id_to_associate) {
  int r = 0;
  String uri_to_post = base_uri;
  uri_to_post += "?media_ids=";
  uri_to_post += media_id_to_associate;

  client.setTimeout(connection_timeout);
  client.setCACert(root_ca);
  client.setNoDelay(1);

  Serial.println();
  Serial.println("Connecting to:");
  Serial.print(basehost);
  Serial.println();


  while ((!client.connect(basehost, httpsPort)) && (r < 30)) {
    delay(100);
    Serial.print(".");
    r++;
  }


  if (r == 30) {
    Serial.println();
    Serial.println("Connection Failed");
    Serial.println();
  } else {
    Serial.println();
    Serial.println("Connected");
    Serial.println();
  }



#if (DEBUG_MODE)
  Serial.print("Host: ");
  Serial.println(basehost);
  Serial.println();
  Serial.print("uri_to_post: ");
  Serial.println(uri_to_post);
  Serial.println();
  Serial.print("message: ");
  Serial.println(message);
#endif


  String postRequest = "POST " + uri_to_post + " HTTP/1.1\r\n" +
                       "Host: " + basehost + "\r\n" +
                       "Authorization:" + OAuth_header + "\r\n" +
                       "Content-Type: application/x-www-form-urlencoded\r\n" +
                       "Content-Length: " + message.length() + "\r\n\r\n" +
                       message + "\r\n";

  client.print(postRequest);

  while (client.connected()) {
    String header = client.readStringUntil('\n');
    if (header == "\r") {
      break;
    }
  }


  String line = client.readStringUntil('\n');

#if (DEBUG_MODE)
  Serial.println(line);
#endif


  if (line.indexOf("created_at") > 0) {

    Serial.println("POST Done!");

#if (FLASH_AT_END)
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(300);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
#endif

  }


  client.stop();

  return "0";

}





bool Sync_With_NTP() {
  bool result = false;
  Serial.println();
  Serial.println("############################################");
  Serial.println("START INIZIALIZE NTP");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServerName);

  ++NTP_set;

  Serial.println("END INIZIALIZE NTP");
  Serial.println("############################################");
  Serial.println();

  return result;
}




void ShowNtpTime() {
  struct tm timeinfo;

  Serial.println();
  Serial.println("############################################");
  Serial.println("START SHOW TIME");
  
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  Serial.println();
  Serial.println("############################################");
  Serial.println("START SHOW TIME");
}





void wifi_connect() {
  Serial.println();
  Serial.println("############################################");
  Serial.println("START INIZIALIZE WI-FI");
  Serial.println();

  Serial.print("[WIFI] start millis : ");
  Serial.println(millis());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int Attempt = 0;

  while (WiFi.status() != WL_CONNECTED) {

    delay(100);

    Attempt++;

    Serial.print(".");
    if (Attempt % 50 == 0) {
      Serial.println();
    }

    if (Attempt == 300) {
      Serial.println();
      Serial.println("Too many attempts... Restart");
      Serial.println();
      delay(2000);
      hard_restart();
    }
  }

  Serial.println();
  Serial.print("[WIFI] connected millis : ");
  Serial.print(millis());
  Serial.println();

  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());

  Serial.println();
  Serial.println("END INIZIALIZE WI-FI");
  Serial.println("############################################");
  Serial.println();
}




bool inizialize_CAM() {
  bool result = false;
  Serial.println();
  Serial.println("############################################");
  Serial.println("START INIZIALIZE CAM");

  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(2000);


  // Disabilita il monitoraggio delle sottotensioni "brownout detector"
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);


  // Da abilitare per Debug
  //Serial.setDebugOutput(true);
  //Serial.println();


  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //config.pixel_format = CAMERA_PF_JPEG,


  /*
    Lista possibili Risoluzioni e Formati

    FRAMESIZE_UXGA (1600 x 1200)
    FRAMESIZE_QVGA (320 x 240)
    FRAMESIZE_CIF (352 x 288)
    FRAMESIZE_VGA (640 x 480)
    FRAMESIZE_SVGA (800 x 600)
    FRAMESIZE_XGA (1024 x 768)
    FRAMESIZE_SXGA (1280 x 1024)

    The image quality (jpeg_quality) can be a number between 0 and 63.
    A lower number means a higher quality. However, very low numbers for image quality,
    specially at higher resolution can make the ESP32-CAM to crash or it may not be able to take the photos properly.
  */


  /*
    if(psramFound()){
    Serial.println("PSRAM FOUND");
    Serial.println();
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 11;
    config.fb_count = 2;
    } else {
    Serial.println("PSRAM NOT FOUND");
    Serial.println();
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    }
  */

  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 11;
  config.fb_count = 2;



  // Inizilizza la  camera con i parameti settati sopra
  esp_err_t err = esp_camera_init(&config);



  /*
    Parametri aggiuntivi per configurare la ripresa della camera

    sensor_t * s = esp_camera_sensor_get();
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_ae_level(s, 0);       // -2 to 2
    s->set_aec_value(s, 300);    // 0 to 1200
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable

  */


  if (err == ESP_OK) {
    Serial.println("Camera inizialized");
    result = true;
  } else {
    Serial.printf("Camera init failed with error 0x%x", err);
    result = false;
  }

  Serial.println("END INIZIALIZE CAM");
  Serial.println("############################################");
  Serial.println();

  return result;
}




String make_status_all(const char* Twitt_Text) {
  String status_all = keys[7];
  status_all += "=";
  status_all += URLEncode(Twitt_Text);
  return status_all;
}




String make_para_string(String status_all, uint32_t value_nonce, uint32_t value_timestamp, String hashStr, String media_id_to_associate) {
  String para_string;

  if (media_id_to_associate != "0") {
    para_string += keys[11];
    para_string += "=" ;
    para_string += media_id_to_associate;
    para_string += "&";
  }

  if (hashStr.length() > 0 ) {
    para_string += imagearray[3];
    para_string += "=" ;
    para_string += hashStr.c_str();
    para_string += "&";
  }

  para_string += keys[1];
  para_string += "=" ;
  para_string += tweeter[0];
  para_string += "&";
  para_string += keys[2];
  para_string += "=";
  para_string += value_nonce;
  para_string += "&";
  para_string += keys[3];
  para_string += "=";
  para_string += keys[9];
  para_string += "&";
  para_string += keys[4];
  para_string += "=";
  para_string += value_timestamp;
  para_string += "&";
  para_string += keys[5];
  para_string += "=";
  para_string += tweeter[2];
  para_string += "&";
  para_string += keys[6];
  para_string += "=";
  para_string += value_version;
  if (hashStr.length() == 0) {
    para_string += "&";
    para_string += status_all;
  }
  return para_string;
}





String make_base_string(String para_string, String hashStr) {
  char para_string_array[500];
  int para_string_len = para_string.length() + 1;
  para_string.toCharArray(para_string_array, para_string_len);

  if (hashStr.length() > 0) {
    String base_string = keys[0];
    base_string += "&";
    base_string += URLEncode(imagearray[1]);
    base_string += "&";
    base_string += URLEncode(para_string.c_str());
    return base_string;
  }

  String base_string = keys[0];
  base_string += "&";
  base_string += URLEncode(tweeter[5]);
  base_string += "&";
  base_string += URLEncode(para_string.c_str());

  return base_string;
}





String make_OAuth_header(String oauth_signature, uint32_t value_nonce, uint32_t value_timestamp, String hashStr) {
  String OAuth_header = "OAuth ";

  if (hashStr.length() > 0) {
    OAuth_header += imagearray[3];
    OAuth_header += "=\"";
    OAuth_header += hashStr;
    OAuth_header += "\", ";
  }

  OAuth_header += keys[1];
  OAuth_header += "=\"";
  OAuth_header += tweeter[0];
  OAuth_header += "\", ";
  OAuth_header += keys[2];
  OAuth_header += "=\"";
  OAuth_header += value_nonce;
  OAuth_header += "\", ";
  OAuth_header += keys[8];
  OAuth_header += "=\"";
  OAuth_header += oauth_signature;
  OAuth_header += "\", ";
  OAuth_header += keys[3];
  OAuth_header += "=\"";
  OAuth_header += keys[9];
  OAuth_header += "\", ";
  OAuth_header += keys[4];
  OAuth_header += "=\"";
  OAuth_header += value_timestamp;
  OAuth_header += "\", ";
  OAuth_header += keys[5];
  OAuth_header += "=\"";
  OAuth_header += tweeter[2];
  OAuth_header += "\", ";
  OAuth_header += keys[6];
  OAuth_header += "=\"";
  OAuth_header += keys[10];
  OAuth_header += "\"";
  return OAuth_header;
}





String make_signature(const char* secret_one, const char* secret_two, String base_string) {
  String signing_key = URLEncode(secret_one);
  signing_key += "&";
  signing_key += URLEncode(secret_two);

  //Serial.println(signing_key);

  uint8_t digestkey[32];
  SHA1_CTX context;
  SHA1Init(&context);
  SHA1Update(&context, (uint8_t*) signing_key.c_str(), (int)signing_key.length());
  SHA1Final(digestkey, &context);

  uint8_t digest[32];
  ssl_hmac_sha1((uint8_t*) base_string.c_str(), (int)base_string.length(), digestkey, SHA1_SIZE, digest);

  String oauth_signature = URLEncode(base64::encode(digest, SHA1_SIZE).c_str());
  //Serial.println(oauth_signature);

  return oauth_signature;
}





String URLEncode(String data_to_encode) {
  char msgarray[500];
  int data_to_encode_len = data_to_encode.length() + 1;
  data_to_encode.toCharArray(msgarray, data_to_encode_len);
  char* msg = msgarray;

  const char *hex = "0123456789ABCDEF";
  String encodedMsg = "";

  while (*msg != '\0') {
    if ( ('a' <= *msg && *msg <= 'z')
         || ('A' <= *msg && *msg <= 'Z')
         || ('0' <= *msg && *msg <= '9')
         || *msg  == '-' || *msg == '_' || *msg == '.' || *msg == '~' ) {
      encodedMsg += *msg;
    } else {
      encodedMsg += '%';
      encodedMsg += hex[*msg >> 4];
      encodedMsg += hex[*msg & 0xf];
    }
    msg++;
  }
  return encodedMsg;
}





void ssl_hmac_sha1(const uint8_t *msg, int length, const uint8_t *key, int key_len, uint8_t *digest) {
  SHA1_CTX context;
  uint8_t k_ipad[64];
  uint8_t k_opad[64];
  int i;

  memset(k_ipad, 0, sizeof k_ipad);
  memset(k_opad, 0, sizeof k_opad);
  memcpy(k_ipad, key, key_len);
  memcpy(k_opad, key, key_len);

  for (i = 0; i < 64; i++) {
    k_ipad[i] ^= 0x36;
    k_opad[i] ^= 0x5c;
  }

  SHA1Init(&context);
  SHA1Update(&context, k_ipad, 64);
  SHA1Update(&context, msg, length);
  SHA1Final(digest, &context);
  SHA1Init(&context);
  SHA1Update(&context, k_opad, 64);
  SHA1Update(&context, digest, SHA1_SIZE);
  SHA1Final(digest, &context);
}






void gotoSleep() {
  delay(2000);
  Serial.println("Going to sleep now");
  delay(2000);
  esp_deep_sleep_start();
  Serial.println("Questa riga non verrà mai stampata");
}



void disableInterrupt() {
  void gpio_uninstall_isr_service(void);
}




void print_FreeHeap() {
  // Visualizza lo spazio di memoria libero nell Heap
  // utile per debuggare
  Serial.print("[HEAP] mills : ");
  Serial.print(millis());
  Serial.print(" - heap : ");
  Serial.println(ESP.getFreeHeap());
  Serial.flush();
}



bool isNumeric(String str) {
  for (byte i = 0; i < str.length(); i++) {
    if (isDigit(str.charAt(i))) return true;
  }
  return false;
}


void printDigits(int digits) {
  Serial.print(": ");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}



void hard_restart() {
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true);
}





void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  Serial.flush();
}






void BeginSleep() {

  CurTime ct = getTime();

  //Some ESP32 are too fast to maintain accurate time
  //long SleepTimer = (SleepDuration * 60 - ((ct.CurrentMin % SleepDuration) * 60 + ct.CurrentSec));

  long SleepTimer = SleepDuration;

  esp_err_t deepSleepTimerEnabled = esp_sleep_enable_timer_wakeup(SleepTimer * uS_TO_S_FACTOR);


  Serial.println();
  if (deepSleepTimerEnabled == ESP_OK) {
    Serial.println("Will wake up thanks to timer");
  } else {
    Serial.println("ERROR WHILE SETTING UP THE TIMER");
  }

  Serial.println();
  Serial.println("############################################");
  Serial.println("Entering " + String(SleepTimer) + " - secs of sleep time");
  Serial.println("Awake for : " + String((millis() - StartTime) / 1000.0, 3) + " secs");
  Serial.println("Starting deep-sleep period...");
  Serial.println("############################################");

  esp_deep_sleep_start();

}
