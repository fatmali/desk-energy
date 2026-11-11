#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <sys/time.h>

#include "EnergyDashboard.h"
#include "EnergyRelay.h"
#include "EnergyStorage.h"
#include "DailyFocusCounter.h"
#include "DeskState.h"
#include "PomodoroTimer.h"
#include "TimerButton.h"
#include "WebPage.h"

constexpr int TftMosiPin = 23;
constexpr int TftClockPin = 18;
constexpr int TftCsPin = 15;
constexpr int TftDcPin = 2;
constexpr int TftResetPin = 4;
constexpr int TftBacklightPin = 32;
constexpr int BootButtonPin = 0;
constexpr uint16_t BackgroundColor = ST77XX_BLACK;
constexpr int32_t NairobiUtcOffset = 3 * 60 * 60;

void focusCompleted(uint32_t completedAt);

Adafruit_ST7789 display(&SPI, TftCsPin, TftDcPin, TftResetPin);
PomodoroTimer timer(focusCompleted);
DailyFocusCounter dailyFocus;
bool dailyNeedsSave = false;
uint32_t lastDailySaveAt = 0;
TimerButton button;
JobStatus job;
WebServer server(80);
Preferences preferences;
String taskTitle;
String hostname;
String setupSsid;
String wifiSsid;
String wifiPassword;
String networkMessage = "Connecting to Wi-Fi...";
char accessCode[17] = {};
bool storageReady = false;
bool apActive = false;
bool online = false;
bool connectPending = false;
bool pairingVisible = false;
uint32_t pairShownAt = 0;
uint32_t lastConnectAt = 0;
uint32_t connectedAt = 0;
uint32_t rebootAt = 0;
bool rebootPending = false;
int32_t timezoneOffset = NairobiUtcOffset;
String requestBody;
String serialProvisioningBody;
bool serialProvisioning = false;
bool discardSerialProvisioning = false;
uint32_t lastSerialByteAt = 0;
bool bodyComplete = false;
bool updateReceiving = false;
bool updateComplete = false;
unsigned int updateFiles = 0;
String updateError;
bool displayDirty = true;
uint32_t lastDisplayCheckAt = 0;
EnergyState energyState;
EnergyRelay energyRelay;
EnergyDashboard dashboard;
constexpr int StripHeight = 8;
GFXcanvas16 strip(320, StripHeight);
bool displayAvailable = false;
uint32_t lastFrameMs = 0;
uint32_t lastDrawingMs = 0;

void printStatus();
void provisionSerialWifi(const String &body);

void focusCompleted(uint32_t completedAt) {
  const uint64_t now = esp_timer_get_time() / 1000;
  dailyFocus.completed(now - uint32_t(uint32_t(now) - completedAt));
}

void updateDailyFocus() {
  timeval clock = {};
  gettimeofday(&clock, nullptr);
  if (dailyFocus.synchronize(int64_t(clock.tv_sec) * 1000 + clock.tv_usec / 1000,
                             esp_timer_get_time() / 1000, timezoneOffset)) {
    dailyNeedsSave = true;
    displayDirty = true;
  }
  if (storageReady && dailyNeedsSave && uint32_t(millis() - lastDailySaveAt) >= 1000) {
    lastDailySaveAt = millis();
    if (preferences.putBytes("daily", &dailyFocus.saved, sizeof(dailyFocus.saved)) ==
        sizeof(dailyFocus.saved)) {
      dailyNeedsSave = false;
    } else {
      Serial.println("ERROR: Today's focus count could not be saved; retrying.");
    }
  }
}

const char *phaseLabel() {
  switch (timer.phase()) {
    case PomodoroTimer::Phase::Focus:
      return "FOCUS";
    case PomodoroTimer::Phase::ShortBreak:
      return "SHORT BREAK";
    case PomodoroTimer::Phase::LongBreak:
      return "LONG BREAK";
  }
  return "INVALID PHASE";
}

const char *stateLabel() {
  switch (timer.state()) {
    case PomodoroTimer::State::Ready:
      return "READY";
    case PomodoroTimer::State::Running:
      return "RUNNING";
    case PomodoroTimer::State::Paused:
      return "PAUSED";
  }
  return "INVALID STATE";
}

void printHelp() {
  Serial.println("Commands (single characters, uppercase also works):");
  Serial.println("  s = start/resume   p = pause   r = reset");
  Serial.println("  n = skip phase     t = status  ? = help");
  Serial.println("25 min focus / 5 min break / 15 min break after 4 focuses.");
  Serial.println("Phases advance automatically while running.");
  Serial.println("BOOT: tap to start/pause/resume; hold 1.5 seconds to reset.");
}

void printStatus() {
  const uint32_t seconds = timer.remainingSeconds();
  Serial.printf("%s | %s | %02lu:%02lu | Completed: %lu\n",
                phaseLabel(), stateLabel(),
                static_cast<unsigned long>(seconds / 60),
                static_cast<unsigned long>(seconds % 60),
                static_cast<unsigned long>(timer.completedSessions()));
  Serial.printf("Wi-Fi: saved network=%s, status=%d\n",
                wifiSsid.isEmpty() ? "no" : "yes", static_cast<int>(WiFi.status()));
  if (online) Serial.printf("Wi-Fi signal: %ld dBm\n", static_cast<long>(WiFi.RSSI()));
  Serial.printf("Display: dashboard, last frame %lu ms (drawing %lu ms)\n",
                static_cast<unsigned long>(lastFrameMs), static_cast<unsigned long>(lastDrawingMs));
  Serial.printf("Energy relay: %s\n", energyRelay.enabled() ? "enabled" : "disabled");
  Serial.printf("Energy status: %s\n", energyStatusText(energyState.status(time(nullptr), millis())));
}

bool controlTimer(char command, uint32_t now) {
  switch (command) {
    case 's':
      timer.start(now);
      break;
    case 'p':
      timer.pause(now);
      break;
    case 'r':
      timer.reset();
      break;
    case 'n':
      timer.skip(now);
      Serial.println("Phase skipped; unfinished focus sessions do not count.");
      break;
    default:
      return false;
  }
  displayDirty = true;
  printStatus();
  return true;
}

void handleButton(uint32_t now) {
  const TimerButton::Action action = button.update(digitalRead(BootButtonPin) == LOW, now);
  if (action == TimerButton::Action::Toggle) {
    controlTimer(timer.state() == PomodoroTimer::State::Running ? 'p' : 's', now);
  } else if (action == TimerButton::Action::Reset) {
    controlTimer('r', now);
  }
}

void handleCommand(uint32_t now) {
  if (serialProvisioning && !discardSerialProvisioning &&
      uint32_t(now - lastSerialByteAt) >= 5000) {
    serialProvisioningBody = "";
    discardSerialProvisioning = true;
    Serial.println("ERROR: USB provisioning timed out; send a newline before retrying.");
  }
  if (Serial.available() <= 0) {
    return;
  }
  int command = Serial.read();
  if (serialProvisioning) {
    lastSerialByteAt = now;
    if (command == '\n') {
      if (!discardSerialProvisioning) provisionSerialWifi(serialProvisioningBody);
      serialProvisioningBody = "";
      serialProvisioning = false;
      discardSerialProvisioning = false;
    } else if (command != '\r' && !discardSerialProvisioning) {
      if (serialProvisioningBody.length() >= 768) {
        serialProvisioningBody = "";
        discardSerialProvisioning = true;
        Serial.println("ERROR: USB provisioning message is too long.");
      } else {
        serialProvisioningBody += char(command);
      }
    }
    return;
  }
  if (command == '{') {
    serialProvisioning = true;
    serialProvisioningBody = "{";
    lastSerialByteAt = now;
    return;
  }
  if (command >= 'A' && command <= 'Z') {
    command += 'a' - 'A';
  }
  if (controlTimer(command, now)) {
    return;
  }
  switch (command) {
    case 't':
      printStatus();
      return;
    case '?':
      printHelp();
      return;
    case '\r':
    case '\n':
    case ' ':
    case '\t':
      return;
    default:
      Serial.println("Unknown command. Send ? for help.");
      return;
  }
}

void sendJson(int code, const JsonDocument &document) {
  String body;
  serializeJson(document, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const char *message) {
  Serial.printf("HTTP %d: %s\n", code, message);
  JsonDocument document;
  document["error"] = message;
  sendJson(code, document);
}

bool authorized() {
  if (server.authenticate("focus", accessCode)) {
    return true;
  }
  server.requestAuthentication(BASIC_AUTH, "Desk focus", "Use focus and the access code on the screen.");
  return false;
}

bool authorizedWrite() {
  if (!authorized()) {
    return false;
  }
  // A cross-origin HTML form cannot supply this header; no CORS is enabled.
  if (server.header("X-Desk-Focus") != "1") {
    sendError(403, "Missing X-Desk-Focus header.");
    return false;
  }
  const String origin = server.header("Origin");
  if (origin.length() && origin != "http://" + server.hostHeader()) {
    sendError(403, "Cross-origin requests are not allowed.");
    return false;
  }
  return true;
}

void receiveJson() {
  if (!server.hasRaw()) {
    bodyComplete = false;
    return;
  }
  HTTPRaw &raw = server.raw();
  if (raw.status == RAW_START) {
    requestBody = "";
    bodyComplete = false;
    if (!authorizedWrite()) {
      server.client().stop();
      return;
    }
    if (!server.header("Content-Type").startsWith("application/json") ||
        server.clientContentLength() <= 0 || server.clientContentLength() > 768) {
      sendError(400, "Send a JSON body between 1 and 768 bytes.");
      server.client().stop();
    }
  } else if (raw.status == RAW_WRITE) {
    if (requestBody.length() + raw.currentSize > 768) {
      sendError(413, "Request body is too large.");
      server.client().stop();
      return;
    }
    requestBody.concat(reinterpret_cast<const char *>(raw.buf), raw.currentSize);
  } else if (raw.status == RAW_END) {
    bodyComplete = true;
  } else if (raw.status == RAW_ABORTED) {
    bodyComplete = false;
    Serial.println("HTTP request body was interrupted.");
  }
}

bool readJson(JsonDocument &document) {
  if (!authorizedWrite()) {
    return false;
  }
  if (!bodyComplete ||
      deserializeJson(document, requestBody, DeserializationOption::NestingLimit(2)) ||
      !document.is<JsonObject>()) {
    sendError(400, "Invalid JSON object.");
    return false;
  }
  return true;
}

bool readText(JsonVariantConst value, String &output) {
  if (!value.is<const char *>()) {
    return false;
  }
  JsonString text = value.as<JsonString>();
  if (text.size() != strlen(text.c_str())) {
    return false;
  }
  output = text.c_str();
  return true;
}

int saveWifiCredentials(const JsonDocument &document, String &error) {
  String ssid, password;
  if (!readText(document["ssid"], ssid) || ssid.isEmpty() || ssid.length() > 32 ||
      !readText(document["password"], password) ||
      (!password.isEmpty() && (password.length() < 8 || password.length() > 63))) {
    error = "Use a 1-32 byte SSID and an 8-63 byte password (or blank for open Wi-Fi).";
    return 400;
  }
  JsonDocument settings;
  settings["ssid"] = ssid;
  settings["password"] = password;
  String saved;
  serializeJson(settings, saved);
  if (!storageReady || preferences.putString("wifi", saved) != saved.length()) {
    error = "Could not save Wi-Fi credentials.";
    return 500;
  }
  wifiSsid = ssid;
  wifiPassword = password;
  connectPending = true;
  lastConnectAt = millis();
  networkMessage = "Trying the new Wi-Fi network. Reconnect using the device hostname.";
  return 0;
}

void provisionSerialWifi(const String &body) {
  JsonDocument document;
  if (deserializeJson(document, body, DeserializationOption::NestingLimit(2)) ||
      !document.is<JsonObject>()) {
    Serial.println("ERROR: Invalid USB provisioning JSON.");
    return;
  }
  if (!document["relay"].isUnbound()) {
    if (document.size() != 1) {
      Serial.println("ERROR: Relay provisioning cannot include other settings.");
    } else if (document["relay"].isNull()) {
      if (energyRelay.disable()) Serial.println("Energy relay disabled.");
    } else {
      energyRelay.configure(document["relay"].as<JsonVariantConst>());
    }
    return;
  }
  if (!document["reuse_password"].isUnbound()) {
    if (!document["reuse_password"].is<bool>() || !document["reuse_password"].as<bool>() ||
        !document["password"].isUnbound() || wifiSsid.isEmpty()) {
      Serial.println("ERROR: Password reuse requires reuse_password:true, no password field, and a saved network.");
      return;
    }
    document["password"] = wifiPassword;
  }
  String error;
  if (saveWifiCredentials(document, error) != 0) {
    Serial.printf("ERROR: %s\n", error.c_str());
    return;
  }
  Serial.println("Wi-Fi credentials saved; connecting.");
}

void sendStatus() {
  JsonDocument document;
  document["state"] = timer.state() == PomodoroTimer::State::Ready ? "ready" :
      timer.state() == PomodoroTimer::State::Running ? "running" : "paused";
  document["phase"] = timer.phase() == PomodoroTimer::Phase::Focus ? "focus" :
      timer.phase() == PomodoroTimer::Phase::ShortBreak ? "short_break" : "long_break";
  document["remaining"] = timer.remainingSeconds();
  document["duration"] = timer.durationMs() / 1000;
  document["completed"] = timer.completedSessions();
  if (dailyFocus.timeKnown) {
    document["completed_today"] = dailyFocus.saved.count;
  } else {
    document["completed_today"] = nullptr;
  }
  document["task"] = taskTitle;
  document["connected"] = online;
  document["hostname"] = hostname;
  document["ip"] = online ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  document["network_message"] = networkMessage;
  document["job"]["state"] = job.state;
  document["job"]["kind"] = job.kind;
  document["job"]["label"] = job.label;
  sendJson(200, document);
}

void startAccessPoint() {
  if (apActive) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(setupSsid.c_str(), accessCode)) {
    networkMessage = "Setup Wi-Fi failed. Restart the board.";
    Serial.println("ERROR: Could not start setup access point.");
  } else {
    apActive = true;
    networkMessage = wifiSsid.isEmpty() ? "Join the setup network to configure Wi-Fi." :
        "Wi-Fi unavailable. Check the network name/password in setup.";
    Serial.printf("Setup network available: %s (code is on the TFT).\n", setupSsid.c_str());
  }
  displayDirty = true;
}

void receiveFirmware() {
  if (!server.hasUpload()) {
    updateError = "Use multipart/form-data with one firmware file.";
    if (server.hasRaw() && server.raw().status == RAW_START) {
      sendError(400, updateError.c_str());
      server.client().stop();
    }
    return;
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (++updateFiles > 1) {
      updateError = "Only one firmware file is allowed.";
      Update.abort();
      return;
    }
    updateError = "";
    updateComplete = false;
    if (!authorizedWrite()) {
      server.client().stop();
      return;
    }
    if (timer.state() != PomodoroTimer::State::Ready) {
      updateError = "Reset the timer to Ready before updating.";
    } else if (!upload.filename.endsWith(".bin")) {
      updateError = "Select this project's firmware.bin.";
    } else if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      updateError = Update.errorString();
    } else {
      updateReceiving = true;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE && updateReceiving && updateError.isEmpty()) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      updateError = Update.errorString();
      Update.abort();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    updateComplete = updateReceiving && updateError.isEmpty() && upload.totalSize > 0;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    updateReceiving = false;
    updateComplete = false;
    updateFiles = 0;
    Serial.println("ERROR: Firmware upload interrupted; keeping current firmware.");
  }
}

void configureRoutes() {
  const char *headers[] = {"X-Desk-Focus", "Origin", "Content-Type"};
  server.collectHeaders(headers, 3);
  const EnergyAcceptError restored = EnergyStorage::restore(preferences, energyState, time(nullptr), millis());
  if (restored != EnergyAcceptError::None) {
    Serial.printf("ERROR: Saved energy could not be restored: %s\n", energyErrorText(restored));
  } else if (energyState.reading().computedAt) {
    Serial.println("Saved energy restored; original timestamps retained.");
  }
  energyRelay.begin(preferences, energyState);
  server.on("/", HTTP_GET, []() {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("Referrer-Policy", "no-referrer");
    server.sendHeader("Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
        "connect-src 'self'; frame-ancestors 'none'; form-action 'self'; base-uri 'none'");
    server.send_P(200, "text/html", DeskWebPage);
  });
  server.on("/api/status", HTTP_GET, []() {
    if (authorized()) sendStatus();
  });
  server.on("/api/control", HTTP_POST, []() {
    JsonDocument document;
    if (!readJson(document)) return;
    String action;
    if (!readText(document["action"], action)) {
      sendError(400, "An action is required.");
      return;
    }
    const char command = action == "start" ? 's' : action == "pause" ? 'p' :
        action == "reset" ? 'r' : action == "skip" ? 'n' : 0;
    if (!command) {
      sendError(400, "Unknown timer action.");
      return;
    }
    controlTimer(command, millis());
    sendStatus();
  }, receiveJson);
  server.on("/api/task", HTTP_POST, []() {
    JsonDocument document;
    String title;
    if (!readJson(document)) return;
    if (!readText(document["task"], title) || !validDisplayText(title.c_str(), 48)) {
      sendError(400, "Task must be at most 48 printable ASCII characters.");
      return;
    }
    String saved;
    serializeJson(document, saved);
    if (preferences.putString("task", saved) != saved.length()) {
      sendError(500, "Could not save the task.");
      return;
    }
    taskTitle = title;
    displayDirty = true;
    sendStatus();
  }, receiveJson);
  server.on("/api/job", HTTP_POST, []() {
    JsonDocument document;
    String id, label, kind, state;
    if (!readJson(document)) return;
    if (!readText(document["id"], id) || !readText(document["label"], label) ||
        !readText(document["kind"], kind) || !readText(document["state"], state) ||
        !job.report(id.c_str(), label.c_str(), kind.c_str(), state.c_str(), millis())) {
      sendError(409, "Invalid job report, or a newer run has replaced this run.");
      return;
    }
    displayDirty = true;
    sendStatus();
  }, receiveJson);
  server.on("/api/clock", HTTP_POST, []() {
    JsonDocument document;
    if (!readJson(document)) return;
    if (!document["epoch"].is<int64_t>() || !document["offset"].is<int32_t>()) {
      sendError(400, "Clock epoch and timezone offset must be integers.");
      return;
    }
    const int64_t epoch = document["epoch"];
    const int32_t offset = document["offset"];
    if (epoch < 1704067200 || epoch > 4102444800LL || offset < -43200 || offset > 50400) {
      sendError(400, "Clock value is out of range.");
      return;
    }
    if (offset != timezoneOffset && preferences.putInt("offset", offset) != sizeof(int32_t)) {
      sendError(500, "Could not save the timezone offset.");
      return;
    }
    timezoneOffset = offset;
    timeval timeValue = {};
    timeValue.tv_sec = epoch;
    if (settimeofday(&timeValue, nullptr) != 0) {
      sendError(500, "Could not set the clock.");
      return;
    }
    displayDirty = true;
    sendStatus();
  }, receiveJson);
  server.on("/api/pair", HTTP_POST, []() {
    JsonDocument document;
    if (!readJson(document)) return;
    pairShownAt = millis();
    pairingVisible = true;
    displayDirty = true;
    sendStatus();
  }, receiveJson);
  server.on("/api/wifi", HTTP_POST, []() {
    JsonDocument document;
    if (!readJson(document)) return;
    String error;
    const int errorCode = saveWifiCredentials(document, error);
    if (errorCode != 0) {
      sendError(errorCode, error.c_str());
      return;
    }
    sendStatus();
  }, receiveJson);
  server.on("/api/update", HTTP_POST, []() {
    if (!authorizedWrite()) return;
    if (updateError.isEmpty() && (!updateReceiving || !updateComplete)) {
      updateError = "No complete firmware file received.";
    }
    if (updateError.isEmpty() && !Update.end(true)) {
      updateError = Update.errorString();
    }
    updateReceiving = false;
    updateComplete = false;
    updateFiles = 0;
    if (!updateError.isEmpty()) {
      Update.abort();
      sendError(400, updateError.c_str());
      return;
    }
    JsonDocument document;
    document["message"] = "Firmware accepted. Rebooting.";
    sendJson(200, document);
    rebootAt = millis();
    rebootPending = true;
  }, receiveFirmware);
  server.onNotFound([]() { sendError(404, "Not found."); });
  server.begin();
}

void initializeNetwork() {
  storageReady = preferences.begin("deskfocus", false);
  if (!storageReady) {
    networkMessage = "Storage error. Wi-Fi disabled.";
    Serial.println("ERROR: Could not open device settings.");
    return;
  }
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("Wi-Fi disconnect reason: %u; reported RSSI: %d dBm\n",
                  static_cast<unsigned int>(info.wifi_sta_disconnected.reason),
                  static_cast<int>(info.wifi_sta_disconnected.rssi));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  String savedCode = preferences.getString("code");
  if (savedCode.isEmpty()) {
    snprintf(accessCode, sizeof(accessCode), "%08lx%08lx",
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
    if (preferences.putString("code", accessCode) != 16) {
      storageReady = false;
      networkMessage = "Storage error. Wi-Fi disabled.";
      Serial.println("ERROR: Could not persist device access code.");
      return;
    }
    pairingVisible = true;
  } else {
    if (savedCode.length() != 16) {
      storageReady = false;
      networkMessage = "Invalid access code storage.";
      Serial.println("ERROR: Stored access code has an invalid length.");
      return;
    }
    savedCode.toCharArray(accessCode, sizeof(accessCode));
  }
  hostname = "deskfocus-" + String(static_cast<uint32_t>(ESP.getEfuseMac()) & 0xFFFFFF, HEX);
  setupSsid = "DeskFocus-" + hostname.substring(10);
  timezoneOffset = preferences.getInt("offset", NairobiUtcOffset);
  const size_t dailySize = preferences.getBytesLength("daily");
  if (dailySize == sizeof(dailyFocus.saved)) {
    if (preferences.getBytes("daily", &dailyFocus.saved, sizeof(dailyFocus.saved)) != dailySize) {
      Serial.println("ERROR: Could not restore today's focus count.");
      dailyFocus.saved = {};
    }
  } else if (dailySize != 0) {
    Serial.println("ERROR: Saved daily focus count has an invalid size.");
  }
  JsonDocument document;
  String saved = preferences.getString("wifi");
  if (!saved.isEmpty()) {
    if (deserializeJson(document, saved) ||
        !readText(document["ssid"], wifiSsid) || !readText(document["password"], wifiPassword)) {
      Serial.println("ERROR: Saved Wi-Fi settings are invalid. Use setup again.");
      wifiSsid = "";
    }
  }
  saved = preferences.getString("task");
  if (!saved.isEmpty() && (deserializeJson(document, saved) || !readText(document["task"], taskTitle))) {
    Serial.println("ERROR: Saved task is invalid.");
  }
  WiFi.setHostname(hostname.c_str());
  WiFi.setAutoReconnect(true);
  if (wifiSsid.isEmpty()) {
    startAccessPoint();
  } else {
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  }
  lastConnectAt = millis();
  pairShownAt = millis();
  configureRoutes();
}

void updateNetwork(uint32_t now) {
  if (!storageReady) return;
  if (connectPending && uint32_t(now - lastConnectAt) >= 500) {
    startAccessPoint();
    WiFi.disconnect();
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    connectPending = false;
    lastConnectAt = now;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != online) {
    online = connected;
    displayDirty = true;
    if (online) {
      connectedAt = now;
      networkMessage = "Connected. Open http://" + hostname + ".local/ on your usual Wi-Fi.";
      if (!MDNS.begin(hostname.c_str())) {
        Serial.println("ERROR: mDNS unavailable; use the IP address in Serial status.");
      } else {
        MDNS.addService("http", "tcp", 80);
      }
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      Serial.printf("Wi-Fi connected: http://%s/ or http://%s.local/\n",
                    WiFi.localIP().toString().c_str(), hostname.c_str());
    } else {
      MDNS.end();
      networkMessage = "Wi-Fi disconnected. Timer continues offline.";
      lastConnectAt = now;
      Serial.println("Wi-Fi disconnected; timer continues offline.");
    }
  }
  if (!online && !connectPending && uint32_t(now - lastConnectAt) >= 30000) {
    startAccessPoint();
    if (!wifiSsid.isEmpty()) WiFi.reconnect();
    lastConnectAt = now;
  }
  if (online && apActive && !connectPending && uint32_t(now - connectedAt) >= 60000) {
    WiFi.softAPdisconnect(true);
    apActive = false;
    displayDirty = true;
  }
  if (pairingVisible && uint32_t(now - pairShownAt) >= 120000) {
    pairingVisible = false;
    displayDirty = true;
  }
  if (energyRelay.tick()) displayDirty = true;
  server.handleClient();
  if (rebootPending && uint32_t(millis() - rebootAt) >= 750) ESP.restart();
}

void initializeDisplay() {
  pinMode(TftBacklightPin, OUTPUT);
  digitalWrite(TftBacklightPin, LOW);
  SPI.begin(TftClockPin, -1, TftMosiPin, TftCsPin);
  display.init(170, 320);
  display.setSPISpeed(40000000);
  display.setRotation(1);
  display.setTextWrap(false);
  displayAvailable = strip.getBuffer() != nullptr;
  if (!displayAvailable) {
    Serial.println("ERROR: Display strip allocation failed.");
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(12, 72);
    display.print("Display memory error");
  }
  strip.setTextWrap(false);
  Serial.println("ST7789 configured: 320x170 landscape, SPI MOSI23/SCLK18, "
                 "CS15/DC2/RST4/BL32.");
  Serial.println("Display connection has no readback; visually confirm the screen.");
}

void renderDisplay() {
  if (!displayAvailable) return;
  const uint32_t now = millis();
  if (!displayDirty && uint32_t(now - lastDisplayCheckAt) < 125) return;
  const uint32_t seconds = timer.remainingSeconds();
  if (pairingVisible) {
    if (displayDirty) {
      display.fillScreen(BackgroundColor);
      display.setTextColor(ST77XX_WHITE, BackgroundColor);
      display.setTextSize(2);
      display.setCursor(12, 12); display.print("Device access code");
      display.setCursor(12, 50); display.print(accessCode);
      display.setTextSize(1);
      display.setCursor(12, 92); display.print("Browser login: focus");
      display.setCursor(12, 110); display.print(setupSsid);
      display.setCursor(12, 126); display.print(online ? WiFi.localIP().toString() : "http://192.168.4.1");
      display.setCursor(12, 146); display.print("Dashboard returns automatically.");
    }
    lastDisplayCheckAt = now;
    displayDirty = false;
    return;
  }
  const PomodoroTimer::State frameState = timer.state();
  const PomodoroTimer::Phase framePhase = timer.phase();
  const bool paused = frameState == PomodoroTimer::State::Paused;
  const time_t clock = time(nullptr);
  EnergyDashboard::Frame frame = {};
  frame.status = energyState.status(clock, now);
  frame.score = energyState.reading().score;
  frame.timer = frameState == PomodoroTimer::State::Ready ? EnergyDashboard::Timer::Hidden :
      paused ? EnergyDashboard::Timer::Paused : EnergyDashboard::Timer::Running;
  frame.timerLabel = paused ? "PAUSED" : framePhase == PomodoroTimer::Phase::Focus ? "FOCUS" : "BREAK";
  frame.timerSeconds = seconds;
  // The top-right clock stays blank until NTP or /api/clock has set the time.
  frame.clockMinutes = clock >= 1704067200 ?
      int((int64_t(clock) + timezoneOffset) % 86400 / 60) : -1;
  const bool changed = dashboard.prepare(frame, now);
  lastDisplayCheckAt = now;
  if (!displayDirty && !changed) return;
  const uint32_t frameStart = millis();
  uint32_t drawingUs = 0;
  for (int y = 0; y < EnergyDashboard::Height; y += StripHeight) {
    const int rows = EnergyDashboard::Height - y < StripHeight ? EnergyDashboard::Height - y : StripHeight;
    const uint32_t drawStart = micros();
    dashboard.render(strip, y, rows);
    drawingUs += micros() - drawStart;
    display.drawRGBBitmap(0, y, strip.getBuffer(), 320, rows);
    handleButton(millis());
    yield();
  }
  lastFrameMs = millis() - frameStart;
  lastDrawingMs = drawingUs / 1000;
  displayDirty = timer.state() != frameState || timer.phase() != framePhase;
}

void setup() {
  Serial.begin(115200);
  pinMode(BootButtonPin, INPUT_PULLUP);
  Serial.println("\nDesk Focus Timer - Energy Dashboard");
  initializeDisplay();
  initializeNetwork();
  updateDailyFocus();
  printHelp();
  printStatus();
  renderDisplay();
  digitalWrite(TftBacklightPin, HIGH);
}

void loop() {
  const uint32_t now = millis();
  if (timer.update(now)) {
    displayDirty = true;
    Serial.println("Session complete. Next phase started.");
    printStatus();
  }
  handleCommand(now);
  handleButton(now);
  updateNetwork(now);
  job.expire(millis());
  updateDailyFocus();
  renderDisplay();
  delay(5);
}
