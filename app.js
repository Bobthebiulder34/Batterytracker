// Your specific Google Sheet ID
const SHEET_ID = "INSERT_YOUR_SHEET_ID_HERE";
const SHEET_NAME = "Batteries";

// Main function that handles POST requests from ESP32
function doPost(e) {
  try {
    const spreadsheet = SpreadsheetApp.openById(SHEET_ID);
    let sheet = spreadsheet.getSheetByName(SHEET_NAME);
    
    // Create sheet if it doesn't exist
    if (!sheet) {
      sheet = spreadsheet.insertSheet(SHEET_NAME);
    }
    
    // Parse the data sent from ESP32
    const data = JSON.parse(e.postData.contents);
    
    // Initialize sheet with headers if empty
    if (sheet.getLastRow() === 0) {
      const headers = ["Battery Name", "NFC Tag ID", "Times Used", "Total Time (seconds)", "Total Time (formatted)", "Last Updated"];
      sheet.appendRow(headers);
    }
    
    // Find if this battery already exists
    const range = sheet.getDataRange();
    const values = range.getValues();
    let foundRow = -1;
    
    for (let i = 1; i < values.length; i++) {
      if (values[i][0] === data.name) {
        foundRow = i + 1;  // Google Sheets uses 1-based indexing
        break;
      }
    }
    
    const timestamp = new Date().toLocaleString();
    
    if (foundRow > 0) {
      // Update existing battery
      sheet.getRange(foundRow, 1, 1, 6).setValues([[
        data.name,
        data.uid,
        data.usageCount,
        data.totalTime,
        data.totalTimeFormatted,
        timestamp
      ]]);
    } else {
      // Add new battery
      sheet.appendRow([
        data.name,
        data.uid,
        data.usageCount,
        data.totalTime,
        data.totalTimeFormatted,
        timestamp
      ]);
    }
    
    return ContentService.createTextOutput(JSON.stringify({
      status: "success",
      message: "Battery data updated"
    })).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    return ContentService.createTextOutput(JSON.stringify({
      status: "error",
      message: error.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

// Function to clear all battery data (called from sheet manually or ESP32)
function doGet(e) {
  if (e.parameter.action === "clear") {
    const spreadsheet = SpreadsheetApp.openById(SHEET_ID);
    let sheet = spreadsheet.getSheetByName(SHEET_NAME);
    
    if (sheet) {
      const lastRow = sheet.getLastRow();
      if (lastRow > 1) {
        sheet.deleteRows(2, lastRow - 1);  // Keep headers
      }
    }
    return ContentService.createTextOutput("Sheet cleared").setMimeType(ContentService.MimeType.TEXT);
  }
  
  return ContentService.createTextOutput("Apps Script is ready").setMimeType(ContentService.MimeType.TEXT);
}
