
#include <ESP8266WiFi.h>                // ESP8266 WiFi
#include <PubSubClient.h>               // MQTT
#include <FastLED.h>                    // FastLED library
#include <ArduinoJson.h>                // JSON deserialisation

#include "secrets.h"                    // WiFi Secrets (SSID and Password)

/****************************** Config */

#define MQTT_BROKER   "192.168.178.19"  // Hostname/IP of the broker
#define MQTT_PORT     1883              // Port of the Broker
#define MQTT_BASE     "ESPLIGHT"        // MQTT Basename
#define MAX_TOPICLEN  100               // Max. length of topic string
#define MAX_MSGLEN    100               // Max. length of message string
#define MAX_HNLEN     100               // Max. length of the hostname
#define NUM_LEDS      12                // Number of LEDs in the LED-Ring
#define LED_DATAPIN   4                 // DOUT to LED Ring (GPIO4 = D2)

#define LOOPTIME      1000              // Statistic updateloop in ms

/****************************** Globals */

typedef enum  {
  PAT_SOLID,        // All LEDs with Color 1
  PAT_RAINBOW,      // Rainbow
  PAT_WHITEGRAD,    // Color 1 to white gradient
  PAT_BLACKGRAD,    // Color 1 to black gradient
  PAT_COLGRAD,      // Color 1 to Color 2 gradient
  PAT_DOT,          // Single dot in Color 1
  PAT_SMOOTHDOT     // Single dot in Color 1 smoothed
} tyPatterns;

typedef enum  {
  EFF_SOLID,        // Simple, solid 
  EFF_BREATHE1,     // Sine-wave brightness
  EFF_BREATHE2,     // Sine-wave brightness with pause
  EFF_ROTATE,       // Rotate pattern
  EFF_GLITTER,      // Add glitter to the pattern
  EFF_RANDOM        // Randomize brightness
} tyEffects;

WiFiClient    m_espClient;              // The WiFi object
PubSubClient  m_Client(m_espClient);    // The MQTT object on WiFi
 
char          m_cBaseTopic[100] = "";   // Systems base topic

CRGB          m_Leds[NUM_LEDS];         // The LEDs
CRGB          m_Color1 = CRGB::Red;     // Color 1
CRGB          m_Color2 = CRGB::Green;   // Color 2
tyPatterns    m_Pattern = PAT_SOLID;    // Current LED Mode
tyEffects     m_Effect = EFF_SOLID;     // Current Effect Mode
uint16_t      m_delay=50;               // Loop-Delay
bool          m_FullRefresh = true;     // Trigger a full refresh after changing pattern, effect, etc

/****************************** The Functions */

/**
 * @brief Set a new Pattern
 * 
 * @param NewPattern 
 */
void SetPattern(tyPatterns NewPattern) {
  switch (m_Pattern) {
    default:
      m_Pattern = PAT_SOLID;
      // Fall through!
    case PAT_SOLID:
      fill_solid(m_Leds, NUM_LEDS, m_Color1);
      break;
    case PAT_RAINBOW:
      fill_rainbow(m_Leds, NUM_LEDS, 0, 21);
      break;
    case PAT_WHITEGRAD:
      fill_gradient_RGB(m_Leds, 0, CRGB::White, NUM_LEDS-1, m_Color1);
      break;
    case PAT_BLACKGRAD:
      fill_gradient_RGB(m_Leds, 0, CRGB::Black, NUM_LEDS-1, m_Color1);
      break;
    case PAT_COLGRAD:
      fill_gradient_RGB(m_Leds, 0, m_Color1, NUM_LEDS-1, m_Color2);
      break;
    case PAT_DOT:
      fill_solid(m_Leds, NUM_LEDS, CRGB::Black);
      m_Leds[NUM_LEDS/2] = m_Color1;
      break;
    case PAT_SMOOTHDOT:
      fill_solid(m_Leds, NUM_LEDS, CRGB::Black);
      m_Leds[NUM_LEDS/2] = m_Color1;
      blur1d(m_Leds, NUM_LEDS, 64);
      break;
  } // switch
  return;
}

/**
 * @brief The mqtt status worker
 */
void StatusWorker() {
  static unsigned long nextupdate = 0;

  // Publish status data every sec to MQTT
  if (millis() >= nextupdate) {
    nextupdate = millis() + LOOPTIME;

    char cTopic[MAX_TOPICLEN];
    char cMessage[MAX_MSGLEN];
    StaticJsonDocument<200> doc;

    doc["uptime"] = millis()/1000;
    doc["pattern"] = m_Pattern;
    doc["effect"] = m_Effect;

    doc["delay"] = m_delay;
    JsonArray data1 = doc.createNestedArray("color1");
    data1.add(m_Color1.r);
    data1.add(m_Color1.g);
    data1.add(m_Color1.b);

    JsonArray data2 = doc.createNestedArray("color2");
    data2.add(m_Color2.r);
    data2.add(m_Color2.g);
    data2.add(m_Color2.b);

    snprintf(&cTopic[0], MAX_TOPICLEN, "%s/stats", m_cBaseTopic);
    serializeJson(doc, cMessage);
    m_Client.publish(cTopic, cMessage);
  } // next update

  return;
}

/**
 * @brief The effect worker, called cyclic
 */
void EffectWorker() {
  static uint8_t effect_tick = 0;
  switch (m_Effect) {
    default:
      m_Effect = EFF_SOLID;
      // Fall through!
    case EFF_SOLID: 
      break;
    case EFF_BREATHE1:
      FastLED.setBrightness(quadwave8(effect_tick));
      effect_tick++; // Overflows!
      break;
    case EFF_BREATHE2:
      FastLED.setBrightness(quadwave8(effect_tick));
      effect_tick++; // Overflows!
      if (effect_tick == 0) FastLED.delay(1000);
      break;
    case EFF_ROTATE:
      {
        CRGB OverflowCol = m_Leds[0];
        for (int i = 0; i < NUM_LEDS-1; i++){        
          m_Leds[i]=m_Leds[i+1];
        }
        m_Leds[NUM_LEDS-1] = OverflowCol;
      }
      break;
    case EFF_GLITTER:
      {
        uint8_t RandomVal = random(0, 99);
        if (RandomVal<10) {  // 10 percent probability
          CRGB OldColor;
          uint8_t Position;

          Position = random(0, NUM_LEDS-1);
          OldColor = m_Leds[Position];
          m_Leds[Position] = CRGB::White;

          FastLED.delay(50);

          m_Leds[Position] = OldColor;
        }
      }
      break;
    case EFF_RANDOM: // Randomizer (fire effect)
      {
        // TODO Should be smoother
        for (uint8_t i=0; i<NUM_LEDS; i++) {
          uint8_t val = random(0,0xFF);
          m_Leds[i] = m_Color1;
          m_Leds[i].subtractFromRGB(val);
        }
      }
      break;
  } // switch
  return;
} // EffectWorker

/**
 * @brief Callback for received MQTT commands
 * 
 * @param topic 
 * @param payload 
 * @param length 
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  // JSON deserialisation
  StaticJsonDocument<200> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, payload);

  // Test if parsing succeeds.
  if (error) {
    return;
  }

  // Set Mode with { "setpattern": 1 }
  if (jsonDoc.containsKey("setpattern")) {
    m_Pattern = jsonDoc["setpattern"].as<tyPatterns>();
    m_FullRefresh = true;
  }

  // Set Mode with { "seteffect": 1 }
  if (jsonDoc.containsKey("seteffect")) {
    m_Effect = jsonDoc["seteffect"].as<tyEffects>();
    m_FullRefresh = true;
  }

  // Set Color 1 with { "setcolor1": [255,255,0] }
  if (jsonDoc.containsKey("setcolor1")) {
    m_Color1.r = jsonDoc["setcolor1"][0].as<int>();
    m_Color1.g = jsonDoc["setcolor1"][1].as<int>();
    m_Color1.b = jsonDoc["setcolor1"][2].as<int>();
    m_FullRefresh = true;
  }

  // Set Color 2 with { "setcolor2": [255,255,0] }
  if (jsonDoc.containsKey("setcolor2")) {
    m_Color2.r = jsonDoc["setcolor2"][0].as<int>();
    m_Color2.g = jsonDoc["setcolor2"][1].as<int>();
    m_Color2.b = jsonDoc["setcolor2"][2].as<int>();
    m_FullRefresh = true;
  }

  // Set Delay with { "setdelay": 1000 }
  if (jsonDoc.containsKey("setdelay")) {
    m_delay = jsonDoc["setdelay"].as<int>();
  }

  // Set Brightness with { "setbright": 50 }
  if (jsonDoc.containsKey("setbright")) {
    FastLED.setBrightness(jsonDoc["setbright"].as<int>());
  }

} // mqtt_callback

/**
 * @brief Reconnect to the broker if not connected. Not blocking
 */
void mqtt_reconnect() {
  if (!m_Client.connected()) {
    if (m_Client.connect("ESP8266")) {
      char cTopic[MAX_TOPICLEN];
      snprintf(&cTopic[0], MAX_TOPICLEN, "%s/cmd", m_cBaseTopic);
      m_Client.subscribe(cTopic);
    }
  }
} // mqtt_reconnect

/**
 * @brief System setup
 */
void setup() {
  // WiFi (Client only)
  WiFi.mode(WIFI_STA);

  // Generate Wifi Hostname with Basename and Chip-ID
  char cBuffer[MAX_HNLEN];
  snprintf(&cBuffer[0], MAX_HNLEN, "%s_%X", MQTT_BASE, system_get_chip_id() );
  WiFi.hostname(cBuffer);
  WiFi.begin(ssid, password);

  // Wait for WiFi connection
  // TODO: Add timeout with system reset
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // MQTT Basename plus Chip-ID
  snprintf(&m_cBaseTopic[0], MAX_TOPICLEN, "%s_%X", MQTT_BASE, system_get_chip_id() );
  m_Client.setServer(MQTT_BROKER, MQTT_PORT);
  m_Client.setCallback(mqtt_callback);

  // FastLED Setup
  FastLED.addLeds<NEOPIXEL, LED_DATAPIN>(m_Leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  fill_solid(m_Leds, NUM_LEDS, m_Color1);

  // Random Seed
  randomSeed(analogRead(0));

} // setup

/**
 * @brief Arduino system loop 
 */
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!m_Client.connected()) {
      mqtt_reconnect();
    } else {
      m_Client.loop();          // MQTT client loop
      StatusWorker();           // MQTT status

      if (m_FullRefresh) {      // Update LED pattern if necessary
        SetPattern(m_Pattern);
        m_FullRefresh = false;
      }
      EffectWorker();           // Effect worker

      FastLED.delay(m_delay);   // Also triggers a .show()
    } // Client.connected
  } else {
    // TODO: If not connected, retry and/or restart system
  }
} // loop
