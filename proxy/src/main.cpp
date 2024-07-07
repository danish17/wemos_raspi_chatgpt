#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <CircularBuffer.h>
#include "config.h"

#define LED_GREEN_PIN D1
#define LED_RED_PIN D2

const String chatGptUrl = "https://api.openai.com/v1/chat/completions";

// Initialize the OLED display using U8g2 library
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/D5, /* data=*/D6); // ESP8266 HW I2C with pin remapping

// Define a structure to hold message information
struct Message
{
  String role;
  String content;
};

// Create a circular buffer to store the last 10 messages
CircularBuffer<Message, 20> messageHistory; // 20 to account for both user and assistant messages

// Function prototypes
String sendToChatGPT(const String &query);

void setup()
{
  ESP.wdtDisable();
  ESP.wdtEnable(WDTO_8S);

  Serial.begin(115200);
  WiFi.begin(ssid, password);

  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_RED_PIN, LOW);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    yield();
  }

  // Initialize the OLED display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
  u8g2.drawStr(0, 10, "Listening...");
  u8g2.sendBuffer();
}

void loop()
{
  yield(); // Allow the ESP8266 to perform background tasks

  if (Serial.available() > 0)
  {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, HIGH);

    String receivedData = Serial.readStringUntil('\n');

    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("Request: ");
    u8g2.print(receivedData);
    u8g2.sendBuffer();

    String response = sendToChatGPT(receivedData);

    DynamicJsonDocument responseJson(2048);
    DeserializationError error = deserializeJson(responseJson, response);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    String content = responseJson["choices"][0]["message"]["content"].as<String>();

    DynamicJsonDocument contentJson(1024);
    error = deserializeJson(contentJson, content);
    if (error)
    {
      Serial.print(F("Content deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    String longAnswer = contentJson["long"].as<String>();
    String shortAnswer = contentJson["short"].as<String>();

    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print(shortAnswer);
    u8g2.sendBuffer();

    Serial.println(longAnswer);

    delay(5000);

    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_RED_PIN, LOW);

    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("Listening...");
    u8g2.sendBuffer();
  }

  yield(); // Allow the ESP8266 to perform background tasks
}

String sendToChatGPT(const String &query)
{
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient https;
  https.begin(client, chatGptUrl);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + chatGptApiKey);

  DynamicJsonDocument doc(8192); // Increased size to accommodate message history

  doc["model"] = "gpt-3.5-turbo";
  doc["max_tokens"] = 150;

  JsonArray messages = doc.createNestedArray("messages");

  // Add system message
  JsonObject systemMessage = messages.createNestedObject();
  systemMessage["role"] = "system";
  systemMessage["content"] = "You are a Home Assistant AI. Provide responses in JSON format with two fields: 'long' for detailed answers and 'short' for brief answers suitable for display on a small screen (0.96 inch OLED). These fields cannot be nested and need to contain a single string. This is an example response for query 'Speed of light': {\"long\": \"The speed of light is 299792458 meter per second\", \"short\": \"299792458 m/s\"}";

  // Add message history
  for (int i = 0; i < messageHistory.size(); i++)
  {
    JsonObject historyMessage = messages.createNestedObject();
    historyMessage["role"] = messageHistory[i].role;
    historyMessage["content"] = messageHistory[i].content;
  }

  // Add current user message
  JsonObject userMessage = messages.createNestedObject();
  userMessage["role"] = "user";
  userMessage["content"] = query;

  String requestBody;
  serializeJson(doc, requestBody);

  int httpResponseCode = https.POST(requestBody);

  yield(); // Allow the ESP8266 to perform background tasks

  if (httpResponseCode > 0)
  {
    String response = https.getString();
    https.end();

    // Parse the response and add it to the message history
    DynamicJsonDocument responseJson(2048);
    DeserializationError error = deserializeJson(responseJson, response);
    if (!error)
    {
      String assistantResponse = responseJson["choices"][0]["message"]["content"].as<String>();

      // Add user query to history
      messageHistory.push(Message{"user", query});

      // Add assistant response to history
      messageHistory.push(Message{"assistant", assistantResponse});
    }

    return response;
  }
  else
  {
    https.end();
    return "Error in ChatGPT response: HTTP request failed";
  }
}
