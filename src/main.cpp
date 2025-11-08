#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <stdio.h>

// -------- Pin assignments --------
constexpr uint8_t LED_DATA_PIN = 6;   // DIN on the LED strip
constexpr uint16_t LED_COUNT = 30;    // Change to match your strip length
constexpr uint8_t BT_RX_PIN = 2;      // HC-05 TX -> Arduino pin 2
constexpr uint8_t BT_TX_PIN = 3;      // HC-05 RX <- Arduino pin 3 (needs voltage divider)

// -------- Bluetooth configuration --------
constexpr unsigned long BLUETOOTH_BAUD = 9600;

// -------- Animation configuration --------
constexpr unsigned long RAINBOW_INTERVAL_MS = 40;

SoftwareSerial bluetooth(BT_RX_PIN, BT_TX_PIN);
Adafruit_NeoPixel strip(LED_COUNT, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);

enum class AnimationMode : uint8_t { Static, Rainbow };
AnimationMode currentMode = AnimationMode::Static;

uint8_t staticColor[3] = {0, 0, 0};  // R, G, B
uint8_t currentBrightness = 128;

uint8_t rainbowOffset = 0;
unsigned long lastRainbowUpdate = 0;

String commandBuffer;
String serialCommandBuffer;

// Forward declarations
void applyStaticColor();
void setStaticColor(uint8_t red, uint8_t green, uint8_t blue);
void startRainbow();
void updateRainbow();
void handleCommand(const String &command, Stream *responseStream);
void pollStream(Stream &stream, String &buffer, Stream *responseStream);
uint32_t colorFromHex(const String &hex);
uint32_t wheel(uint8_t position);

void setup() {
  Serial.begin(115200);
  bluetooth.begin(BLUETOOTH_BAUD);

  strip.begin();
  strip.clear();
  strip.setBrightness(currentBrightness);
  strip.show();

  // Provide feedback to the host and bluetooth client
  Serial.println(F("Bluetooth LED strip controller ready."));
  bluetooth.println(F("READY"));
}

void loop() {
  // Gather characters from the Bluetooth serial connection.
  pollStream(bluetooth, commandBuffer, &bluetooth);
  pollStream(Serial, serialCommandBuffer, &Serial);

  if (currentMode == AnimationMode::Rainbow) {
    updateRainbow();
  }
}

void applyStaticColor() {
  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    strip.setPixelColor(i, strip.Color(staticColor[0], staticColor[1], staticColor[2]));
  }
  strip.setBrightness(currentBrightness);
  strip.show();
}

void setStaticColor(uint8_t red, uint8_t green, uint8_t blue) {
  staticColor[0] = red;
  staticColor[1] = green;
  staticColor[2] = blue;

  currentMode = AnimationMode::Static;
  applyStaticColor();
}

void startRainbow() {
  currentMode = AnimationMode::Rainbow;
  lastRainbowUpdate = 0;  // Force immediate update
}

void updateRainbow() {
  const unsigned long now = millis();
  if (now - lastRainbowUpdate < RAINBOW_INTERVAL_MS) {
    return;
  }

  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    uint8_t colorIndex = (i * 256 / strip.numPixels() + rainbowOffset) & 0xFF;
    strip.setPixelColor(i, wheel(colorIndex));
  }

  strip.setBrightness(currentBrightness);
  strip.show();

  rainbowOffset = (rainbowOffset + 1) & 0xFF;
  lastRainbowUpdate = now;
}

void handleCommand(const String &rawCommand, Stream *responseStream) {
  if (rawCommand.isEmpty()) {
    return;
  }

  String command = rawCommand;
  command.trim();
  if (command.length() == 0) {
    return;
  }

  Serial.print(F("Received command: "));
  Serial.println(command);

  String response = F("OK");

  if (command.equalsIgnoreCase(F("OFF"))) {
    setStaticColor(0, 0, 0);
  } else if (command.equalsIgnoreCase(F("RAINBOW"))) {
    startRainbow();
  } else if (command.startsWith(F("#")) && command.length() == 7) {
    uint32_t color = colorFromHex(command.substring(1));
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    setStaticColor(r, g, b);
  } else if (command.startsWith(F("COLOR "))) {
    int r = 0, g = 0, b = 0;
    if (sscanf(command.c_str(), "COLOR %d %d %d", &r, &g, &b) == 3) {
      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);
      setStaticColor(static_cast<uint8_t>(r),
                     static_cast<uint8_t>(g),
                     static_cast<uint8_t>(b));
    } else {
      response = F("ERR");
    }
  } else if (command.startsWith(F("BRIGHT "))) {
    long level = command.substring(7).toInt();
    level = constrain(level, 0, 255);
    currentBrightness = static_cast<uint8_t>(level);

    if (currentMode == AnimationMode::Static) {
      applyStaticColor();
    }
  } else {
    response = F("ERR");
    Serial.println(F("Unknown command."));
  }

  if (responseStream != nullptr) {
    responseStream->println(response);
  }
}

void pollStream(Stream &stream, String &buffer, Stream *responseStream) {
  while (stream.available() > 0) {
    char incoming = stream.read();

    if (incoming == '\n') {
      handleCommand(buffer, responseStream);
      buffer = "";
    } else if (incoming != '\r') {
      if (buffer.length() < 48) {  // simple guard against runaway packets
        buffer += incoming;
      }
    }
  }
}

uint32_t colorFromHex(const String &hex) {
  char buffer[7];
  hex.toCharArray(buffer, sizeof(buffer));
  return strtoul(buffer, nullptr, 16);
}

uint32_t wheel(uint8_t position) {
  position = 255 - position;
  if (position < 85) {
    return strip.Color(255 - position * 3, 0, position * 3);
  }
  if (position < 170) {
    position -= 85;
    return strip.Color(0, position * 3, 255 - position * 3);
  }
  position -= 170;
  return strip.Color(position * 3, 255 - position * 3, 0);
}