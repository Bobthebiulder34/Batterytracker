#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <NetworkTables.h>

// NetworkTables setup
NetworkTables nt;
const char* ntServer = "10.TE.AM.2";  // Replace TE.AM with your team number (e.g., "10.12.34.2" for team 1234)

// NFC Reader Setup (I2C)
#define PN532_IRQ   (2)
#define PN532_RESET (3)
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// WiFi and Web Server
const char* ssid = "FRC_ROBOT";  // Your robot's WiFi network
const char* password = "your_password";
WebServer server(80);

// Storage
Preferences preferences;

// Battery data structure
struct Battery {
  String uid;           // NFC tag UID
  String name;          // Battery label (e.g., "Battery 1")
  int usageCount;       // Number of times used
  unsigned long totalTime; // Total time in robot (seconds)
  unsigned long installTime; // When currently installed (0 if not installed)
};

Battery batteries[20];  // Support up to 20 batteries
int numBatteries = 0;
String currentBatteryUID = "";

// Mode management
enum Mode { NORMAL, SETUP };
Mode currentMode = NORMAL;

// Button pin for mode switching
#define MODE_BUTTON 4

void setup() 
{
  Serial.begin(115200);
  pinMode(MODE_BUTTON, INPUT_PULLUP);
  // Initialize NFC reader 
  nfc.begin();
  // Check NFC reader
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("NFC reader not found!");
    while (1);
  }
  nfc.SAMConfig();
  // Announce NFC reader ready
  Serial.println("NFC Reader Ready");
  // Load saved data
  loadBatteryData();
  // Setup WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
  // Setup web server routes
  setupWebServer(); // Define web server routes
  server.begin();
  // Connect to NetworkTables
  nt.connect(ntServer, 1735);  // 1735 is standard NT port
  Serial.println("NetworkTables connected to RoboRIO");
  Serial.println("Battery Management System Ready");
  Serial.println("Press button for 3 seconds to enter SETUP mode");
}
void loop() {
  server.handleClient();
  nt.update();  // Keep NetworkTables connection alive
  checkModeButton();
  
  if (currentMode == SETUP) 
  {
    handleSetupMode();
  } 
  else 
  {
    handleNormalMode();
  }
  delay(2000);  // Scan every 2 seconds
}

void checkModeButton() 
{
  static unsigned long pressStart = 0;
  static bool wasPressed = false;
  
  bool isPressed = (digitalRead(MODE_BUTTON) == LOW);
  
  if (isPressed && !wasPressed) 
  {
    pressStart = millis();
    wasPressed = true;
  }
  // Check for long press/debounce
  if (isPressed && wasPressed && (millis() - pressStart > 3000)) 
  {
    currentMode = (currentMode == NORMAL) ? SETUP : NORMAL;
    Serial.println(currentMode == SETUP ? "\n=== SETUP MODE ===" : "\n=== NORMAL MODE ===");
    wasPressed = false;
  }
  
  if (!isPressed) 
  {
    wasPressed = false;
  }
}

void handleSetupMode() 
{
  static bool setupStarted = false;
  static int batteryIndex = 0;
  // Prompt for number of batteries
  if (!setupStarted) 
  {
    Serial.println("How many batteries do you want to register?");
    Serial.println("Enter number (1-20) in Serial Monitor:");
    setupStarted = true;
    batteryIndex = 0;
  }
  
  // Check for serial input for number of batteries
  if (Serial.available() > 0 && batteryIndex == 0) 
  {
    String input = Serial.readStringUntil('\n');
    int num = input.toInt();
    if (num > 0 && num <= 20) 
    {
      numBatteries = num;
      Serial.println("Registering " + String(numBatteries) + " batteries");
      Serial.println("Scan battery 1 now...");
      batteryIndex = 1;
    }
  }
  
  // Scan and register batteries
  if (batteryIndex > 0 && batteryIndex <= numBatteries) 
  {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) 
    {
      String uidString = getUIDString(uid, uidLength);
      
      // Check if already registered
      bool alreadyExists = false;
      for (int i = 0; i < batteryIndex - 1; i++) 
      {
        if (batteries[i].uid == uidString) 
        {
          Serial.println("ERROR: This tag is already registered!");
          alreadyExists = true;
          break;
        }
      }
      
      if (!alreadyExists) 
      {
        batteries[batteryIndex - 1].uid = uidString;
        batteries[batteryIndex - 1].name = "Battery " + String(batteryIndex);
        batteries[batteryIndex - 1].usageCount = 0;
        batteries[batteryIndex - 1].totalTime = 0;
        batteries[batteryIndex - 1].installTime = 0;
        // Confirm registration
        Serial.println("Battery " + String(batteryIndex) + " registered: " + uidString);
        batteryIndex++;
        // Prompt for next battery or finish
        if (batteryIndex <= numBatteries) 
        {
          Serial.println("Scan battery " + String(batteryIndex) + " now...");
        } else 
        {
          Serial.println("\n=== Setup Complete! ===");
          saveBatteryData();
          setupStarted = false;
          currentMode = NORMAL;
          Serial.println("Switched to NORMAL mode");
        }
      }
      
      delay(2000); // Prevent multiple reads
    }
  }
}

void handleNormalMode() 
{
  static unsigned long lastUpdate = 0;
  static unsigned long lastNTUpdate = 0;
  
  // Update usage time for currently installed battery
  if (currentBatteryUID != "" && millis() - lastUpdate > 1000) 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID && batteries[i].installTime > 0) 
      {
        batteries[i].totalTime++;
        lastUpdate = millis();
        
        // Save every 60 seconds
        if (batteries[i].totalTime % 60 == 0) 
        {
          saveBatteryData();
        }
        break;
      }
    }
  }
  
  // Update NetworkTables every 5 seconds
  if (millis() - lastNTUpdate > 5000) 
  {
    publishToNetworkTables();
    lastNTUpdate = millis();
  }
  
  // Check for battery scan
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  // Scan for NFC tag
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) 
  {
    String uidString = getUIDString(uid, uidLength);
    
    for (int i = 0; i < numBatteries; i++) 
    {
        // Check if known battery
      if (batteries[i].uid == uidString) 
      {
        // Known battery scanned
        Serial.println("Battery scanned: " + uidString);
        installBattery(i);
        delay(2000); // Prevent multiple reads
        return;
      }
    }
    // Unknown battery
    Serial.println("Unknown battery scanned: " + uidString);
    Serial.println("Please register this battery in SETUP mode");
    delay(2000);
  }
}

void installBattery(int index)
{
  // Remove previous battery if any
  if (currentBatteryUID != "") 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID) 
      {
        batteries[i].installTime = 0;
        break;
      }
    }
  }
  
  // Install new battery
  batteries[index].usageCount++;
  batteries[index].installTime = millis() / 1000;
  currentBatteryUID = batteries[index].uid;
  
  Serial.println("\n=== BATTERY INSTALLED ===");
  Serial.println("Name: " + batteries[index].name);
  Serial.println("Usage Count: " + String(batteries[index].usageCount));
  Serial.println("Total Time: " + formatTime(batteries[index].totalTime));
  Serial.println("========================\n");
  
  saveBatteryData();
  publishToNetworkTables();  // Immediately update NT when battery changes
}

String getUIDString(uint8_t* uid, uint8_t uidLength) 
{
  String uidString = "";
  for (uint8_t i = 0; i < uidLength; i++) 
  {
    if (uid[i] < 0x10) uidString += "0";
    uidString += String(uid[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

String formatTime(unsigned long seconds) 
{
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  return String(hours) + "h " + String(minutes) + "m " + String(secs) + "s";
}

void loadBatteryData() 
{
  preferences.begin("batteries", false);
  numBatteries = preferences.getInt("count", 0);
  
  for (int i = 0; i < numBatteries; i++) 
  {
    String prefix = "bat" + String(i) + "_";
    batteries[i].uid = preferences.getString((prefix + "uid").c_str(), "");
    batteries[i].name = preferences.getString((prefix + "name").c_str(), "");
    batteries[i].usageCount = preferences.getInt((prefix + "usage").c_str(), 0);
    batteries[i].totalTime = preferences.getULong((prefix + "time").c_str(), 0);
    batteries[i].installTime = 0; // Always start with no battery installed
  }
  
  preferences.end();
  Serial.println("Loaded " + String(numBatteries) + " batteries from storage");
}

void saveBatteryData() 
{
  preferences.begin("batteries", false);
  preferences.putInt("count", numBatteries);
  
  for (int i = 0; i < numBatteries; i++) 
  {
    // Save each battery's data
    String prefix = "bat" + String(i) + "_";
    preferences.putString((prefix + "uid").c_str(), batteries[i].uid);
    preferences.putString((prefix + "name").c_str(), batteries[i].name);
    preferences.putInt((prefix + "usage").c_str(), batteries[i].usageCount);
    preferences.putULong((prefix + "time").c_str(), batteries[i].totalTime);
  }
  // Announce save
  preferences.end();
}

void publishToNetworkTables() 
{
  // Publish current battery info
  // Find current battery
  if (currentBatteryUID != "") 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
        // Match found
      if (batteries[i].uid == currentBatteryUID) 
      {// Publish details
        nt.putString("/BatteryManager/CurrentBattery/Name", batteries[i].name);
        nt.putNumber("/BatteryManager/CurrentBattery/UsageCount", batteries[i].usageCount);
        nt.putNumber("/BatteryManager/CurrentBattery/TotalTimeSeconds", batteries[i].totalTime);
        nt.putString("/BatteryManager/CurrentBattery/TotalTimeFormatted", formatTime(batteries[i].totalTime));
        
        // Calculate time since installation (current session time)
        unsigned long sessionTime = (millis() / 1000) - batteries[i].installTime;
        nt.putNumber("/BatteryManager/CurrentBattery/SessionTimeSeconds", sessionTime);
        nt.putString("/BatteryManager/CurrentBattery/SessionTimeFormatted", formatTime(sessionTime));
        nt.putBoolean("/BatteryManager/BatteryInstalled", true);
        break;
      }
    }    
} 
    else 
  {
    nt.putString("/BatteryManager/CurrentBattery/Name", "No Battery");
    nt.putBoolean("/BatteryManager/BatteryInstalled", false);
  }
  
  // Publish summary stats
  nt.putNumber("/BatteryManager/TotalBatteries", numBatteries);
  
  // Find most-used battery
  int maxUses = 0;
  String mostUsedName = "None";
  for (int i = 0; i < numBatteries; i++) 
  {
    if (batteries[i].usageCount > maxUses)
    {
      maxUses = batteries[i].usageCount;
      mostUsedName = batteries[i].name;
    }
  }// Publish most-used battery
  nt.putString("/BatteryManager/MostUsedBattery", mostUsedName);
  nt.putNumber("/BatteryManager/MostUsedCount", maxUses);
}

void setupWebServer() 
{
    // Root page to display battery info
  server.on("/", HTTP_GET, []() {
    String html = "<html><head><title>FRC Battery Manager</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:20px;} table{border-collapse:collapse;width:100%;} th,td{border:1px solid #ddd;padding:8px;text-align:left;} th{background-color:#4CAF50;color:white;} .current{background-color:#ffffcc;}</style>";
    html += "</head><body>";
    html += "<h1>FRC Battery Management</h1>";
    html += "<p>Mode: " + String(currentMode == SETUP ? "SETUP" : "NORMAL") + "</p>";
    html += "<table><tr><th>Name</th><th>UID</th><th>Uses</th><th>Total Time</th><th>Status</th></tr>";
    // Populate table with battery data
    for (int i = 0; i < numBatteries; i++) 
    {
      String rowClass = (batteries[i].uid == currentBatteryUID) ? " class='current'" : "";
      html += "<tr" + rowClass + ">";
      html += "<td>" + batteries[i].name + "</td>";
      html += "<td>" + batteries[i].uid + "</td>";
      html += "<td>" + String(batteries[i].usageCount) + "</td>";
      html += "<td>" + formatTime(batteries[i].totalTime) + "</td>";
      html += "<td>" + String(batteries[i].uid == currentBatteryUID ? "INSTALLED" : "Available") + "</td>";
      html += "</tr>";
    }
    
    html += "</table></body></html>";
    server.send(200, "text/html", html);
  });
  // API endpoint to get battery data in JSON
  server.on("/api/batteries", HTTP_GET, []() {
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    
    for (int i = 0; i < numBatteries; i++) 
    {
      JsonObject obj = array.createNestedObject();
      obj["name"] = batteries[i].name;
      obj["uid"] = batteries[i].uid;
      obj["usageCount"] = batteries[i].usageCount;
      obj["totalTime"] = batteries[i].totalTime;
      obj["installed"] = (batteries[i].uid == currentBatteryUID);
    }
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
}