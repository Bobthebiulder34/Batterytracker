# FRC Battery Tracker - How It Actually Works

## What This Thing Does

Alright, so here's the deal: this system keeps track of your robot batteries. You know how batteries just kinda disappear or you forget which ones you've been using too much? This fixes that. You stick cheap NFC stickers on each battery, wave them near a reader, and boom - it tracks everything automatically.

---

## The Hardware Stuff You Need

Let's start with what you're actually building:

**The Brain:** ESP32 microcontroller board (like ten bucks on Amazon)

**The Scanner:** PN532 NFC reader module (another ten bucks or so)

**The Tags:** NFC stickers for your batteries (literally like 50 cents each)

**How to Hook It Up:**
```
PN532 SDA → ESP32 pin 21 (this is the data line)
PN532 SCL → ESP32 pin 22 (this is the clock line)
PN532 IRQ → ESP32 pin 2  (tells the ESP32 "hey I found something!")
PN532 RST → ESP32 pin 3  (resets the reader if things get weird)
Power everything with 3.3V (don't use 5V or you'll fry stuff)
```

That's it. Pretty simple wiring honestly.

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

---

## How Battery Data Gets Stored

Each battery has a little profile that looks like this:

```cpp
struct Battery {
  String uid;                  // The tag's unique ID (like a fingerprint)
  String name;                 // What you call it ("Battery 1", "Bob", whatever)
  int usageCount;              // How many times you've used it
  unsigned long totalTime;     // Total seconds it's been running
  unsigned long installTime;   // When you put it in (0 means it's chilling)
  bool syncedToSheets;         // Did we upload this yet?
};
```

The code can remember up to 20 batteries. That's stored in an array:

```cpp
Battery batteries[20];              // All your batteries
int numBatteries = 0;               // How many you've actually registered
String currentBatteryUID = "";      // Which one's in the robot right now
```

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
3 - Add new batteries        (register new ones)
4 - Remove and add batteries (clean up your list)
```

### Option 1: Reset Everything
Just nukes all your data. It'll ask you to type "YES" to make sure you're not being dumb. Then it wipes the ESP32's memory AND clears your Google Sheet.

### Option 2: Reset Some Batteries
Say you want to reset "Battery 3" and "Battery 5" but keep the others:
1. Choose option 2
2. Type "Battery 3" and hit enter
3. Type "Battery 5" and hit enter
4. Type "DONE"
5. It resets just those two

### Option 3: Adding New Batteries
This is the fun part:
1. Tell it how many batteries you want to add
2. Scan each battery's NFC tag when it tells you to
3. It auto-names them "Battery 1", "Battery 2", etc.
4. Type "DONE" when you're finished
5. Everything gets saved and uploaded

**Important:** It checks if a tag is already registered, so you can't accidentally add the same battery twice.

### Option 4: Remove Old, Add New
Perfect for when you retire some batteries:
1. Type the names of batteries you want to remove
2. Type "DONE"
3. Then it asks how many new ones you want to add
4. Follow the same process as option 3

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
      installBattery(i);  // Yep! Install it
      return;
    }
  }
  
  // Nope, never seen this tag before
  Serial.println("Unknown battery!");
}
```

### NetworkTables Updates
Every 5 seconds, it sends fresh data to the RoboRIO so your robot code always knows what's up.

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

### Step 3: Tell Everyone
```cpp
saveBatteryData();              // Save to flash (survives power loss)
updateBatteryOnSheets(index);   // Upload to Google Sheets
publishToNetworkTables();       // Tell the RoboRIO
```

And boom, you're done. The new battery is installed and being tracked.

---

## Saving Data - How It Survives Reboots

The ESP32 has flash memory that doesn't get erased when you unplug it. Think of it like a tiny hard drive.

### Saving:
```cpp
preferences.begin("batteries", false);  // Open the "batteries" storage area
preferences.putInt("count", numBatteries);  // Save how many batteries

// For each battery, save its info
for (int i = 0; i < numBatteries; i++) {
  String prefix = "bat" + String(i) + "_";  // Like "bat0_", "bat1_", etc.
  preferences.putString((prefix + "uid").c_str(), batteries[i].uid);
  preferences.putString((prefix + "name").c_str(), batteries[i].name);
  // ... and so on
}

preferences.end();  // Close it up
```

It creates keys like:
- `bat0_uid` = "04A1B2C3D4E5F6"
- `bat0_name` = "Battery 1"
- `bat0_usage` = 5
- `bat0_time` = 7200

### Loading:
Same thing in reverse. When the ESP32 boots up, it reads all that stuff back into memory.

---

## The Web Dashboard

You can visit the ESP32 in a browser (just type its IP address) and see a nice table:

| Battery | Tag ID | Times Used | Total Runtime | Status |
|---------|--------|------------|---------------|--------|
| Battery 1 | 04A1B2C3... | 5 | 2h 15m 30s | Available |
| Battery 2 | 04D1E2F3... | 3 | 1h 45m 12s | IN ROBOT |

The currently installed battery is highlighted in yellow. Pretty slick.

There's also a JSON API at `/api/batteries` if you want to build your own custom dashboard or app.

---

## Google Sheets Integration

This is where all your historical data lives.

### When Data Gets Uploaded:
- When you install a battery
- When you remove a battery (by installing a different one)
- When the system boots up
- When you reset batteries in setup mode

### What Gets Sent:
```json
{
  "battery_uuid": "04A1B2C3D4E5F6",
  "battery_usage": "Battery 1",
  "battery_usage_count": 5,
  "total_time_used": 7200,
  "total_percentage_used": 0
}
```

The Google Apps Script on the other end either:
- Updates an existing row if it finds a matching UUID
- Creates a new row if this is a new battery

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
- `/BatteryManager/CurrentBattery/UsageCount` = 5
- `/BatteryManager/CurrentBattery/TotalTimeSeconds` = 7200
- `/BatteryManager/CurrentBattery/TotalTimeFormatted` = "2h 0m 0s"
- `/BatteryManager/CurrentBattery/SessionTimeSeconds` = 450 (how long it's been in THIS time)
- `/BatteryManager/BatteryInstalled` = true

**General Stats:**
- `/BatteryManager/TotalBatteries` = 8
- `/BatteryManager/MostUsedBattery` = "Battery 3"
- `/BatteryManager/MostUsedCount` = 12

Your robot code can read these values and do whatever you want with them - display on the driver station, make decisions based on battery health, whatever.

---

## Helper Functions That Make Life Easier

### Converting Tag IDs to Strings:
```cpp
String getUIDString(uint8_t* uid, uint8_t uidLength) {
  // Takes raw bytes like [04, A1, B2, C3, D4, E5, F6]
  // Returns a nice string like "04A1B2C3D4E5F6"
}
```

### Making Time Look Nice:
```cpp
String formatTime(unsigned long seconds) {
  // 7200 seconds → "2h 0m 0s"
  // Way easier to read than "7200"
}
```

---

## Things That Might Trip You Up

### Battery Removal Issue
**The Big One:** If you pull out a battery without scanning a new one, the system thinks it's still in there and keeps counting time. You need to either:
- Scan a different battery
- Restart the ESP32
- (Future feature: add a "REMOVE" command)

### Google Sheets Weirdness
Sometimes Google responds with HTTP 400 even though the data uploaded fine. That's just Google being Google. As long as you see the data in your sheet, ignore the error.

### WiFi Range
If the ESP32 is too far from the router, uploads might fail but local tracking still works. Check your WiFi signal.

### NFC Reading Distance
You gotta get the battery pretty close to the reader - like within 4cm. If it's not reading, move it closer.

### Power Loss During Timing
If you lose power while a battery is running, the current session time is lost. But the total time and usage count are safe because they're saved every 60 seconds.

---

## Real-World Example: A Day at Competition

**Morning:**
1. Plug in the ESP32
2. It loads your 8 registered batteries
3. Uploads current stats to Google Sheets
4. Connects to robot WiFi

**Between Matches:**
1. Pull out the old battery, scan a fresh one
2. System records: "Battery 3 ran for 3 minutes 42 seconds"
3. Uploads final data to Sheets
4. New battery starts tracking immediately

**End of Day:**
1. Check the web dashboard: "Battery 2 has been used 6 times today"
2. Open Google Sheets: See total runtime for all batteries
3. Robot code shows current battery on driver station

**Next Event:**
1. All your data is still there from last time
2. Keep tracking where you left off
3. Know exactly which batteries are fresh and which are tired

---

## Mounting It on the Robot

**The Setup:**
1. Mount the ESP32 plus NFC reader near the battery compartment
2. Position the NFC reader so it's RIGHT next to where the battery sits
3. Put an NFC sticker on each battery
4. When you slide the battery in, the tag automatically gets close enough to scan

**What Happens Automatically:**
- Battery slides in, NFC reader detects it instantly
- Old battery (if any) gets auto-removed from tracking
- New battery starts tracking immediately
- Data uploads to Google Sheets
- Robot code sees the new battery info via NetworkTables

**The Beauty of This:**
No buttons to press, no commands to type. Just swap batteries and the system handles everything. You could literally be swapping batteries between matches and not even think about it.

**Mounting Tips:**
- Keep the NFC reader within about 4cm of where the battery will be
- Make sure the battery doesn't block WiFi antenna
- Secure everything so it doesn't rattle loose during matches
- The built-in LED will blink when a scan happens

**One Consideration:**
The battery removal issue becomes less of a problem because you're almost always putting a fresh battery in when you take one out. The system will automatically "remove" the old one when it detects the new one.

---

## Using the Built-in LED

The ESP32-WROOM-DA has a built-in LED on GPIO 2 that you can use to show when a scan happens.

**Add at the top with your other pin definitions:**
```cpp
#define LED_PIN 2  // Built-in LED on ESP32-WROOM-DA
```

**In your setup() function, add:**
```cpp
pinMode(LED_PIN, OUTPUT);
digitalWrite(LED_PIN, LOW);  // Start with LED off
```

**Modify the installBattery() function:**
```cpp
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
```

**What you'll see:**
- Slide battery in, LED lights up for half a second, turns off
- Quick visual confirmation that the scan worked
- No extra hardware needed

---

## How Battery History Works

**The ESP32 has a "database" in its flash memory** that stores info about every battery you've registered. When you scan a battery, here's what happens:

### When You First Register a Battery (Setup Mode):
```cpp
batteries[numBatteries].uid = "04A1B2C3D4E5F6";  // The tag's unique ID
batteries[numBatteries].name = "Battery 1";
batteries[numBatteries].usageCount = 0;          // Never used yet
batteries[numBatteries].totalTime = 0;           // 0 seconds of runtime
saveBatteryData();  // Save to flash memory
```

This creates a "profile" for that battery that lives on the ESP32 forever (until you reset it).

### When You Scan That Battery Later:
```cpp
// ESP32 reads the NFC tag: "04A1B2C3D4E5F6"
// Searches through its database...
for (int i = 0; i < numBatteries; i++) {
  if (batteries[i].uid == "04A1B2C3D4E5F6") {
    // Found it! This is Battery 1
    // It already knows:
    //   - usageCount = 5 (been used 5 times before)
    //   - totalTime = 7200 (ran for 2 hours total in past uses)
    installBattery(i);  // Install it and start adding to that time
  }
}
```

### The Data Flow:

**First Time (Registration):**
1. You scan the battery in setup mode
2. ESP32 saves: UID to "04A1B2C3D4E5F6", totalTime to 0
3. Data lives in flash memory

**Every Time After:**
1. You scan the battery
2. ESP32 looks up that UID in its database
3. Finds the existing record with all the history
4. Adds new runtime to the existing totalTime
5. Saves updated data back to flash memory

**Example Timeline:**
- Day 1: Register Battery 1, use it for 15 minutes, totalTime equals 900 seconds
- Power off overnight, data saved in flash
- Day 2: Scan Battery 1 again, ESP32 says "Oh this is Battery 1, it has 900 seconds already"
- Use it for 10 more minutes, totalTime equals 1500 seconds
- Day 3: Scan Battery 1, starts at 1500 seconds and keeps counting

### The Key Point:

**The NFC tag is just an ID card.** It doesn't store any data itself - it just says "I'm battery 04A1B2C3D4E5F6". 

**All the actual data** (runtime, usage count, name) lives on the ESP32 in flash memory, indexed by that UID.

Think of it like a library card system:
- The NFC tag equals your library card (just has your ID number)
- The ESP32 equals the library's database (has your entire checkout history)
- When you scan your card, the library looks up your history

---

## The Bottom Line

This system is pretty straightforward once you understand it:
1. NFC tags identify batteries
2. ESP32 tracks time and usage
3. Data gets saved locally AND uploaded to the cloud
4. You can see everything on a web page or in your robot code

The code might look intimidating at first, but it's really just:
- Scan a tag, figure out which battery it is
- Track time, add 1 every second
- Save data, every minute to flash, whenever something changes to Sheets
- Show data, web page and NetworkTables

That's it. Everything else is just details to make those four things work reliably.

This is honestly how the system was designed to be used - fully automatic tracking during battery swaps mounted right on the robot. Way better than manually scanning or entering data.