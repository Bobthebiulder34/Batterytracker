# FRC Battery Tracker - How It Actually Works (6-Battery PWM Edition)

## What This Thing Does

Alright, so here's the deal: this system keeps track of your robot batteries AND tells your RoboRIO which specific battery is installed using a unique PWM signal. You know how batteries just kinda disappear or you forget which ones you've been using too much? This fixes that. You stick cheap NFC stickers on each of your 6 batteries, wave them near a reader, and boom - it tracks everything automatically AND sends a signal to your robot telling it exactly which battery is in.

**New in this version:** Each of your 6 batteries gets its own unique PWM signal that the RoboRIO can read. No more guessing which battery is in the robot - the robot KNOWS.

---

## The Hardware Stuff You Need

Let's start with what you're actually building:

**The Brain:** ESP32 microcontroller board (like ten bucks on Amazon)

**The Scanner:** PN532 NFC reader module (another ten bucks or so)

**The Tags:** NFC stickers for your batteries (literally like 50 cents each)

**New: Connection to RoboRIO:** One wire for PWM signal, one for ground

**How to Hook It Up:**

**ESP32 to NFC Reader:**
```
PN532 SDA → ESP32 pin 21 (this is the data line)
PN532 SCL → ESP32 pin 22 (this is the clock line)
PN532 IRQ → ESP32 pin 2  (tells the ESP32 "hey I found something!")
PN532 RST → ESP32 pin 3  (resets the reader if things get weird)
Power everything with 3.3V (don't use 5V or you'll fry stuff)
```

**ESP32 to RoboRIO (NEW!):**
```
ESP32 pin 5 (PWM output) → RoboRIO DIO 0
ESP32 GND               → RoboRIO GND
```

That's it. Pretty simple wiring honestly.

---

## The 6-Battery System and PWM Signals

**Big Change:** This version only accepts exactly 6 batteries (not 20). Each battery gets a unique PWM signal:

| Battery | PWM Value | What It Means |
|---------|-----------|---------------|
| Battery 1 | 21  | RoboRIO sees ~21 on DIO 0 |
| Battery 2 | 64  | RoboRIO sees ~64 on DIO 0 |
| Battery 3 | 106 | RoboRIO sees ~106 on DIO 0 |
| Battery 4 | 148 | RoboRIO sees ~148 on DIO 0 |
| Battery 5 | 191 | RoboRIO sees ~191 on DIO 0 |
| Battery 6 | 233 | RoboRIO sees ~233 on DIO 0 |
| No Battery | 0 | RoboRIO sees 0 |

These values are evenly spaced across the 0-255 PWM range so the RoboRIO can easily tell them apart.

**How it works:**
1. You scan Battery 3 on the ESP32
2. ESP32 sends PWM signal of 106 on pin 5
3. RoboRIO reads that on DIO 0
4. RoboRIO code says "Oh, Battery 3 is installed!"

**Why this is cool:**
- Robot code can make decisions based on which battery is in
- Driver station can show which battery you're using
- No need to manually tell the robot which battery you installed
- Works even if NetworkTables goes down

---

## What Each Library Does

You know how you have to import a bunch of stuff at the top? Here's what they actually do:

- **Wire.h** - Talks to the NFC reader using I2C (a communication protocol)
- **Adafruit_PN532.h** - Makes the NFC reader actually work without writing 500 lines of code
- **WiFi.h** - Gets you on the network, obviously
- **WebServer.h** - Creates that little web page you can check
- **Preferences.h** - Saves stuff to the ESP32's memory so data doesn't disappear when you unplug it
- **ArduinoJson.h** - Formats data nicely for sending to Google Sheets
- **NetworkTables.h** - Talks to your RoboRIO (the robot's main computer)
- **HTTPClient.h** - Sends data to the internet (Google Sheets)

---

## The Settings You Need to Change

At the top of the code, there's a bunch of stuff you gotta customize:

```cpp
const char* TEAM_SSID = "PLACEHOLDER";      // Your robot's WiFi name
const char* TEAM_PASSWORD = "";             // WiFi password
const char* ROBORIO_IP = "PLACEHOLDER";     // Usually something like "10.TE.AM.2"
```

The RoboRIO IP is usually `10.[team number first 2 digits].[team number last 2 digits].2`. So if you're team 4546, it's `10.45.46.2`.

And then there's your Google Sheets URL - we'll get to that later.

**New PWM Settings:**
```cpp
const int PWM_PIN = 5;  // Where the PWM signal comes out
const int PWM_VALUES[MAX_BATTERIES] = {21, 64, 106, 148, 191, 233};  // Signals for each battery
```

---

## How Battery Data Gets Stored

Each battery has a little profile that looks like this:

```cpp
struct Battery {
  String uid;                  // The tag's unique ID (like a fingerprint)
  String name;                 // What you call it ("Battery 1", "Battery 2", etc.)
  int usageCount;              // How many times you've used it
  unsigned long totalTime;     // Total seconds it's been running
  unsigned long installTime;   // When you put it in (0 means it's chilling)
  bool syncedToSheets;         // Did we upload this yet?
  int pwmValue;                // NEW: This battery's unique PWM signal
};
```

The code can remember exactly 6 batteries. That's stored in an array:

```cpp
#define MAX_BATTERIES 6                 // Hard limit: 6 batteries only
Battery batteries[MAX_BATTERIES];       // Your 6 batteries
int numBatteries = 0;                   // How many you've registered (max 6)
String currentBatteryUID = "";          // Which one's in the robot right now
```

**Why only 6?** Because we're using PWM signals to identify them, and 6 batteries gives us nice, evenly-spaced signals that are easy to tell apart. More batteries would make the signals too close together and cause errors.

---

## Startup Sequence - What Happens When You Plug It In

When you power on the ESP32, here's what goes down:

### 1. Wake Up the Serial Port
```cpp
Serial.begin(115200);
```
This opens up communication so you can see what's happening in the serial monitor.

### 2. Find the NFC Reader
```cpp
nfc.begin();
uint32_t versiondata = nfc.getFirmwareVersion();
if (!versiondata) {
  Serial.println("Can't find NFC reader!");
  while (1);  // Just stops here until you fix the wiring
}
```

If it can't find the reader, it just gives up. Go check your wiring.

### 3. Load Your Old Data
```cpp
loadBatteryData();
```
Grabs all the battery info you saved last time. Even if you unplugged everything.

### 4. Connect to WiFi
```cpp
WiFi.begin(TEAM_SSID, TEAM_PASSWORD);
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");  // Prints dots while connecting
}
```

It'll just sit there printing dots until it connects. If it never connects, you probably typed the password wrong.

### 5. Start the Web Server
```cpp
setupWebServer();
server.begin();
```
Now you can visit the ESP32's IP address in a browser and see a nice dashboard.

### 6. Talk to the Robot
```cpp
nt.connect(ROBORIO_IP, NT_PORT);
```
Connects to NetworkTables so your robot code can see battery info.

### 7. Upload Everything to Google Sheets
```cpp
syncAllBatteriesToSheets();
```
Sends all your current battery data to the cloud.

### 8. Initialize PWM Output (NEW!)
```cpp
pinMode(PWM_PIN, OUTPUT);
analogWrite(PWM_PIN, 0);  // Start with no signal (no battery installed)
```

### 9. Show PWM Mapping
The startup messages now include:
```
Battery Management System Ready (6-Battery System)
PWM Signal Assignments:
  Battery 1 → PWM 21
  Battery 2 → PWM 64
  Battery 3 → PWM 106
  Battery 4 → PWM 148
  Battery 5 → PWM 191
  Battery 6 → PWM 233
```

---

## The Main Loop - What Happens Continuously

The loop runs forever, checking stuff every 2 seconds:

```cpp
void loop() {
  server.handleClient();      // Check if anyone's looking at the web page
  nt.update();                // Keep talking to the RoboRIO
  checkSerialCommand();       // Did someone type something?
  
  if (currentMode == SETUP) {
    handleSetupMode();        // You're configuring batteries
  } else {
    handleNormalMode();       // Normal tracking mode
  }
  
  delay(2000);                // Wait 2 seconds, then do it again
}
```

Pretty straightforward - just keep checking everything.

---

## Setup Mode - Adding and Managing Batteries

Type "SETUP" in the serial monitor and you get a menu:

```
1 - Reset all batteries       (nuclear option, deletes everything)
2 - Reset specific batteries  (zero out some batteries but keep others)
3 - Add new batteries        (register new ones - MAX 6 TOTAL)
4 - Remove and add batteries (clean up your list)
```

**IMPORTANT:** You can only have 6 batteries total. If you try to add more, it'll tell you to remove some first.

### Option 1: Reset Everything
Just nukes all your data. It'll ask you to type "YES" to make sure you're not being dumb. Then it:
- Wipes all 6 battery slots
- Clears PWM signal (sets to 0)
- Clears the ESP32's memory
- Clears your Google Sheet

### Option 2: Reset Some Batteries
Say you want to reset "Battery 3" and "Battery 5" but keep the others:
1. Choose option 2
2. Type "Battery 3" and hit enter
3. Type "Battery 5" and hit enter
4. Type "DONE"
5. It resets just those two (but they keep their PWM assignments)

### Option 3: Adding New Batteries
This is where the PWM magic happens:
1. Tell it how many batteries you want to add (max 6 minus what you already have)
2. Scan each battery's NFC tag when it tells you to
3. It auto-assigns:
   - Name: "Battery 1", "Battery 2", etc.
   - PWM Value: Based on position (1st = 21, 2nd = 64, etc.)
4. Type "DONE" when you're finished
5. Everything gets saved and uploaded

**Example output when scanning:**
```
[OK] Battery 1 registered!
  Tag ID: 04A1B2C3D4E5F6
  PWM Signal: 21

[OK] Battery 2 registered!
  Tag ID: 04D1E2F3A4B5C6
  PWM Signal: 64
```

**Important:** Once a battery gets assigned a PWM value (like Battery 1 = 21), that's permanent. Even if you reset it, Battery 1 will always be PWM 21.

### Option 4: Remove Old, Add New
Perfect for when you retire some batteries:
1. Type the names of batteries you want to remove
2. Type "DONE"
3. Clears PWM signal if you removed the installed battery
4. Then asks how many new ones you want to add
5. Follow the same process as option 3

**Note:** If you remove batteries, the new ones you add will fill in the lowest available slots. So if you remove Battery 2, the next battery you add becomes the new Battery 2 with PWM = 64.

---

## Normal Mode - Actually Tracking Batteries

Once you're out of setup mode, here's what happens:

### The Timer
Every single second, if there's a battery installed:
```cpp
batteries[i].totalTime++;  // Add one second
```

That's it. Just counts up. Simple.

Every 60 seconds, it saves to flash memory:
```cpp
if (batteries[i].totalTime % 60 == 0) {
  saveBatteryData();  // Don't lose data if power goes out
}
```

### The Scanner
Every 2 seconds (because of that delay in the loop), it checks for NFC tags:

```cpp
if (nfc.readPassiveTargetID(...)) {
  // Found a tag! What is it?
  String uidString = getUIDString(uid, uidLength);
  
  // Is it in our database?
  for (int i = 0; i < numBatteries; i++) {
    if (batteries[i].uid == uidString) {
      installBattery(i);  // Yep! Install it and send PWM signal
      return;
    }
  }
  
  // Nope, never seen this tag before
  Serial.println("Unknown battery!");
}
```

### NetworkTables Updates
Every 5 seconds, it sends fresh data to the RoboRIO including the battery number and PWM value.

---

## What Happens When You Scan a Battery

This is the heart of the whole system. When you wave a battery near the reader:

### Step 1: Remove the Old Battery (If Any)
```cpp
if (currentBatteryUID != "") {
  // Find the old battery
  // Set its installTime to 0 (means "not installed anymore")
  // Send its final data to Google Sheets
}
```

**SUPER IMPORTANT:** The battery only gets "removed" when you scan a different battery. If you just pull it out without scanning a new one, the timer keeps going! This is probably the biggest gotcha in the whole system.

### Step 2: Install the New Battery
```cpp
batteries[index].usageCount++;                    // "This battery has been used X times"
batteries[index].installTime = millis() / 1000;   // Remember when we put it in
currentBatteryUID = batteries[index].uid;         // This is now the current battery
```

### Step 3: Send PWM Signal (NEW!)
```cpp
sendPWMSignal(index);  // Send this battery's unique PWM value
```

This is where the magic happens:
```cpp
void sendPWMSignal(int batteryIndex) {
  int pwmValue = batteries[batteryIndex].pwmValue;  // Get the battery's PWM (21, 64, 106, etc.)
  analogWrite(PWM_PIN, pwmValue);                   // Send it out on pin 5
  
  Serial.println("[PWM] Sending signal " + String(pwmValue) + " for " + batteries[batteryIndex].name);
}
```

The RoboRIO now knows exactly which battery is installed!

### Step 4: Tell Everyone
```cpp
saveBatteryData();              // Save to flash (survives power loss)
updateBatteryOnSheets(index);   // Upload to Google Sheets (now includes PWM value!)
publishToNetworkTables();       // Tell the RoboRIO
```

And boom, you're done. The new battery is installed, being tracked, AND the robot knows which one it is.

---

## The PWM Signal System - How the RoboRIO Knows

Here's the full workflow:

**ESP32 Side:**
1. Battery scanned → ESP32 identifies it as Battery 3
2. ESP32 looks up Battery 3's PWM value → 106
3. ESP32 sends PWM signal of 106 on pin 5
4. Signal stays on until a different battery is scanned

**RoboRIO Side:**
1. Reads digital input on DIO 0 as a duty cycle (0.0 to 1.0)
2. Converts to 0-255 scale → gets ~106
3. Checks which battery that matches (with ±10 tolerance):
   - Is it 21±10? No → Not Battery 1
   - Is it 64±10? No → Not Battery 2
   - Is it 106±10? Yes! → Battery 3!
4. Robot code now knows Battery 3 is installed

**Why the ±10 tolerance?**
PWM signals aren't perfect. Electrical noise, wiring resistance, etc. can make the signal drift a bit. So if we expect 106 but get 110, that's fine - it's within tolerance and still clearly Battery 3.

**What the robot sees on SmartDashboard:**
```
Battery/Name: "Battery 3"
Battery/Number: 3
Battery/PWM Value: 106
Battery/Installed: true
Battery/Usage Count: 15
Battery/Total Runtime: "2h 30m 15s"
```

---

## Saving Data - How It Survives Reboots

The ESP32 has flash memory that doesn't get erased when you unplug it. Think of it like a tiny hard drive.

### Saving:
```cpp
preferences.begin("batteries", false);  // Open the "batteries" storage area
preferences.putInt("count", numBatteries);  // Save how many batteries (max 6)

// For each battery, save its info
for (int i = 0; i < numBatteries; i++) {
  String prefix = "bat" + String(i) + "_";  // Like "bat0_", "bat1_", etc.
  preferences.putString((prefix + "uid").c_str(), batteries[i].uid);
  preferences.putString((prefix + "name").c_str(), batteries[i].name);
  preferences.putInt((prefix + "usage").c_str(), batteries[i].usageCount);
  preferences.putULong((prefix + "time").c_str(), batteries[i].totalTime);
  // PWM value is NOT saved because it's based on position
}

preferences.end();  // Close it up
```

It creates keys like:
- `bat0_uid` = "04A1B2C3D4E5F6"
- `bat0_name` = "Battery 1"
- `bat0_usage` = 5
- `bat0_time` = 7200

### Loading:
When the ESP32 boots up, it reads all that stuff back AND reassigns PWM values based on position:
```cpp
for (int i = 0; i < numBatteries; i++) {
  // Load all the saved data...
  batteries[i].uid = preferences.getString(...);
  batteries[i].name = preferences.getString(...);
  batteries[i].usageCount = preferences.getInt(...);
  batteries[i].totalTime = preferences.getULong(...);
  
  // Restore PWM value based on position
  batteries[i].pwmValue = PWM_VALUES[i];  // Battery in slot 0 gets PWM 21, slot 1 gets 64, etc.
}
```

This means Battery 1 will ALWAYS have PWM signal 21, Battery 2 will ALWAYS have 64, etc.

---

## The Web Dashboard

You can visit the ESP32 in a browser (just type its IP address) and see a nice table:

| Battery | Tag ID | PWM Signal | Times Used | Total Runtime | Status |
|---------|--------|------------|------------|---------------|--------|
| Battery 1 | 04A1B2C3... | 21 | 5 | 2h 15m 30s | Available |
| Battery 2 | 04D1E2F3... | 64 | 3 | 1h 45m 12s | IN ROBOT |
| Battery 3 | 04A4B5C6... | 106 | 8 | 3h 20m 45s | Available |

The currently installed battery is highlighted in yellow. The PWM Signal column shows which signal the RoboRIO will see.

There's also a JSON API at `/api/batteries` that now includes PWM values:
```json
[
  {
    "name": "Battery 1",
    "uid": "04A1B2C3D4E5F6",
    "usageCount": 5,
    "totalTime": 8130,
    "pwmValue": 21,
    "batteryNumber": 1,
    "installed": false
  },
  {
    "name": "Battery 2",
    "uid": "04D1E2F3A4B5C6",
    "usageCount": 3,
    "totalTime": 6312,
    "pwmValue": 64,
    "batteryNumber": 2,
    "installed": true
  }
]
```

---

## Google Sheets Integration

This is where all your historical data lives.

### When Data Gets Uploaded:
- When you install a battery
- When you remove a battery (by installing a different one)
- When the system boots up
- When you reset batteries in setup mode

### What Gets Sent (Updated):
```json
{
  "battery_uuid": "04A1B2C3D4E5F6",
  "battery_usage": "Battery 1",
  "battery_usage_count": 5,
  "total_time_used": 7200,
  "total_percentage_used": 0,
  "pwm_value": 21,
  "battery_number": 1
}
```

The Google Apps Script on the other end either:
- Updates an existing row if it finds a matching UUID
- Creates a new row if this is a new battery

**Your Google Sheet will look like:**
| UUID | Name | Usage Count | Total Time | PWM | Battery # |
|------|------|-------------|------------|-----|-----------|
| 04A1... | Battery 1 | 5 | 7200 | 21 | 1 |
| 04D1... | Battery 2 | 3 | 6312 | 64 | 2 |

### Clearing the Sheet:
When you do "Reset all batteries", it sends a GET request to:
```
https://your-script-url.com?action=clear
```

And the script deletes everything except the headers.

---

## NetworkTables - Talking to Your Robot

Every 5 seconds, the ESP32 tells the RoboRIO:

**Current Battery Info:**
- `/BatteryManager/CurrentBattery/Name` = "Battery 1"
- `/BatteryManager/CurrentBattery/BatteryNumber` = 1 (NEW!)
- `/BatteryManager/CurrentBattery/PWMValue` = 21 (NEW!)
- `/BatteryManager/CurrentBattery/UsageCount` = 5
- `/BatteryManager/CurrentBattery/TotalTimeSeconds` = 7200
- `/BatteryManager/CurrentBattery/TotalTimeFormatted` = "2h 0m 0s"
- `/BatteryManager/CurrentBattery/SessionTimeSeconds` = 450
- `/BatteryManager/BatteryInstalled` = true

**General Stats:**
- `/BatteryManager/TotalBatteries` = 6
- `/BatteryManager/MaxBatteries` = 6 (NEW!)
- `/BatteryManager/MostUsedBattery` = "Battery 3"
- `/BatteryManager/MostUsedCount` = 12

Your robot code can read these values and do whatever you want with them.

---

## The RoboRIO Java Code

On the RoboRIO side, you have a simple BatteryManagementSystem class that:

**Reads the PWM Signal:**
```java
double dutyCycle = dutyCycle.getOutput();      // Get duty cycle (0.0 to 1.0)
pwmValue = (int)(dutyCycle * 255.0);           // Convert to 0-255
currentBatteryNumber = identifyBattery(pwmValue);  // Figure out which battery
```

**Identifies the Battery:**
```java
if (pwmValue is 21±10) return 1;  // Battery 1
if (pwmValue is 64±10) return 2;  // Battery 2
if (pwmValue is 106±10) return 3;  // Battery 3
// etc.
```

**Publishes to SmartDashboard:**
```java
SmartDashboard.putString("Battery/Name", "Battery 3");
SmartDashboard.putNumber("Battery/Number", 3);
SmartDashboard.putNumber("Battery/PWM Value", 106);
SmartDashboard.putBoolean("Battery/Installed", true);
```

**Provides Health Warnings:**
```java
int health = getBatteryHealthLevel();
// 0 = good (< 20 uses)
// 1 = warning (20-39 uses)
// 2 = critical (40+ uses)

String warning = getHealthWarning();
// "⚠️ Battery 3 has high usage"
// "🔴 Battery 3 needs replacement soon!"
```

**Usage in Robot Code:**
```java
// In Robot.java
private BatteryManagementSystem batterySystem;

public void robotInit() {
  batterySystem = new BatteryManagementSystem(0);  // DIO port 0
}

public void robotPeriodic() {
  batterySystem.periodic();  // Update battery status
  batterySystem.checkBatteryStatus();  // Auto-detect changes
}

// In a command
if (batterySystem.getBatteryNumber() == 3) {
  System.out.println("Battery 3 is installed!");
}
```

---

## Using the Built-in LED

The ESP32-WROOM-DA has a built-in LED on GPIO 2 that blinks when you scan a battery.

**What happens:**
```cpp
void installBattery(int index) {
  digitalWrite(LED_PIN, HIGH);  // Turn on LED
  
  // Remove old battery, install new one, send PWM signal...
  
  delay(500);                   // Keep LED on for half second
  digitalWrite(LED_PIN, LOW);   // Turn off LED
}
```

**What you'll see:**
- Slide battery in
- LED lights up for half a second
- LED turns off
- Quick visual confirmation that the scan worked

---

## Mounting It on the Robot

**The Setup:**
1. Mount the ESP32 plus NFC reader near the battery compartment
2. Position the NFC reader so it's RIGHT next to where the battery sits
3. Put an NFC sticker on each of your 6 batteries
4. Run one wire from ESP32 pin 5 to RoboRIO DIO 0
5. Connect grounds together
6. When you slide the battery in, the tag automatically gets close enough to scan

**What Happens Automatically:**
- Battery slides in, NFC reader detects it instantly
- ESP32 identifies which battery it is (1-6)
- Old battery (if any) gets auto-removed from tracking
- New battery starts tracking immediately
- PWM signal sent to RoboRIO
- RoboRIO instantly knows which battery is installed
- Data uploads to Google Sheets
- Robot code sees the new battery info via NetworkTables

**The Beauty of This:**
No buttons to press, no commands to type. Just swap batteries and the system handles everything. You could literally be swapping batteries between matches and not even think about it. The robot will always know which battery is in.

**Mounting Tips:**
- Keep the NFC reader within about 4cm of where the battery will be
- Make sure the battery doesn't block WiFi antenna
- Route the PWM wire cleanly to the RoboRIO
- Secure everything so it doesn't rattle loose during matches
- The built-in LED will blink when a scan happens

**One Consideration:**
The battery removal issue becomes less of a problem because you're almost always putting a fresh battery in when you take one out. The system will automatically "remove" the old one when it detects the new one.

---

## How Battery History Works

**The ESP32 has a "database" in its flash memory** that stores info about each of your 6 batteries. When you scan a battery, here's what happens:

### When You First Register a Battery (Setup Mode):
```cpp
batteries[0].uid = "04A1B2C3D4E5F6";  // The tag's unique ID
batteries[0].name = "Battery 1";
batteries[0].usageCount = 0;          // Never used yet
batteries[0].totalTime = 0;           // 0 seconds of runtime
batteries[0].pwmValue = 21;           // Gets PWM 21 (first battery)
saveBatteryData();  // Save to flash memory
```

This creates a "profile" for that battery that lives on the ESP32 forever (until you reset it).

### When You Scan That Battery Later:
```cpp
// ESP32 reads the NFC tag: "04A1B2C3D4E5F6"
// Searches through its 6 battery slots...
for (int i = 0; i < numBatteries; i++) {
  if (batteries[i].uid == "04A1B2C3D4E5F6") {
    // Found it! This is Battery 1
    // It already knows:
    //   - usageCount = 5 (been used 5 times before)
    //   - totalTime = 7200 (ran for 2 hours total)
    //   - pwmValue = 21 (always sends PWM 21 for this battery)
    installBattery(i);  // Install it and send PWM signal
  }
}
```

### The Data Flow:

**First Time (Registration):**
1. You scan the battery in setup mode
2. ESP32 saves: UID = "04A1B2C3D4E5F6", pwmValue = 21, totalTime = 0
3. Data lives in flash memory
4. Battery is now "Battery 1" forever

**Every Time After:**
1. You scan the battery
2. ESP32 looks up that UID in its database
3. Finds it's Battery 1 with PWM 21
4. Sends PWM signal of 21 to RoboRIO
5. Adds new runtime to the existing totalTime
6. Saves updated data back to flash memory

**Example Timeline:**
- Day 1: Register Battery 1, use it for 15 minutes, totalTime = 900 seconds, PWM always 21
- Power off overnight, data saved in flash
- Day 2: Scan Battery 1 again, ESP32 says "Battery 1, sending PWM 21"
- Use it for 10 more minutes, totalTime = 1500 seconds
- Day 3: Scan Battery 1, starts at 1500 seconds, still sends PWM 21

### The Key Points:

**The NFC tag is just an ID card.** It doesn't store any data itself - it just says "I'm battery 04A1B2C3D4E5F6". 

**All the actual data** (runtime, usage count, name, PWM value) lives on the ESP32 in flash memory, indexed by that UID.

**The PWM value is permanent.** Battery 1 will always send PWM 21, Battery 2 will always send 64, etc. Even if you reset the battery's stats, it keeps its PWM assignment.

Think of it like a library card system:
- The NFC tag = your library card (just has your ID number)
- The ESP32 = the library's database (has your entire checkout history)
- When you scan your card, the library looks up your history
- Your library card number never changes, just like the battery's PWM value

---

## Things That Might Trip You Up

### Battery Removal Issue
**The Big One:** If you pull out a battery without scanning a new one, the system thinks it's still in there and keeps counting time AND keeps sending the PWM signal. You need to either:
- Scan a different battery
- Restart the ESP32
- (Future feature: add a "REMOVE" command)

### The 6-Battery Limit
You can't register more than 6 batteries. If you try, it'll tell you:
```
ERROR: Already have 6 batteries registered!
Use option 4 to remove batteries first.
```

If you need to add a 7th battery, you have to remove one first.

### PWM Value Conflicts
Don't try to manually set PWM values or swap battery positions. The system automatically assigns PWM based on slot:
- Slot 0 (Battery 1) = PWM 21
- Slot 1 (Battery 2) = PWM 64
- Etc.

If you remove Battery 2 and add a new battery, the new battery becomes Battery 2 with PWM 64.

### Google Sheets Weirdness
Sometimes Google responds with HTTP 400 even though the data uploaded fine. That's just Google being Google. As long as you see the data in your sheet (including PWM values), ignore the error.

### WiFi Range
If the ESP32 is too far from the router, uploads might fail but local tracking and PWM signaling still work. Check your WiFi signal.

### NFC Reading Distance
You gotta get the battery pretty close to the reader - like within 4cm. If it's not reading, move it closer.

### Power Loss During Timing
If you lose power while a battery is running:
- Current session time is lost
- Total time and usage count are safe (saved every 60 seconds)
- PWM signal obviously stops
- RoboRIO will show "No Battery" until you re-scan

### RoboRIO Not Detecting Battery
If the RoboRIO shows PWM = 0 or wrong battery:
- Check wiring: ESP32 pin 5 → RoboRIO DIO 0
- Verify ground connection
- Make sure ESP32 is actually scanning (check serial monitor)
- Check tolerance setting in RoboRIO code (default ±10)

---

## Real-World Example: A Day at Competition

**Morning:**
1. Plug in the ESP32
2. It loads your 6 registered batteries
3. Uploads current stats to Google Sheets
4. Connects to robot WiFi
5. PWM output initialized to 0 (no battery)

**Between Matches:**
1. Pull out the old battery, scan a fresh one (Battery 3)
2. System records: "Battery 3 ran for 3 minutes 42 seconds"
3. ESP32 sends PWM signal of 106
4. RoboRIO instantly knows Battery 3 is installed
5. Driver station shows "Battery 3" on dashboard
6. Uploads final data to Sheets
7. New battery starts tracking immediately

**During a Match:**
- Robot code can check: `if (batterySystem.getBatteryNumber() == 3)`
- Maybe reduce power if Battery 3 has high usage count
- Display warning on driver station if battery is critical

**End of Day:**
1. Check the web dashboard: "Battery 2 has been used 6 times today"
2. Open Google Sheets: See total runtime for all 6 batteries with PWM values
3. Robot code showed current battery on driver station all day
4. Know exactly which batteries are fresh and which are tired

**Next Event:**
1. All your data is still there from last time
2. All 6 batteries have their permanent PWM assignments
3. Keep tracking where you left off
4. Robot always knows which battery is which

---

## The Bottom Line

This system is pretty straightforward once you understand it:
1. NFC tags identify batteries (just like before)
2. ESP32 tracks time and usage (just like before)
3. **ESP32 sends unique PWM signal for each battery (NEW!)**
4. **RoboRIO reads PWM and knows which battery is installed (NEW!)**
5. Data gets saved locally AND uploaded to the cloud
6. You can see everything on a web page, robot dashboard, or in your robot code

The code might look intimidating at first, but it's really just:
- Scan a tag, figure out which battery it is (1-6)
- Send that battery's PWM signal to the RoboRIO
- Track time, add 1 every second
- Save data, every minute to flash, whenever something changes to Sheets
- Show data, web page, NetworkTables, and SmartDashboard

That's it. Everything else is just details to make those five things work reliably.

**The PWM system adds one simple step:** When you install a battery, the ESP32 not only tracks it but also tells the RoboRIO "Hey, this is Battery 3" by sending PWM signal 106. The robot can then make intelligent decisions based on which specific battery is installed.

This is honestly how the system was designed to be used - fully automatic tracking during battery swaps mounted right on the robot, with the robot always knowing exactly which battery it's running. Way better than manually scanning, entering data, or guessing which battery you installed.
