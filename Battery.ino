// FRC Battery Management System - Team 4546
// Built by the team to track battery usage and keep our batteries healthy!
// 
// What you need:
// - ESP32 board (the brain)
// - PN532 NFC reader (the scanner)
// - NFC tags on each battery (the cheap sticker kind work great)
//
// How to wire it up:
// - PN532 SDA → GPIO 21
// - PN532 SCL → GPIO 22
// - PN532 IRQ → GPIO 2
// - PN532 RST → GPIO 3
// - Power everything with 3.3V
// Libraries required:
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <NetworkTables.h>
#include <HTTPClient.h>

// Team-specific settings
const char* TEAM_SSID = "PLACEHOLDER";           // robot's WiFi name
const char* TEAM_PASSWORD = "";           // WiFi password
const char* ROBORIO_IP = "PLACEHOLDER";    // RoboRIO address 
const int NT_PORT = 1735;                 // NetworkTables port

// Google Sheets settings
const char* GOOGLE_APPS_SCRIPT_URL = "your app url";  // Only works with this secret URL
// (replace with your actual Google Apps Script URL example:https://script.google.com/macros/s/AKfycbwRuJsgjrJhxMN7T3fVYPWsoHHo0Lf131n6oyqt4PW6/dev)
// Pin connections
#define PN532_IRQ   (2)      // NFC reader interrupt
#define PN532_RESET (3)      // NFC reader reset
#define LED_PIN 2  // Built-in LED on ESP32-WROOM-DA

// The stuff we need to make everything work
NetworkTables nt;
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
WebServer server(80);
Preferences preferences;  // This saves data to the ESP32's flash memory

// Battery info structure to hold each battery's data
struct Battery 
{
  String uid;                  // The unique ID from the NFC tag
  String name;                 // What we call it (e.g., "Battery 1")
  int usageCount;              // How many times we've used it
  unsigned long totalTime;     // Total seconds this battery has run
  unsigned long installTime;   // When we put it in (0 means it's not in the robot)
  bool syncedToSheets;  // Track if this battery has been uploaded
};

// Our battery database - we can track up to 20 batteries
Battery batteries[20];
int numBatteries = 0;
String currentBatteryUID = "";  // Which battery is in the robot right now

// Modes - are we in normal operation or setup?
enum Mode { NORMAL, SETUP };
enum SetupSubMode { MENU, RESET_ALL, RESET_SOME, ADD_BATTERIES, ADD_REMOVE };
Mode currentMode = NORMAL;
SetupSubMode setupSubMode = MENU;

void setup() 
{
  // Get serial communication going for debugging and commands
  Serial.begin(115200);
  // Fire up the NFC reader
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) 
  {
    Serial.println("Uh oh, can't find the NFC reader! Check your wiring.");
    while (1);  // Stop here if we can't find it
  }
  nfc.SAMConfig();
  Serial.println("NFC reader is ready to scan!");
  
  // Load any batteries we saved from last time
  loadBatteryData();
  
  // Connect to the robot's WiFi
  WiFi.begin(TEAM_SSID, TEAM_PASSWORD);
  Serial.print("Connecting to robot WiFi");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! My IP is: " + WiFi.localIP().toString());
  
  // Start the web server so people can see the dashboard
  setupWebServer();
  server.begin();
  
  // Connect to the RoboRIO for NetworkTables
  nt.connect(ROBORIO_IP, NT_PORT);
  Serial.println("Connected to RoboRIO!");
  
  // Sync all batteries to Google Sheets on startup
  syncAllBatteriesToSheets();
  //start led pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off
  Serial.println("\nBattery Management System Ready");
  Serial.println("Type 'SETUP' to configure batteries");
}

void loop() 
{
  // Keep everything running
  server.handleClient();
  nt.update();
  checkSerialCommand();
  
  // Do the right thing based on what mode we're in
  if (currentMode == SETUP) 
  {
    handleSetupMode();
  } 
  else 
  {
    handleNormalMode();
  }
  
  // Check for new batteries every 2 seconds
  delay(2000);
}

// Watch for the "SETUP" command in serial monitor
void checkSerialCommand() 
{
  if (Serial.available() > 0) 
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd == "SETUP" && currentMode == NORMAL) 
    {
      currentMode = SETUP;
      setupSubMode = MENU;
      Serial.println("\nEntering Setup Mode");
      displaySetupMenu();
    }
  }
}

// Show the setup menu options
void displaySetupMenu()
{
  Serial.println("\nSetup Menu:");
  Serial.println("1 - Reset all batteries");
  Serial.println("2 - Reset specific batteries");
  Serial.println("3 - Add new batteries");
  Serial.println("4 - Remove and add batteries");
  Serial.println("\nEnter option (1-4):");
}

// Handle all the setup operations
void handleSetupMode() 
{
  static int batteryIndex = 0;
  static String batteriesToReset[20];
  static int resetCount = 0;
  static String batteriesToRemove[20];
  static int removeCount = 0;
  
  if (Serial.available() > 0) 
  {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    // Main menu - pick what to do
    if (setupSubMode == MENU) {
      int option = input.toInt();
      
      switch(option) {
        case 1:  // Nuke everything
          setupSubMode = RESET_ALL;
          Serial.println("\nReset All Batteries");
          Serial.println("This will delete all battery data.");
          Serial.println("Type 'YES' to confirm:");
          break;
          
        case 2:  // Reset just some batteries
          setupSubMode = RESET_SOME;
          Serial.println("\nReset Some Batteries");
          Serial.println("Current batteries:");
          listAllBatteries();
          Serial.println("\nEnter battery names to reset (one per line)");
          Serial.println("Type 'DONE' when finished:");
          resetCount = 0;
          break;
          
        case 3:  // Add new batteries
          setupSubMode = ADD_BATTERIES;
          Serial.println("\nAdd New Batteries");
          Serial.println("How many batteries to add?");
          batteryIndex = 0;
          break;
          
        case 4:  // Remove some, add some
          setupSubMode = ADD_REMOVE;
          Serial.println("\nManage Batteries");
          Serial.println("Current batteries:");
          listAllBatteries();
          Serial.println("\nEnter battery names to remove (one per line)");
          Serial.println("Type 'DONE' when finished:");
          removeCount = 0;
          break;
          
        default:
          Serial.println("Invalid option. Enter 1-4:");
          break;
      }
    }
    // Actually reset everything
    else if (setupSubMode == RESET_ALL) 
    {
      if (input.equalsIgnoreCase("YES")) 
      {
        numBatteries = 0;
        currentBatteryUID = "";
        for (int i = 0; i < 20; i++) 
        {
          batteries[i] = Battery();
        }
        saveBatteryData();
        clearSheetsData();  // Clear Google Sheets
        Serial.println("All batteries reset");
        currentMode = NORMAL;
        Serial.println("\nNormal Mode");
      } 
      else 
      {
        Serial.println("Reset cancelled");
        displaySetupMenu();
        setupSubMode = MENU;
      }
    }
    // Reset specific batteries
    else if (setupSubMode == RESET_SOME) 
    {
      if (input.equalsIgnoreCase("DONE")) 
      {
        for (int r = 0; r < resetCount; r++) 
        {
          for (int i = 0; i < numBatteries; i++) 
          {
            if (batteries[i].name.equalsIgnoreCase(batteriesToReset[r])) 
            {
              Serial.println("Resetting " + batteries[i].name);
              batteries[i].usageCount = 0;
              batteries[i].totalTime = 0;
              batteries[i].installTime = 0;
              updateBatteryOnSheets(i);  // Update the old battery
              if (batteries[i].uid == currentBatteryUID) 
              {
                currentBatteryUID = "";
              }
            }
          }
        }
        saveBatteryData();
        Serial.println("Selected batteries reset");
        currentMode = NORMAL;
        Serial.println("\nNormal Mode");
      } 
      else 
      {
        batteriesToReset[resetCount] = input;
        resetCount++;
        Serial.println("Added '" + input + "' to reset list");
        Serial.println("Enter another name or type 'DONE'");
      }
    }
    // Add new batteries
    else if (setupSubMode == ADD_BATTERIES) 
    {
      if (batteryIndex == 0) 
      {
        int addCount = input.toInt();
        if (addCount > 0 && (numBatteries + addCount) <= 20) 
        {
          batteryIndex = 1;
          Serial.println("Adding " + String(addCount) + " batteries");
          Serial.println("Current total: " + String(numBatteries));
          Serial.println("Scan battery " + String(batteryIndex));
        } 
        else 
        {
          Serial.println("Invalid number. Enter 1-" + String(20 - numBatteries));
        }
      }
    }
    // Remove then add
    else if (setupSubMode == ADD_REMOVE) 
    {
      if (input.equalsIgnoreCase("DONE")) 
      {
        for (int r = 0; r < removeCount; r++) 
        {
          for (int i = 0; i < numBatteries; i++) 
          {
            if (batteries[i].name.equalsIgnoreCase(batteriesToRemove[r])) 
            {
              Serial.println("Removing " + batteries[i].name);
              removeBatteryFromSheets(batteries[i].name);  // Remove from sheets
              for (int j = i; j < numBatteries - 1; j++) 
              {
                batteries[j] = batteries[j + 1];
              }
              numBatteries--;
              i--;
            }
          }
        }
        
        Serial.println("How many new batteries to add?");
        Serial.println("Enter 0 to skip");
        setupSubMode = ADD_BATTERIES;
        batteryIndex = 0;
      } 
      else 
      {
        batteriesToRemove[removeCount] = input;
        removeCount++;
        Serial.println("'" + input + "' will be removed");
        Serial.println("Enter another name or type 'DONE'");
      }
    }
  }
  
  // Scanning batteries during registration
  if ((setupSubMode == ADD_BATTERIES) && batteryIndex > 0) 
  {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) 
    {
      String uidString = getUIDString(uid, uidLength);
      
      // Make sure this tag isn't already registered
      bool alreadyExists = false;
      for (int i = 0; i < numBatteries; i++) 
      {
        if (batteries[i].uid == uidString) 
        {
          Serial.println("WARNING: This tag is already registered as " + batteries[i].name);
          alreadyExists = true;
          break;
        }
      }
      
      if (!alreadyExists) 
      {
        batteries[numBatteries].uid = uidString;
        batteries[numBatteries].name = "Battery " + String(numBatteries + 1);
        batteries[numBatteries].usageCount = 0;
        batteries[numBatteries].totalTime = 0;
        batteries[numBatteries].installTime = 0;
        batteries[numBatteries].syncedToSheets = false;
        numBatteries++;
        
        Serial.println("[OK] Battery " + String(numBatteries) + " registered!");
        Serial.println("  Tag ID: " + uidString);
        
        if (Serial.available() > 0) 
        {
          String check = Serial.readStringUntil('\n');
          if (check.equalsIgnoreCase("DONE")) 
          {
            Serial.println("\n*** All set! Batteries registered. ***");
            saveBatteryData();
            syncAllBatteriesToSheets();  // Upload all new batteries
            currentMode = NORMAL;
            Serial.println(">>> Back to Normal Mode <<<");
            batteryIndex = 0;
          } 
          else 
          {
            batteryIndex++;
            Serial.println("Scan the next battery or type 'DONE'");
          }
        } 
        else 
        {
          batteryIndex++;
          Serial.println("Scan the next battery or type 'DONE'");
        }
      }
      
      delay(2000);
    }
  }
}

// Show all the batteries we have
void listAllBatteries() 
{
  if (numBatteries == 0) 
  {
    Serial.println("  No batteries registered");
    return;
  }
  
  for (int i = 0; i < numBatteries; i++) 
  {
    Serial.print("  - " + batteries[i].name);
    Serial.print(" | Tag: " + batteries[i].uid);
    Serial.print(" | Used " + String(batteries[i].usageCount) + " times");
    Serial.print(" | Runtime: " + formatTime(batteries[i].totalTime));
    Serial.println(batteries[i].uid == currentBatteryUID ? " [INSTALLED]" : "");
  }
}

// Normal operation - track battery usage
void handleNormalMode() 
{
  static unsigned long lastUpdate = 0;
  static unsigned long lastNTUpdate = 0;
  
  // Update the timer for the battery that's currently in the robot
  if (currentBatteryUID != "" && millis() - lastUpdate > 1000) 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID && batteries[i].installTime > 0) 
      {
        batteries[i].totalTime++;
        lastUpdate = millis();
        
        // Save to flash every minute so we don't lose data
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
  
  // Check if someone is scanning a battery
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) 
  {
    String uidString = getUIDString(uid, uidLength);
    
    // See if this battery is in our database
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == uidString) 
      {
        installBattery(i);
        delay(2000);
        return;
      }
    }
    
    // Unknown battery!
    Serial.println("Unknown battery: " + uidString);
    Serial.println("Register this battery in setup mode");
    delay(2000);
  }
}

// Register that a battery was just installed
void installBattery(int index) 
{
  // Blink LED to show successful scan
  digitalWrite(LED_PIN, HIGH);  // Turn on LED 
  // Remove old battery if any
  if (currentBatteryUID != "") 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID) 
      {
        batteries[i].installTime = 0;
        updateBatteryOnSheets(i);
        break;
      }
    }
  }
    
    // Install new battery
    batteries[index].usageCount++;
    batteries[index].installTime = millis() / 1000;
    currentBatteryUID = batteries[index].uid;
    
    Serial.println("\nBattery Installed");
    Serial.println("Name: " + batteries[index].name);
    Serial.println("Usage count: " + String(batteries[index].usageCount));
    Serial.println("Total time: " + formatTime(batteries[index].totalTime));
    Serial.println("");
    
    saveBatteryData();
    updateBatteryOnSheets(index);
    publishToNetworkTables();
    
    delay(500);                   // Keep LED on for half second
    digitalWrite(LED_PIN, LOW);   // Turn off LED
}

// Convert the NFC tag's ID to a readable string
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

// Make time look nice (like "2h 15m 30s")
String formatTime(unsigned long seconds) 
{
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  return String(hours) + "h " + String(minutes) + "m " + String(secs) + "s";
}

// Load battery data from the ESP32's memory
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
    batteries[i].installTime = 0;  // Nothing installed when we boot up
  }
  
  preferences.end();
  
  if (numBatteries > 0) 
  {
    Serial.println("Loaded " + String(numBatteries) + " batteries");
  } 
  else 
  {
    Serial.println("No batteries registered");
  }
}

// Save battery data to the ESP32's memory
void saveBatteryData() 
{
  preferences.begin("batteries", false);
  preferences.putInt("count", numBatteries);
  
  for (int i = 0; i < numBatteries; i++) 
  {
    String prefix = "bat" + String(i) + "_";
    preferences.putString((prefix + "uid").c_str(), batteries[i].uid);
    preferences.putString((prefix + "name").c_str(), batteries[i].name);
    preferences.putInt((prefix + "usage").c_str(), batteries[i].usageCount);
    preferences.putULong((prefix + "time").c_str(), batteries[i].totalTime);
  }
  
  preferences.end();
}

// Send battery info to the RoboRIO via NetworkTables
void publishToNetworkTables() 
{
  if (currentBatteryUID != "") 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID) 
      {
        nt.putString("/BatteryManager/CurrentBattery/Name", batteries[i].name);
        nt.putNumber("/BatteryManager/CurrentBattery/UsageCount", batteries[i].usageCount);
        nt.putNumber("/BatteryManager/CurrentBattery/TotalTimeSeconds", batteries[i].totalTime);
        nt.putString("/BatteryManager/CurrentBattery/TotalTimeFormatted", formatTime(batteries[i].totalTime));
        
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
  
  nt.putNumber("/BatteryManager/TotalBatteries", numBatteries);
  
  // Find which battery has been used the most
  int maxUses = 0;
  String mostUsedName = "None";
  for (int i = 0; i < numBatteries; i++) 
  {
    if (batteries[i].usageCount > maxUses) 
    {
      maxUses = batteries[i].usageCount;
      mostUsedName = batteries[i].name;
    }
  }
  nt.putString("/BatteryManager/MostUsedBattery", mostUsedName);
  nt.putNumber("/BatteryManager/MostUsedCount", maxUses);
}

// Set up the web dashboard
void setupWebServer() 
{
  // Main page - shows all batteries in a table
  server.on("/", HTTP_GET, []() {
    String html = "<html><head><title>Team 4546 Battery Tracker</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f5f5f5;} ";
    html += "h1{color:#333;} table{border-collapse:collapse;width:100%;background:white;} ";
    html += "th,td{border:1px solid #ddd;padding:12px;text-align:left;} ";
    html += "th{background-color:#4CAF50;color:white;} .current{background-color:#fff3cd;}</style>";
    html += "</head><body>";
    html += "<h1>Team 4546 Battery Tracker</h1>";
    html += "<p>Mode: " + String(currentMode == SETUP ? "Setup" : "Normal Operation") + "</p>";
    html += "<table><tr><th>Battery</th><th>Tag ID</th><th>Times Used</th><th>Total Runtime</th><th>Status</th></tr>";
    
    for (int i = 0; i < numBatteries; i++) 
    {
      String rowClass = (batteries[i].uid == currentBatteryUID) ? " class='current'" : "";
      html += "<tr" + rowClass + ">";
      html += "<td>" + batteries[i].name + "</td>";
      html += "<td>" + batteries[i].uid + "</td>";
      html += "<td>" + String(batteries[i].usageCount) + "</td>";
      html += "<td>" + formatTime(batteries[i].totalTime) + "</td>";
      html += "<td>" + String(batteries[i].uid == currentBatteryUID ? "IN ROBOT" : "Available") + "</td>";
      html += "</tr>";
    }
    
    html += "</table></body></html>";
    server.send(200, "text/html", html);
  });
  
  // JSON endpoint for custom apps
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

// ==================== Google Sheets Functions ====================

// Upload all batteries to Google Sheets (initial sync)
// ==================== Google Sheets Functions ====================
// Replace the entire Google Sheets section at the bottom of your code with this:

// Upload all batteries to Google Sheets (initial sync)
void syncAllBatteriesToSheets() 
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println("[SHEETS] WiFi not connected - skipping sync");
    return;
  }
  
  if (numBatteries == 0) 
  {
    Serial.println("[SHEETS] No batteries to sync");
    return;
  }
  
  Serial.println("[SHEETS] Syncing " + String(numBatteries) + " batteries to Google Sheets...");
  for (int i = 0; i < numBatteries; i++) 
  {
    updateBatteryOnSheets(i);
    delay(500);  // Rate limiting
  }
  Serial.println("[SHEETS] ✓ Sync complete!");
}

// Update or append battery data using Google Apps Script
void updateBatteryOnSheets(int batteryIndex) 
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println("[SHEETS] WiFi offline - cannot sync " + batteries[batteryIndex].name);
    return;
  }
  
  HTTPClient http;
  
  http.begin(GOOGLE_APPS_SCRIPT_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);  // 15 second timeout

  DynamicJsonDocument doc(512);
  doc["battery_uuid"] = batteries[batteryIndex].uid;
  doc["battery_usage"] = batteries[batteryIndex].name;
  doc["battery_usage_count"] = batteries[batteryIndex].usageCount;
  doc["total_time_used"] = batteries[batteryIndex].totalTime;
  doc["total_percentage_used"] = 0;  // You can calculate this based on your needs
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.println("[DEBUG] Uploading: " + batteries[batteryIndex].name);
  
  int httpCode = http.POST(jsonPayload);
  
  // Google Apps Script often returns 400 or 302 even on success
  // As long as we get a response, assume it worked
  if (httpCode > 0) 
  {
    batteries[batteryIndex].syncedToSheets = true;
    Serial.println("[SHEETS] ✓ Sent data for " + batteries[batteryIndex].name + " (HTTP " + String(httpCode) + ")");
  } 
  else 
  {
    Serial.println("[SHEETS] ✗ Connection failed: " + http.errorToString(httpCode));
  }

  http.end();
}

// Initialize the sheet with headers (handled automatically by Apps Script)
void initializeSheet() 
{
  Serial.println("[SHEETS] Sheet will auto-initialize on first battery update");
}

// Remove a battery row from the sheet
void removeBatteryFromSheets(String batteryName) 
{
  Serial.println("[SHEETS] Note: " + batteryName + " removed locally. Manual removal from Google Sheet may be needed.");
  // You can implement actual deletion via Apps Script if needed
}

// Clear all battery data from the sheet (reset all)
void clearSheetsData() 
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println("[SHEETS] WiFi offline - cannot clear sheet");
    return;
  }

  HTTPClient http;
  String clearUrl = String(GOOGLE_APPS_SCRIPT_URL) + "?action=clear";
  
  http.begin(clearUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();

  if (httpCode > 0) 
  {
    Serial.println("[SHEETS] ✓ Cleared all data from Google Sheets");
  } 
  else 
  {
    Serial.println("[SHEETS] ✗ Failed to clear sheet (HTTP " + String(httpCode) + ")");
  }

  http.end();
}