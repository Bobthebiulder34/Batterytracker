// BatteryManagementSystem.java - Team 4546
// Complete battery management system in a single file
// This file contains the BatteryManager subsystem and all necessary constants
// 
// Usage in your Robot.java:
//   private BatteryManagementSystem batterySystem;
//   
//   @Override
//   public void robotInit() {
//       batterySystem = new BatteryManagementSystem(0);  // DIO port 0
//   }
//
//   @Override
//   public void robotPeriodic() {
//       batterySystem.periodic();
//       batterySystem.checkBatteryStatus();  // Optional: auto-detect changes
//   }

package frc.robot;

import edu.wpi.first.wpilibj.DigitalInput;
import edu.wpi.first.wpilibj.DutyCycle;
import edu.wpi.first.wpilibj.Timer;
import edu.wpi.first.networktables.NetworkTable;
import edu.wpi.first.networktables.NetworkTableEntry;
import edu.wpi.first.networktables.NetworkTableInstance;
import edu.wpi.first.wpilibj.smartdashboard.SmartDashboard;

public class BatteryManagementSystem {
    
    // ==================== CONSTANTS ====================
    
    // PWM signal values for each battery (with tolerance)
    // The ESP32 sends these values: 21, 64, 106, 148, 191, 233
    private static final int TOLERANCE = 10;  // ±10 PWM units tolerance
    
    private static final class BatteryPWM {
        static final int BATTERY_1 = 21;
        static final int BATTERY_2 = 64;
        static final int BATTERY_3 = 106;
        static final int BATTERY_4 = 148;
        static final int BATTERY_5 = 191;
        static final int BATTERY_6 = 233;
        static final int NO_BATTERY = 0;
    }
    
    // Battery health thresholds (usage count)
    private static final int USAGE_WARNING_THRESHOLD = 20;  // Yellow warning
    private static final int USAGE_CRITICAL_THRESHOLD = 40; // Red warning
    
    // Battery status check interval (seconds)
    private static final double STATUS_CHECK_INTERVAL = 2.0;
    
    // Total number of batteries in the system
    private static final int MAX_BATTERIES = 6;
    
    // Enable/disable features
    private static final boolean ENABLE_HEALTH_WARNINGS = true;
    private static final boolean ENABLE_BATTERY_LOGGING = true;
    
    // NetworkTables paths
    private static final String BATTERY_TABLE = "BatteryManager";
    private static final String CURRENT_BATTERY_NAME = "CurrentBattery/Name";
    private static final String USAGE_COUNT = "CurrentBattery/UsageCount";
    private static final String TOTAL_TIME = "CurrentBattery/TotalTimeFormatted";
    private static final String SESSION_TIME = "CurrentBattery/SessionTimeFormatted";
    private static final String BATTERY_INSTALLED = "BatteryInstalled";
    
    // ==================== HARDWARE ====================
    
    private final DigitalInput pwmInput;
    private final DutyCycle dutyCycle;
    
    // ==================== NETWORKTABLES ====================
    
    private final NetworkTable batteryTable;
    private final NetworkTableEntry currentBatteryEntry;
    private final NetworkTableEntry usageCountEntry;
    private final NetworkTableEntry totalTimeEntry;
    private final NetworkTableEntry sessionTimeEntry;
    private final NetworkTableEntry batteryInstalledEntry;
    
    // ==================== STATE TRACKING ====================
    
    private int currentBatteryNumber;
    private String currentBatteryName;
    private int pwmValue;
    private boolean batteryInstalled;
    
    // Battery change detection
    private Timer batteryCheckTimer;
    private boolean lastBatteryState;
    private int lastBatteryNumber;
    
    // ==================== CONSTRUCTOR ====================
    
    /**
     * Creates a new BatteryManagementSystem
     * @param pwmChannel DIO channel to read PWM signal from ESP32 (0-9)
     */
    public BatteryManagementSystem(int pwmChannel) {
        // Initialize PWM input
        pwmInput = new DigitalInput(pwmChannel);
        dutyCycle = new DutyCycle(pwmInput);
        
        // Initialize NetworkTables
        batteryTable = NetworkTableInstance.getDefault().getTable(BATTERY_TABLE);
        currentBatteryEntry = batteryTable.getEntry(CURRENT_BATTERY_NAME);
        usageCountEntry = batteryTable.getEntry(USAGE_COUNT);
        totalTimeEntry = batteryTable.getEntry(TOTAL_TIME);
        sessionTimeEntry = batteryTable.getEntry(SESSION_TIME);
        batteryInstalledEntry = batteryTable.getEntry(BATTERY_INSTALLED);
        
        // Initialize state
        currentBatteryNumber = 0;
        currentBatteryName = "No Battery";
        pwmValue = 0;
        batteryInstalled = false;
        
        // Initialize battery change detection
        batteryCheckTimer = new Timer();
        batteryCheckTimer.start();
        lastBatteryState = false;
        lastBatteryNumber = 0;
        
        if (ENABLE_BATTERY_LOGGING) {
            System.out.println("[BatteryManager] Initialized on DIO " + pwmChannel);
            System.out.println("[BatteryManager] PWM Value Mapping:");
            System.out.println("  Battery 1 → PWM ~21");
            System.out.println("  Battery 2 → PWM ~64");
            System.out.println("  Battery 3 → PWM ~106");
            System.out.println("  Battery 4 → PWM ~148");
            System.out.println("  Battery 5 → PWM ~191");
            System.out.println("  Battery 6 → PWM ~233");
        }
    }
    
    // ==================== PERIODIC UPDATE ====================
    
    /**
     * Call this in robotPeriodic() to update battery status
     */
    public void periodic() {
        // Read PWM signal from ESP32
        readPWMSignal();
        
        // Update SmartDashboard
        updateDashboard();
    }
    
    /**
     * Reads the PWM signal and determines which battery is installed
     */
    private void readPWMSignal() {
        // Get duty cycle (0.0 to 1.0)
        double dutyCycleValue = dutyCycle.getOutput();
        
        // Convert to 0-255 scale (matching ESP32 analogWrite scale)
        pwmValue = (int)(dutyCycleValue * 255.0);
        
        // Determine which battery based on PWM value
        int previousBatteryNumber = currentBatteryNumber;
        currentBatteryNumber = identifyBattery(pwmValue);
        
        // Update battery name
        if (currentBatteryNumber > 0) {
            currentBatteryName = currentBatteryEntry.getString("Battery " + currentBatteryNumber);
            batteryInstalled = batteryInstalledEntry.getBoolean(true);
        } else {
            currentBatteryName = "No Battery";
            batteryInstalled = false;
        }
        
        // Log battery changes
        if (ENABLE_BATTERY_LOGGING && currentBatteryNumber != previousBatteryNumber) {
            if (currentBatteryNumber > 0) {
                System.out.println("[BatteryManager] Battery detected: " + currentBatteryName + 
                                 " (PWM: " + pwmValue + ")");
            } else {
                System.out.println("[BatteryManager] No battery detected (PWM: " + pwmValue + ")");
            }
        }
    }
    
    /**
     * Identifies which battery is installed based on PWM value
     * @param pwm The PWM value (0-255)
     * @return Battery number (1-6) or 0 if no battery
     */
    private int identifyBattery(int pwm) {
        // Check if PWM is within tolerance of each battery's value
        if (isWithinTolerance(pwm, BatteryPWM.BATTERY_1)) {
            return 1;
        } else if (isWithinTolerance(pwm, BatteryPWM.BATTERY_2)) {
            return 2;
        } else if (isWithinTolerance(pwm, BatteryPWM.BATTERY_3)) {
            return 3;
        } else if (isWithinTolerance(pwm, BatteryPWM.BATTERY_4)) {
            return 4;
        } else if (isWithinTolerance(pwm, BatteryPWM.BATTERY_5)) {
            return 5;
        } else if (isWithinTolerance(pwm, BatteryPWM.BATTERY_6)) {
            return 6;
        } else {
            return 0;  // No battery or invalid signal
        }
    }
    
    /**
     * Checks if a value is within tolerance of a target
     * @param value The measured value
     * @param target The target value
     * @return true if within tolerance
     */
    private boolean isWithinTolerance(int value, int target) {
        return Math.abs(value - target) <= TOLERANCE;
    }
    
    /**
     * Updates SmartDashboard with current battery information
     */
    private void updateDashboard() {
        SmartDashboard.putString("Battery/Name", currentBatteryName);
        SmartDashboard.putNumber("Battery/Number", currentBatteryNumber);
        SmartDashboard.putNumber("Battery/PWM Value", pwmValue);
        SmartDashboard.putBoolean("Battery/Installed", batteryInstalled);
        
        // Get additional info from NetworkTables if available
        if (batteryInstalled) {
            int usageCount = (int)usageCountEntry.getDouble(0);
            String totalTime = totalTimeEntry.getString("0h 0m 0s");
            String sessionTime = sessionTimeEntry.getString("0h 0m 0s");
            
            SmartDashboard.putNumber("Battery/Usage Count", usageCount);
            SmartDashboard.putString("Battery/Total Runtime", totalTime);
            SmartDashboard.putString("Battery/Session Runtime", sessionTime);
            SmartDashboard.putNumber("Battery/Health Level", getBatteryHealthLevel());
            SmartDashboard.putString("Battery/Warning", getHealthWarning());
        }
    }
    
    // ==================== BATTERY CHANGE DETECTION ====================
    
    /**
     * Call this in robotPeriodic() to automatically detect and log battery changes
     * This is optional - only use if you want automatic console logging
     */
    public void checkBatteryStatus() {
        if (!batteryCheckTimer.hasElapsed(STATUS_CHECK_INTERVAL)) {
            return;
        }
        
        batteryCheckTimer.reset();
        
        boolean currentBatteryState = isBatteryInstalled();
        int currentBattNum = getBatteryNumber();
        
        if (!ENABLE_BATTERY_LOGGING) {
            lastBatteryState = currentBatteryState;
            lastBatteryNumber = currentBattNum;
            return;
        }
        
        // Battery was just installed
        if (currentBatteryState && !lastBatteryState) {
            System.out.println("\n✓ Battery installed: " + getBatteryName());
            System.out.println("  Usage count: " + getUsageCount());
            System.out.println("  Total runtime: " + getTotalRuntime());
            if (ENABLE_HEALTH_WARNINGS) {
                String warning = getHealthWarning();
                if (!warning.isEmpty()) {
                    System.out.println("  " + warning);
                }
            }
        }
        // Battery was just removed
        else if (!currentBatteryState && lastBatteryState) {
            System.out.println("\n⚠️  Battery removed!");
        }
        // Different battery installed
        else if (currentBatteryState && currentBattNum != lastBatteryNumber && lastBatteryNumber != 0) {
            System.out.println("\n🔄 Battery changed: Battery " + lastBatteryNumber + 
                             " → " + getBatteryName());
            if (ENABLE_HEALTH_WARNINGS) {
                String warning = getHealthWarning();
                if (!warning.isEmpty()) {
                    System.out.println("  " + warning);
                }
            }
        }
        
        lastBatteryState = currentBatteryState;
        lastBatteryNumber = currentBattNum;
    }
    
    // ==================== GETTERS ====================
    
    /**
     * Gets the current battery number (1-6)
     * @return Battery number, or 0 if no battery
     */
    public int getBatteryNumber() {
        return currentBatteryNumber;
    }
    
    /**
     * Gets the current battery name
     * @return Battery name (e.g., "Battery 1")
     */
    public String getBatteryName() {
        return currentBatteryName;
    }
    
    /**
     * Gets the raw PWM value being received
     * @return PWM value (0-255)
     */
    public int getPWMValue() {
        return pwmValue;
    }
    
    /**
     * Checks if a battery is installed
     * @return true if battery detected
     */
    public boolean isBatteryInstalled() {
        return batteryInstalled && currentBatteryNumber > 0;
    }
    
    /**
     * Gets the battery usage count from NetworkTables
     * @return Number of times this battery has been used
     */
    public int getUsageCount() {
        return (int)usageCountEntry.getDouble(0);
    }
    
    /**
     * Gets the total runtime string from NetworkTables
     * @return Formatted time string (e.g., "2h 15m 30s")
     */
    public String getTotalRuntime() {
        return totalTimeEntry.getString("0h 0m 0s");
    }
    
    /**
     * Gets the session runtime string from NetworkTables
     * @return Formatted time string (e.g., "0h 5m 12s")
     */
    public String getSessionRuntime() {
        return sessionTimeEntry.getString("0h 0m 0s");
    }
    
    /**
     * Checks if a specific battery number is currently installed
     * @param batteryNum Battery number to check (1-6)
     * @return true if that battery is installed
     */
    public boolean isBatteryInstalled(int batteryNum) {
        return currentBatteryNumber == batteryNum && batteryInstalled;
    }
    
    // ==================== BATTERY HEALTH ====================
    
    /**
     * Gets a health warning level based on usage count
     * @return 0 = good, 1 = warning, 2 = critical
     */
    public int getBatteryHealthLevel() {
        int usage = getUsageCount();
        if (usage < USAGE_WARNING_THRESHOLD) {
            return 0;  // Good
        } else if (usage < USAGE_CRITICAL_THRESHOLD) {
            return 1;  // Warning - battery getting old
        } else {
            return 2;  // Critical - battery should be replaced soon
        }
    }
    
    /**
     * Gets a warning message based on battery health
     * @return Warning message or empty string if battery is healthy
     */
    public String getHealthWarning() {
        if (!ENABLE_HEALTH_WARNINGS) {
            return "";
        }
        
        int health = getBatteryHealthLevel();
        switch (health) {
            case 1:
                return "⚠️ Battery " + currentBatteryNumber + " has high usage";
            case 2:
                return "🔴 Battery " + currentBatteryNumber + " needs replacement soon!";
            default:
                return "";
        }
    }
    
    // ==================== UTILITY METHODS ====================
    
    /**
     * Prints current battery status to console
     */
    public void printStatus() {
        System.out.println("========== Battery Status ==========");
        System.out.println("Battery: " + currentBatteryName);
        System.out.println("Number: " + currentBatteryNumber);
        System.out.println("PWM Value: " + pwmValue);
        System.out.println("Installed: " + batteryInstalled);
        if (batteryInstalled) {
            System.out.println("Usage Count: " + getUsageCount());
            System.out.println("Total Runtime: " + getTotalRuntime());
            System.out.println("Session Runtime: " + getSessionRuntime());
            System.out.println("Health Level: " + getBatteryHealthLevel());
            String warning = getHealthWarning();
            if (!warning.isEmpty()) {
                System.out.println("Warning: " + warning);
            }
        }
        System.out.println("===================================");
    }
    
    /**
     * Logs battery info when entering autonomous mode
     */
    public void logAutonomousStart() {
        if (!ENABLE_BATTERY_LOGGING) return;
        
        System.out.println("[Auto] Starting with " + getBatteryName());
        if (ENABLE_HEALTH_WARNINGS) {
            String warning = getHealthWarning();
            if (!warning.isEmpty()) {
                System.out.println("[Auto] " + warning);
            }
        }
    }
    
    /**
     * Logs battery info when entering teleop mode
     */
    public void logTeleopStart() {
        if (!ENABLE_BATTERY_LOGGING) return;
        
        System.out.println("[Teleop] Starting with " + getBatteryName());
        if (ENABLE_HEALTH_WARNINGS) {
            String warning = getHealthWarning();
            if (!warning.isEmpty()) {
                System.out.println("[Teleop] " + warning);
            }
        }
    }
    
    /**
     * Logs battery info when entering disabled mode
     */
    public void logDisabledStart() {
        if (!ENABLE_BATTERY_LOGGING) return;
        
        System.out.println("\n[Robot] Entering disabled mode");
        printStatus();
    }
}
