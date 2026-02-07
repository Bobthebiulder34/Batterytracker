// Robot.java - Team 4546
// Example showing how to use the single-file BatteryManagementSystem

package frc.robot;

import edu.wpi.first.wpilibj.TimedRobot;
import edu.wpi.first.wpilibj2.command.Command;
import edu.wpi.first.wpilibj2.command.CommandScheduler;

public class Robot extends TimedRobot {
    private Command m_autonomousCommand;
    private RobotContainer m_robotContainer;
    
    // Battery Management System - All in one file!
    private BatteryManagementSystem batterySystem;
    
    @Override
    public void robotInit() {
        // Initialize robot container
        m_robotContainer = new RobotContainer();
        
        // Initialize Battery Management System on DIO port 0
        // Change the number if you wire ESP32 PWM to a different DIO port
        batterySystem = new BatteryManagementSystem(0);
        
        System.out.println("[Robot] Initialization complete");
        System.out.println("[Robot] Battery Management System ready on DIO 0");
    }

    @Override
    public void robotPeriodic() {
        // Run the command scheduler
        CommandScheduler.getInstance().run();
        
        // Update battery system (reads PWM, updates dashboard)
        batterySystem.periodic();
        
        // Check for battery changes (optional - provides auto-logging)
        batterySystem.checkBatteryStatus();
    }

    @Override
    public void disabledInit() {
        // Log battery status when entering disabled mode
        batterySystem.logDisabledStart();
    }

    @Override
    public void disabledPeriodic() {
    }

    @Override
    public void autonomousInit() {
        m_autonomousCommand = m_robotContainer.getAutonomousCommand();

        if (m_autonomousCommand != null) {
            m_autonomousCommand.schedule();
        }
        
        // Log which battery is being used for auto
        batterySystem.logAutonomousStart();
    }

    @Override
    public void autonomousPeriodic() {
    }

    @Override
    public void teleopInit() {
        // Cancel auto command if running
        if (m_autonomousCommand != null) {
            m_autonomousCommand.cancel();
        }
        
        // Log which battery is being used for teleop
        batterySystem.logTeleopStart();
    }

    @Override
    public void teleopPeriodic() {
    }

    @Override
    public void testInit() {
        // Cancel all running commands at the start of test mode
        CommandScheduler.getInstance().cancelAll();
        
        // Test battery detection
        System.out.println("\n[Test Mode] Battery Detection Test");
        batterySystem.printStatus();
    }

    @Override
    public void testPeriodic() {
    }

    @Override
    public void simulationInit() {
    }

    @Override
    public void simulationPeriodic() {
    }
    
    // ==================== PUBLIC ACCESS ====================
    
    /**
     * Get the battery management system
     * Use this in commands/subsystems that need battery info
     * @return The BatteryManagementSystem
     */
    public BatteryManagementSystem getBatterySystem() {
        return batterySystem;
    }
}
