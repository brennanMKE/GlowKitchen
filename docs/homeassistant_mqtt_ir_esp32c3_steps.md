# Home Assistant MQTT broker + IR remote trigger + ESP32-C3 lights (Step-by-step)

This guide sets up a simple flow:

**Raspberry Pi (IR receiver) → MQTT broker (Home Assistant) → ESP32-C3 boards (WS2812B lights)**

You will:
1. Confirm the MQTT broker is reachable.
2. Choose MQTT topics/payloads (a “contract”).
3. Make the Pi publish IR button presses to MQTT.
4. Make each ESP32-C3 subscribe and change LED scenes.

---

## 0) What you need

- Home Assistant running on your network (your broker appears to be at **192.168.88.254:1883**).
- MQTT credentials (username/password) that work in MQTT Explorer.
- Raspberry Pi with an IR receiver (any method: LIRC or Python IR library).
- One or more ESP32-C3 boards controlling your LEDs.
- PlatformIO in VS Code for ESP32 builds.
- A way to view serial logs (PlatformIO serial monitor).

---

## 1) Confirm MQTT broker access (PC + MQTT Explorer)

1. Open **MQTT Explorer**.
2. Create/select your connection:
   - **Host:** `192.168.88.254`
   - **Port:** `1883`
   - **Protocol:** `mqtt://`
   - **Username/Password:** the credentials you already have working
   - **TLS/Encryption:** OFF (port 1883 is typically plain MQTT)
3. Click **Connect**.
4. If it connects, you should see topics in the tree (like `homeassistant`, `tele`, `tasmota`, etc.).

### Test publish from MQTT Explorer
1. Find the publish button (or add a new topic/publish).
2. Publish:
   - **Topic:** `ir/remote/livingroom`
   - **Payload:** `SCENE_1`
3. You should see the topic update in the left topic tree.

If this works: the broker is fine.

---

## 2) Define your MQTT “contract” (topics + payloads)

Keep it simple and consistent.

### Recommended topics
- IR event topic (Pi publishes):
  - `ir/remote/livingroom`

- Light command topics (ESP32 subscribes):
  - Broadcast commands: `lights/all/cmd`
  - Per-room commands: `lights/livingroom/cmd`, `lights/kitchen/cmd`, etc.

### Recommended payloads (strings)
Start with plain strings because they are easy to debug:
- `ON`
- `OFF`
- `BRIGHT_UP`
- `BRIGHT_DOWN`
- `SCENE_1`
- `SCENE_2`
- `TOGGLE`

You can switch to JSON later once everything works.

---

## 3) Raspberry Pi: publish IR button presses to MQTT

There are two common approaches:

### Option A — You already use LIRC
- LIRC will tell you which button was pressed (like `KEY_1`, `KEY_POWER`, etc.).
- When you detect a button, publish a matching MQTT payload (like `SCENE_1`).

### Option B — You read IR codes in Python
- Your Python script prints or receives a hex code.
- Map that code to a payload, then publish.

Below is a **minimal MQTT publish snippet** you can drop into your existing IR code.

#### 3.1 Install Python MQTT library
```bash
python3 -m pip install paho-mqtt
```

#### 3.2 Minimal publisher function (Python)
Create a file on the Pi, for example: `ir_to_mqtt.py`

```python
import time
import paho.mqtt.client as mqtt

MQTT_HOST = "192.168.88.254"
MQTT_PORT = 1883
MQTT_USER = "mqtt"
MQTT_PASS = "YOUR_MQTT_PASSWORD"

TOPIC = "lights/all/cmd"  # or "lights/livingroom/cmd"

def publish_cmd(payload: str):
    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.publish(TOPIC, payload, qos=0, retain=False)
    client.disconnect()
    print(f"Published {TOPIC} -> {payload}")

if __name__ == "__main__":
    # Test publish:
    publish_cmd("SCENE_1")
    time.sleep(0.2)
    publish_cmd("OFF")
```

#### 3.3 Connect it to your IR receiver code
Wherever you currently detect a button press, call `publish_cmd(...)`.

Example mapping:
- POWER button → `TOGGLE`
- 1 button → `SCENE_1`
- 2 button → `SCENE_2`
- VOL+ → `BRIGHT_UP`
- VOL- → `BRIGHT_DOWN`

---

## 4) ESP32-C3: subscribe to MQTT and react (PlatformIO + Arduino)

### 4.1 PlatformIO project setup
1. Create/open your ESP32-C3 PlatformIO project.
2. Ensure you have the `PubSubClient` library.
3. Enable USB serial logging (useful for ESP32-C3).

#### Recommended `platformio.ini`
```ini
[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200

lib_deps =
  knolleary/PubSubClient

build_flags =
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
```

> If you have a different C3 board, you may need to change `board = ...`.

### 4.2 ESP32-C3 MQTT subscribe example (minimal)

Paste into `src/main.cpp` and fill in your Wi-Fi + MQTT credentials.

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "192.168.88.254";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "mqtt";
const char* MQTT_PASS = "YOUR_MQTT_PASSWORD";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Non-blocking reconnect timing
static const uint32_t MQTT_RETRY_MS = 2000;
uint32_t mqttRetryAt = 0;

// TODO: replace with your LED actions
void doScene1() { Serial.println("Scene 1"); }
void doScene2() { Serial.println("Scene 2"); }
void doOff()    { Serial.println("Off"); }
void doOn()     { Serial.println("On"); }

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];

  Serial.printf("MQTT %s -> %s\n", topic, msg.c_str());

  if (msg == "SCENE_1") doScene1();
  else if (msg == "SCENE_2") doScene2();
  else if (msg == "OFF") doOff();
  else if (msg == "ON") doOn();
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Quick connect attempt
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 3000) {
    delay(10);
  }

  Serial.printf("WiFi status=%d IP=%s\n", WiFi.status(), WiFi.localIP().toString().c_str());
}

void ensureMqtt() {
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  uint32_t now = millis();
  if (now < mqttRetryAt) return;
  mqttRetryAt = now + MQTT_RETRY_MS;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  String clientId = "esp32c3-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.println("MQTT connecting...");
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("MQTT connected; subscribing...");
    mqtt.subscribe("lights/all/cmd");
    mqtt.subscribe("lights/livingroom/cmd");  // change per device
  } else {
    Serial.printf("MQTT connect failed rc=%d\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  ensureWifi();
}

void loop() {
  ensureWifi();
  ensureMqtt();

  // Run your LED animation updates here with millis()-based timing (no long delays).
}
```

### 4.3 Test the ESP32 subscription
1. Flash the ESP32-C3.
2. Open PlatformIO serial monitor at 115200.
3. From MQTT Explorer publish:
   - Topic: `lights/all/cmd`
   - Payload: `SCENE_1`
4. You should see serial output:
   - `MQTT lights/all/cmd -> SCENE_1`

---

## 5) Recommended “bring-up” order (do this in order)

1. **Broker test:** MQTT Explorer connects and can publish/see topics.
2. **ESP32 test:** ESP32 logs show it connects to Wi-Fi, then MQTT.
3. **Manual command test:** Publish `SCENE_1` from MQTT Explorer and confirm ESP32 reacts.
4. **Pi test publish:** Run the Pi script once and confirm ESP32 reacts.
5. **IR mapping:** Connect IR button presses to publish payloads.
6. **Real LED scenes:** Replace the TODO functions with your LED code (FastLED/NeoPixel).

---

## 6) Troubleshooting checklist

### ESP32 won’t connect to MQTT
- Verify the IP/port: `192.168.88.254:1883`
- Verify username/password (try them in MQTT Explorer)
- Check serial output for `mqtt.state()` codes:
  - rc=-2: network issue / broker unreachable
  - rc=4/5: bad user/pass

### ESP32 connects, but no messages received
- Confirm you subscribed to the same topic you’re publishing.
- MQTT topics are case-sensitive.
- Publish plain strings first (avoid JSON until basic flow works).

### Pi publishes but nothing happens
- Use MQTT Explorer to watch the topic you publish to.
- Confirm the Pi is publishing to `lights/all/cmd` (or the topic your ESP32 subscribes to).

---

## 7) Next upgrade ideas (optional)

- Use Home Assistant automations:
  - Pi publishes `ir/remote/livingroom`
  - HA automation publishes `lights/all/cmd` based on time of day, etc.
- Add “state” topics so ESP32 reports current scene/brightness back to HA.
- Consider receiving IR directly on ESP32-C3 using the RMT peripheral (removes the Pi from the loop).

---

## Notes on ESP32-C3 recovery if uploads fail

If an ESP32-C3 gets into a bad state and cannot upload builds:
1. Disconnect the USB cable from the computer.
2. Hold down the BOOT button.
3. While holding BOOT, reconnect the USB cable.
4. Upload again.

---

If you tell me what IR software you’re using on the Pi (LIRC vs Python library) and what LED library you want (FastLED vs Adafruit NeoPixel), I can provide a fully “copy/paste” end-to-end example with a real IR code mapping.
