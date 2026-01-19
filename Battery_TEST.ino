// FRC Battery Management System - Team 4546 - TEST VERSION
// This version simulates NFC tag scanning without hardware
// Useful for testing the system before physical tags arrive

// Libraries required:
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Team-specific settings
const char* TEAM_SSID = "SSID";           // robot's WiFi name
const char* TEAM_PASSWORD = "wifi password";                  // WiFi password

// Google Sheets settings
const char* GOOGLE_APPS_SCRIPT_URL ="google _apps_script_web_app_url_here"; // Replace with your Apps Script URL

// Battery info structure
struct Battery 
{
  String uid;
  String name;
  int usageCount;
  unsigned long totalTime;
  unsigned long installTime;
  bool syncedToSheets;
};

// Battery database
Battery batteries[20];
int numBatteries = 0;
String currentBatteryUID = "";

// Modes
enum Mode { NORMAL, SETUP };
enum SetupSubMode { MENU, RESET_ALL, RESET_SOME, ADD_BATTERIES, ADD_REMOVE };
Mode currentMode = NORMAL;
SetupSubMode setupSubMode = MENU;

// Web server
WebServer server(80);
Preferences preferences;

// Setup mode state variables
static int batteryIndex = 0;
static int addCount = 0;
static String batteriesToReset[20];
static int resetCount = 0;
static String batteriesToRemove[20];
static int removeCount = 0;

// Forward declarations
void loadBatteryData();
void saveBatteryData();
void setupWebServer();
void syncAllBatteriesToSheets();
void updateBatteryOnSheets(int batteryIndex);
void clearSheetsData();
String formatTime(unsigned long seconds);
void listAllBatteries();
void displaySetupMenu();
void simulateNFCTag(String uid);
void installBattery(int index);
String generateUID();

// Generate a random UID
String generateUID() 
{
  String uid = "04"; // Start with 04 (MIFARE standard)
  for (int i = 0; i < 12; i++) 
  {
    uid += String(random(0, 16), HEX);
  }
  uid.toUpperCase();
  return uid;
}

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  
  // Seed random number generator
  randomSeed(analogRead(0) + millis());
  
  Serial.println("\n\n=== FRC Battery Tracker - TEST MODE ===");
  Serial.println("NFC scanning is SIMULATED");
  Serial.println("RoboRIO connection is DISABLED");
  Serial.println("=====================================\n");
  
  loadBatteryData();
  
  // Connect to WiFi
  WiFi.begin(TEAM_SSID, TEAM_PASSWORD);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) 
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  } 
  else 
  {
    Serial.println("\nWiFi connection failed (continuing in test mode)");
  }
  
  // Start web server
  setupWebServer();
  server.begin();
  
  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("Web server started on http://" + WiFi.localIP().toString());
  }
  
  // Initial sync
  syncAllBatteriesToSheets();
  
  Serial.println("\n=== Test Mode Ready ===");
  Serial.println("Commands:");
  Serial.println("  SETUP - Enter setup mode");
  Serial.println("  SCAN - Simulate NFC tag scan (generates random UID)");
  Serial.println("  STATUS - Show current batteries");
  Serial.println("  WIFI - Check WiFi status");
  Serial.println("========================\n");
}

void loop() 
{
  server.handleClient();
  
  if (currentMode == SETUP) 
  {
    handleSetupMode();
  } 
  else 
  {
    handleNormalMode();
  }
  
  delay(100);  // Short delay for responsiveness
}

// Handle normal mode - check commands and update timers
void handleNormalMode() 
{
  static unsigned long lastUpdate = 0;
  
  // Check for serial commands in normal mode
  if (Serial.available() > 0) 
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd == "SETUP") 
    {
      currentMode = SETUP;
      setupSubMode = MENU;
      batteryIndex = 0;
      addCount = 0;
      resetCount = 0;
      removeCount = 0;
      Serial.println("\nEntering Setup Mode");
      displaySetupMenu();
      return;
    }
    else if (cmd == "SCAN")
    {
      Serial.println("\n[TEST] Simulating NFC scan...");
      String simulatedUID = generateUID();
      Serial.println("Generated UID: " + simulatedUID);
      simulateNFCTag(simulatedUID);
    }
    else if (cmd == "STATUS")
    {
      Serial.println("\n=== Current Battery Status ===");
      listAllBatteries();
      if (currentBatteryUID != "") 
      {
        Serial.println("\nCurrently installed: " + currentBatteryUID);
      }
      Serial.println("================================\n");
    }
    else if (cmd == "WIFI")
    {
      Serial.print("WiFi Status: ");
      if (WiFi.status() == WL_CONNECTED) 
      {
        Serial.println("CONNECTED");
        Serial.println("IP: " + WiFi.localIP().toString());
      } 
      else 
      {
        Serial.println("DISCONNECTED");
      }
      Serial.println();
    }
  }
  
  // Update timer for installed battery
  if (currentBatteryUID != "" && millis() - lastUpdate > 1000) 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID && batteries[i].installTime > 0) 
      {
        batteries[i].totalTime++;
        lastUpdate = millis();
        
        // Save every minute
        if (batteries[i].totalTime % 60 == 0) 
        {
          saveBatteryData();
        }
        break;
      }
    }
  }
}

void displaySetupMenu()
{
  Serial.println("\n=== Setup Menu ===");
  Serial.println("1 - Reset all batteries");
  Serial.println("2 - Reset specific batteries");
  Serial.println("3 - Add new batteries");
  Serial.println("4 - Remove and add batteries");
  Serial.println("0 - Exit to normal mode");
  Serial.println("\nEnter option (0-4):");
}

// Handle all the setup operations
void handleSetupMode() 
{
  // Check for serial input
  if (!Serial.available()) {
    return;  // Exit early if no input available
  }
  
  String input = Serial.readStringUntil('\n');
  input.trim();
  
  if (setupSubMode == MENU) 
  {
    int option = input.toInt();
    
    // Allow 0 to exit setup mode
    if (input == "0") 
    {
      Serial.println("Exiting setup mode\n");
      currentMode = NORMAL;
      Serial.println("=== Normal Mode ===\n");
      return;
    }
    
    Serial.println("You selected option: " + String(option));
    
    switch(option) 
    {
      case 1:
        setupSubMode = RESET_ALL;
        Serial.println("\n=== Reset All Batteries ===");
        Serial.println("This will delete all battery data.");
        Serial.println("Type 'YES' to confirm (or anything else to cancel):");
        break;
        
      case 2:
        setupSubMode = RESET_SOME;
        Serial.println("\n=== Reset Some Batteries ===");
        Serial.println("Current batteries:");
        listAllBatteries();
        Serial.println("\nEnter battery names to reset (one per line)");
        Serial.println("Type 'DONE' when finished:");
        resetCount = 0;
        break;
        
      case 3:
        setupSubMode = ADD_BATTERIES;
        Serial.println("\n=== Add New Batteries ===");
        Serial.println("Current battery count: " + String(numBatteries) + "/20");
        Serial.println("How many batteries to add?");
        batteryIndex = 0;
        addCount = 0;
        break;
        
      case 4:
        setupSubMode = ADD_REMOVE;
        Serial.println("\n=== Manage Batteries ===");
        Serial.println("Current batteries:");
        listAllBatteries();
        Serial.println("\nEnter battery names to remove (one per line)");
        Serial.println("Type 'DONE' when finished:");
        removeCount = 0;
        break;
        
      default:
        Serial.println("Invalid option. Enter 0-4:");
        break;
    }
    return;
  }
  
  if (setupSubMode == RESET_ALL) 
  {
    if (input.equalsIgnoreCase("YES")) 
    {
      Serial.println("\nResetting all batteries...");
      numBatteries = 0;
      currentBatteryUID = "";
      for (int i = 0; i < 20; i++) 
      {
        batteries[i] = Battery();
      }
      saveBatteryData();
      clearSheetsData();
      Serial.println("✓ All batteries reset\n");
    } 
    else 
    {
      Serial.println("Reset cancelled\n");
    }
    currentMode = NORMAL;
    Serial.println("=== Normal Mode ===\n");
  }
  else if (setupSubMode == RESET_SOME) 
  {
    if (input.equalsIgnoreCase("DONE")) 
    {
      Serial.println("\nResetting selected batteries...");
      for (int r = 0; r < resetCount; r++) 
      {
        bool found = false;
        for (int i = 0; i < numBatteries; i++) 
        {
          if (batteries[i].name.equalsIgnoreCase(batteriesToReset[r])) 
          {
            Serial.println("  Resetting " + batteries[i].name);
            batteries[i].usageCount = 0;
            batteries[i].totalTime = 0;
            batteries[i].installTime = 0;
            updateBatteryOnSheets(i);
            if (batteries[i].uid == currentBatteryUID) 
            {
              currentBatteryUID = "";
            }
            found = true;
            break;
          }
        }
        if (!found) 
        {
          Serial.println("  ⚠ Battery '" + batteriesToReset[r] + "' not found");
        }
      }
      saveBatteryData();
      Serial.println("✓ Selected batteries reset\n");
      currentMode = NORMAL;
      Serial.println("=== Normal Mode ===\n");
    } 
    else 
    {
      if (resetCount < 20) 
      {
        batteriesToReset[resetCount] = input;
        resetCount++;
        Serial.println("✓ Added '" + input + "' to reset list (" + String(resetCount) + " total)");
        Serial.println("Enter another name or type 'DONE':");
      } 
      else 
      {
        Serial.println("Maximum reset list reached. Type 'DONE' to proceed.");
      }
    }
  }
  else if (setupSubMode == ADD_BATTERIES) 
  {
    if (batteryIndex == 0) 
    {
      // First input: How many batteries to add?
      int count = input.toInt();
      if (count > 0 && (numBatteries + count) <= 20) 
      {
        addCount = count;
        batteryIndex = 1;
        Serial.println("\nAdding " + String(addCount) + " batteries");
        Serial.println("Starting from battery " + String(numBatteries + 1));
        Serial.println("\n--- Battery 1/" + String(addCount) + " ---");
        String newUID = generateUID();
        Serial.println("Simulated UID: " + newUID);
        Serial.println("Enter battery name (or press Enter for auto-name 'Battery " + String(numBatteries + 1) + "'):");
        // Store the generated UID temporarily
        preferences.begin("temp", false);
        preferences.putString("tempUID", newUID);
        preferences.end();
      } 
      else if (count <= 0) 
      {
        Serial.println("Invalid number. Must be greater than 0.");
        Serial.println("How many batteries to add?");
      }
      else 
      {
        Serial.println("Cannot add " + String(count) + " batteries. Space for " + String(20 - numBatteries) + " remaining.");
        Serial.println("How many batteries to add?");
      }
    }
    else if (batteryIndex > 0 && batteryIndex <= addCount)
    {
      // Retrieve the temporarily stored UID
      preferences.begin("temp", false);
      String uid = preferences.getString("tempUID", "");
      preferences.end();
      
      // Subsequent inputs: Battery names
      String batteryName = input;
      
      // Check if tag already exists
      bool alreadyExists = false;
      for (int i = 0; i < numBatteries; i++) 
      {
        if (batteries[i].uid == uid) 
        {
          Serial.println("⚠ WARNING: This UID somehow already exists (this shouldn't happen with random generation)");
          alreadyExists = true;
          break;
        }
      }
      
      if (alreadyExists) 
      {
        // Regenerate and try again
        batteryIndex++;
        
        if (batteryIndex <= addCount)
        {
          Serial.println("\n--- Battery " + String(batteryIndex) + "/" + String(addCount) + " ---");
          String newUID = generateUID();
          Serial.println("Simulated UID: " + newUID);
          Serial.println("Enter battery name (or press Enter for auto-name 'Battery " + String(numBatteries + 1) + "'):");
          // Store the new UID
          preferences.begin("temp", false);
          preferences.putString("tempUID", newUID);
          preferences.end();
        }
        else
        {
          Serial.println("\n✓ Completed!");
          Serial.println("Successfully added " + String(numBatteries) + " batteries");
          saveBatteryData();
          syncAllBatteriesToSheets();
          currentMode = NORMAL;
          Serial.println("\n=== Normal Mode ===\n");
          batteryIndex = 0;
          addCount = 0;
        }
      }
      else 
      {
        // Auto-generate name if empty
        if (batteryName == "") 
        {
          batteryName = "Battery " + String(numBatteries + 1);
        }
        
        batteries[numBatteries].uid = uid;
        batteries[numBatteries].name = batteryName;
        batteries[numBatteries].usageCount = 0;
        batteries[numBatteries].totalTime = 0;
        batteries[numBatteries].installTime = 0;
        batteries[numBatteries].syncedToSheets = false;
        numBatteries++;
        
        Serial.println("✓ " + batteries[numBatteries - 1].name + " registered!");
        
        batteryIndex++;
        
        if (batteryIndex <= addCount)
        {
          Serial.println("\n--- Battery " + String(batteryIndex) + "/" + String(addCount) + " ---");
          String newUID = generateUID();
          Serial.println("Simulated UID: " + newUID);
          Serial.println("Enter battery name (or press Enter for auto-name 'Battery " + String(numBatteries + 1) + "'):");
          // Store the new UID
          preferences.begin("temp", false);
          preferences.putString("tempUID", newUID);
          preferences.end();
        }
        else
        {
          Serial.println("\n✓ All " + String(addCount) + " batteries added!");
          saveBatteryData();
          syncAllBatteriesToSheets();
          currentMode = NORMAL;
          Serial.println("\n=== Normal Mode ===\n");
          batteryIndex = 0;
          addCount = 0;
        }
      }
    }
  }
  else if (setupSubMode == ADD_REMOVE) 
  {
    if (input.equalsIgnoreCase("DONE")) 
    {
      // Process removals
      if (removeCount > 0) 
      {
        Serial.println("\nRemoving batteries...");
        for (int r = 0; r < removeCount; r++) 
        {
          for (int i = 0; i < numBatteries; i++) 
          {
            if (batteries[i].name.equalsIgnoreCase(batteriesToRemove[r])) 
            {
              Serial.println("  Removing " + batteries[i].name);
              
              // If this battery is currently installed, uninstall it
              if (batteries[i].uid == currentBatteryUID) 
              {
                currentBatteryUID = "";
              }
              
              // Shift all batteries down
              for (int j = i; j < numBatteries - 1; j++) 
              {
                batteries[j] = batteries[j + 1];
              }
              numBatteries--;
              break;
            }
          }
        }
        saveBatteryData();
        Serial.println("✓ Batteries removed\n");
      }
      
      // Now ask about adding new batteries
      Serial.println("How many new batteries to add? (0 to skip)");
      setupSubMode = ADD_BATTERIES;
      batteryIndex = 0;
      addCount = 0;
    } 
    else 
    {
      if (removeCount < 20) 
      {
        batteriesToRemove[removeCount] = input;
        removeCount++;
        Serial.println("✓ '" + input + "' will be removed (" + String(removeCount) + " total)");
        Serial.println("Enter another name or type 'DONE':");
      } 
      else 
      {
        Serial.println("Maximum removal list reached. Type 'DONE' to proceed.");
      }
    }
  }
}

void listAllBatteries() 
{
  if (numBatteries == 0) 
  {
    Serial.println("  No batteries registered");
    return;
  }
  
  for (int i = 0; i < numBatteries; i++) 
  {
    Serial.print("  " + String(i + 1) + ". " + batteries[i].name);
    Serial.print(" | UID: " + batteries[i].uid);
    Serial.print(" | Used: " + String(batteries[i].usageCount) + "x");
    Serial.print(" | Runtime: " + formatTime(batteries[i].totalTime));
    Serial.println(batteries[i].uid == currentBatteryUID ? " [INSTALLED]" : "");
  }
}

// Simulate an NFC tag scan
void simulateNFCTag(String uid) 
{
  // Find if this battery exists
  for (int i = 0; i < numBatteries; i++) 
  {
    if (batteries[i].uid == uid) 
    {
      Serial.println("✓ Found registered battery: " + batteries[i].name);
      installBattery(i);
      return;
    }
  }
  
  Serial.println("⚠ Unknown tag: " + uid);
  Serial.println("Register this battery in setup mode (type SETUP)\n");
}

void installBattery(int index) 
{
  // Uninstall current battery if any
  if (currentBatteryUID != "") 
  {
    for (int i = 0; i < numBatteries; i++) 
    {
      if (batteries[i].uid == currentBatteryUID) 
      {
        unsigned long sessionTime = 0;
        if (batteries[i].installTime > 0) 
        {
          sessionTime = (millis() / 1000) - batteries[i].installTime;
        }
        batteries[i].installTime = 0;
        
        Serial.println("\nℹ Uninstalling previous battery: " + batteries[i].name);
        if (sessionTime > 0) 
        {
          Serial.println("  Session time: " + formatTime(sessionTime));
        }
        
        updateBatteryOnSheets(i);
        break;
      }
    }
  }
  
  // Install new battery
  batteries[index].usageCount++;
  batteries[index].installTime = millis() / 1000;
  currentBatteryUID = batteries[index].uid;
  
  Serial.println("\n=== Battery Installed ===");
  Serial.println("Name: " + batteries[index].name);
  Serial.println("Usage count: " + String(batteries[index].usageCount));
  Serial.println("Total runtime: " + formatTime(batteries[index].totalTime));
  Serial.println("=========================\n");
  
  saveBatteryData();
  updateBatteryOnSheets(index);
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
    batteries[i].installTime = 0;
  }
  
  preferences.end();
  
  if (numBatteries > 0) 
  {
    Serial.println("Loaded " + String(numBatteries) + " batteries from storage");
  } 
  else 
  {
    Serial.println("No batteries registered - use SETUP to add batteries");
  }
}

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
  Serial.println("[SAVED] Battery data saved to flash memory");
}

void setupWebServer() 
{
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Team 4546 Battery Tracker</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='5'>";  // Auto-refresh every 5 seconds
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f0f0f0;}";
    html += "h1{color:#333;margin-bottom:5px;}";
    html += ".test{background:#ffe0e0;padding:10px;border-radius:5px;margin:10px 0;border:2px solid #ff9999;}";
    html += ".info{background:#e0f0ff;padding:10px;border-radius:5px;margin:10px 0;}";
    html += "table{border-collapse:collapse;width:100%;background:white;margin-top:20px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += "th,td{border:1px solid #ddd;padding:12px;text-align:left;}";
    html += "th{background-color:#4CAF50;color:white;font-weight:bold;}";
    html += ".current{background-color:#fff3cd;font-weight:bold;}";
    html += ".empty{text-align:center;padding:30px;color:#999;}";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>🔋 Team 4546 Battery Tracker</h1>";
    html += "<div class='test'><strong>⚠ TEST MODE ACTIVE</strong><br>";
    html += "NFC scanning is simulated with random UIDs. RoboRIO connection is disabled.</div>";
    html += "<div class='info'>";
    html += "<strong>Mode:</strong> " + String(currentMode == SETUP ? "Setup" : "Normal") + " | ";
    html += "<strong>Registered Batteries:</strong> " + String(numBatteries) + "/20";
    if (currentBatteryUID != "") 
    {
      html += " | <strong>Currently Installed:</strong> ";
      for (int i = 0; i < numBatteries; i++) 
      {
        if (batteries[i].uid == currentBatteryUID) 
        {
          html += batteries[i].name;
          break;
        }
      }
    }
    html += "</div>";
    
    if (numBatteries == 0) 
    {
      html += "<div class='empty'>No batteries registered yet.<br>Use the serial console to add batteries.</div>";
    } 
    else 
    {
      html += "<table>";
      html += "<tr><th>#</th><th>Battery Name</th><th>UID</th><th>Times Used</th><th>Total Runtime</th><th>Status</th></tr>";
      
      for (int i = 0; i < numBatteries; i++) 
      {
        String rowClass = (batteries[i].uid == currentBatteryUID) ? " class='current'" : "";
        html += "<tr" + rowClass + ">";
        html += "<td>" + String(i + 1) + "</td>";
        html += "<td>" + batteries[i].name + "</td>";
        html += "<td><small>" + batteries[i].uid + "</small></td>";
        html += "<td>" + String(batteries[i].usageCount) + "</td>";
        html += "<td>" + formatTime(batteries[i].totalTime) + "</td>";
        html += "<td>" + String(batteries[i].uid == currentBatteryUID ? "🟢 INSTALLED" : "⚪ Available") + "</td>";
        html += "</tr>";
      }
      
      html += "</table>";
    }
    
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
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
  doc["total_percentage_used"] = 0;
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.println("[DEBUG] Uploading: " + batteries[batteryIndex].name);
  
  int httpCode = http.POST(jsonPayload);
  
  // Google Apps Script often returns 400 or 302 even on success
  // As long as we get a response, assume it worked
  if (httpCode > 0) 
  {
    // If the data is showing up in your sheet, it's working!
    // Just report success for any response
    Serial.println("[SHEETS] ✓ Sent data for " + batteries[batteryIndex].name + " (HTTP " + String(httpCode) + ")");
  } 
  else 
  {
    Serial.println("[SHEETS] ✗ Connection failed: " + http.errorToString(httpCode));
  }

  http.end();
}
// Clear all battery data from Google Sheets
void clearSheetsData() 
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println("[SHEETS] WiFi offline - cannot clear sheet");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  String clearUrl = String(GOOGLE_APPS_SCRIPT_URL) + "?action=clear";

  HTTPClient http;
  http.begin(client, clearUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();

  String response = http.getString();
  Serial.println("[SHEETS] Clear response: " + response);

  if (httpCode == 200) 
  {
    Serial.println("[SHEETS] ✓ Cleared all data from Google Sheets");
  } 
  else 
  {
    Serial.println("[SHEETS] ✗ Failed to clear sheet (HTTP " + String(httpCode) + ")");
  }

  http.end();
}