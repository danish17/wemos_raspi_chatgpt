#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "config.h"

#define LED_GREEN_PIN D1
#define LED_RED_PIN D2

const String chatGptUrl = "https://api.openai.com/v1/chat/completions";

// Initialize the OLED display using U8g2 library
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/D5, /* data=*/D6); // ESP8266 HW I2C with pin remapping

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

  // Serial.println("Connected to WiFi");
  // Serial.println(WiFi.localIP());

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

    // Serial.println("Received: " + receivedData);

    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("Request: ");
    u8g2.print(receivedData);
    u8g2.sendBuffer();

    String response = sendToChatGPT(receivedData);

    // Serial.println("ChatGPT Response: " + response);

    // Parse the JSON response to extract long and short answers
    DynamicJsonDocument responseJson(2048); // Increased buffer size
    DeserializationError error = deserializeJson(responseJson, response);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // Extract the content from the choices array
    String content = responseJson["choices"][0]["message"]["content"].as<String>();

    // Parse the content as JSON
    DynamicJsonDocument contentJson(1024);
    error = deserializeJson(contentJson, content);
    if (error)
    {
      Serial.print(F("Content deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // Correctly extract the long and short answers
    String longAnswer = contentJson["long"].as<String>();
    String shortAnswer = contentJson["short"].as<String>();

    // Display the short answer on the OLED screen
    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print(shortAnswer);
    u8g2.sendBuffer();

    // Send the long answer back to the Raspberry Pi via Serial
    Serial.println(longAnswer);

    // Delay to keep the response on the screen
    delay(5000); // Keep the response on the display for 5 seconds

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

  DynamicJsonDocument doc(4096);

  doc["model"] = "gpt-3.5-turbo";
  doc["max_tokens"] = 150; // Increased max_tokens

  // Construct the messages array with system message and user query
  JsonArray messages = doc.createNestedArray("messages");

  JsonObject systemMessage = messages.createNestedObject();
  systemMessage["role"] = "system";
  systemMessage["content"] = "You are a Home Assistant AI. Provide responses in JSON format with two fields: 'long' for detailed answers and 'short' for brief answers suitable for display on a small screen (0.96 inch OLED). These fields cannot be nested and need to contain a single string. This is an example response for query 'Speed of light': {\"long\": \"The speed of light is 299792458 meter per second\", \"short\": \"299792458 m/s\"}";

  JsonObject userMessage = messages.createNestedObject();
  userMessage["role"] = "user";
  userMessage["content"] = query;

  String requestBody;
  serializeJson(doc, requestBody);

  // Serial.println("Sending request to ChatGPT");
  // Serial.println(requestBody);

  int httpResponseCode = https.POST(requestBody);

  yield(); // Allow the ESP8266 to perform background tasks

  if (httpResponseCode > 0)
  {
    String response = https.getString();
    https.end();
    // Serial.println("HTTP Response code: " + String(httpResponseCode));
    // Serial.println("HTTP Response: " + response);

    return response; // Return the raw response from ChatGPT
  }
  else
  {
    // Serial.print("HTTP request failed with code: ");
    // Serial.println(httpResponseCode);
    https.end();
    return "Error in ChatGPT response: HTTP request failed";
  }
}