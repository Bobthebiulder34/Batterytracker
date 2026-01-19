// Google Apps Script for ESP32 Battery Data Logger
// Deploy this as a Web App to receive data from your ESP32

function doPost(e) {
  try {
    // Get the active spreadsheet (or specify by ID)
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    
    // If you want to use a specific sheet, uncomment and modify:
    // var sheet = SpreadsheetApp.openById('YOUR_SHEET_ID').getSheetByName('Sheet1');
    
    // Parse the incoming JSON data from ESP32
    var data = JSON.parse(e.postData.contents);
    
    // Extract battery data
    var batteryUUID = data.battery_uuid || '';
    var batteryUsage = data.battery_usage || '';
    var batteryUsageCount = data.battery_usage_count || 0;
    var totalTimeUsed = data.total_time_used || 0;
    var totalPercentageUsed = data.total_percentage_used || 0;
    var timestamp = new Date();
    
    // Add headers if this is the first row
    if (sheet.getLastRow() === 0) {
      sheet.appendRow([
        'Timestamp',
        'Battery UUID',
        'Battery Usage',
        'Battery Usage Count',
        'Total Time Used',
        'Total Percentage Used'
      ]);
    }
    
    // Check if this battery UUID already exists
    var dataRange = sheet.getDataRange();
    var values = dataRange.getValues();
    var batteryFound = false;
    var rowToUpdate = -1;
    
    // Search for existing battery UUID (skip header row)
    for (var i = 1; i < values.length; i++) {
      if (values[i][1] === batteryUUID) {  // Column B (index 1) is Battery UUID
        batteryFound = true;
        rowToUpdate = i + 1;  // +1 because sheet rows are 1-indexed
        break;
      }
    }
    
    if (batteryFound) {
      // Update existing row with new data
      sheet.getRange(rowToUpdate, 1).setValue(timestamp);
      sheet.getRange(rowToUpdate, 3).setValue(batteryUsage);
      sheet.getRange(rowToUpdate, 4).setValue(batteryUsageCount);
      sheet.getRange(rowToUpdate, 5).setValue(totalTimeUsed);
      sheet.getRange(rowToUpdate, 6).setValue(totalPercentageUsed);
    } else {
      // Append new battery data
      sheet.appendRow([
        timestamp,
        batteryUUID,
        batteryUsage,
        batteryUsageCount,
        totalTimeUsed,
        totalPercentageUsed
      ]);
    }
    
    // Return success response
    return ContentService.createTextOutput(JSON.stringify({
      'status': 'success',
      'message': 'Data logged successfully',
      'action': batteryFound ? 'updated' : 'created'
    })).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    // Return error response
    return ContentService.createTextOutput(JSON.stringify({
      'status': 'error',
      'message': error.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

function doGet(e) {
  // Optional: Handle GET requests for testing
  return ContentService.createTextOutput(JSON.stringify({
    'status': 'online',
    'message': 'Battery Logger API is running'
  })).setMimeType(ContentService.MimeType.JSON);
}