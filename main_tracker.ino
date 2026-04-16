#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <DHT.h>

// --- Configuration ---
const char* ssid = "iPhone";         // Apna WiFi SSID dalein
const char* password = "11111111"; // Apna WiFi Password dalein

// --- Pin Definitions ---
#define DHTPIN 15
#define DHTTYPE DHT11
#define GPS_RX 4
#define GPS_TX 5
#define FP_RX 16
#define FP_TX 17

// --- Hardware Objects ---
DHT dht(DHTPIN, DHTTYPE);
HardwareSerial mySerial2(2); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial2);
HardwareSerial gpsSerial(1); 
TinyGPSPlus gps;
WebServer server(80);

// --- Global Variables ---
bool isUnlocked = false;
bool isGpsLocked = false;
float tempC = 0.0;
float hum = 0.0;
String lat_str = "Acquiring...";
String lng_str = "Acquiring...";
String alt_str = "Acquiring...";
String time_str = "Syncing...";
String sysStatus = "System Normal";

unsigned long lastDhtRead = 0;
int fpState = 0; 
int newFpId = 0;

// --- HTML, CSS & JS (Ultra-Modern 3D UI with Map) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Satellite OS</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    
    <style>
        body { 
            margin: 0; 
            background: url('https://images.unsplash.com/photo-1451187580459-43490279c0fa?q=80&w=1920&auto=format&fit=crop') no-repeat center center fixed; 
            background-size: cover;
            color: white; 
            font-family: 'Segoe UI', sans-serif; 
            display: flex; justify-content: center; align-items: center; min-height: 100vh; 
        }
        /* Black overlay for better text readability */
        .overlay { position: fixed; top:0; left:0; width:100%; height:100%; background: rgba(0, 5, 15, 0.6); z-index: -1;}
        
        .glass-panel { 
            background: rgba(10, 15, 30, 0.65); 
            backdrop-filter: blur(15px); 
            border: 1px solid rgba(0, 255, 204, 0.4); 
            border-radius: 20px; 
            padding: 25px; 
            width: 90%; 
            max-width: 450px; 
            box-shadow: 0 0 30px rgba(0, 255, 204, 0.2), inset 0 0 15px rgba(0, 255, 204, 0.1); 
            margin: 20px 0;
        }
        h2 { text-align: center; color: #00ffcc; text-transform: uppercase; letter-spacing: 3px; margin-top: 0; border-bottom: 2px solid rgba(0,255,204,0.3); padding-bottom: 12px; text-shadow: 0 0 10px rgba(0,255,204,0.5);}
        .data-row { display: flex; justify-content: space-between; margin: 12px 0; padding-bottom: 8px; border-bottom: 1px solid rgba(255,255,255,0.08); font-size: 1rem;}
        .value { font-weight: bold; color: #00ffcc; text-shadow: 0 0 5px rgba(0,255,204,0.3);}
        
        #map { height: 180px; width: 100%; border-radius: 12px; margin-top: 15px; border: 2px solid #00ffcc; box-shadow: 0 0 15px rgba(0,255,204,0.3); }
        
        .controls { margin-top: 25px; display: flex; flex-direction: column; gap: 15px; }
        .controls input { padding: 12px; background: rgba(0,0,0,0.6); border: 1px solid #00ffcc; color: #00ffcc; border-radius: 8px; outline: none; text-align: center; font-weight:bold;}
        .btn { padding: 12px; background: linear-gradient(45deg, #00cca3, #00ffcc); color: #000; border: none; font-weight: bold; cursor: pointer; border-radius: 8px; text-transform: uppercase; transition: 0.3s; box-shadow: 0 4px 15px rgba(0,255,204,0.4);}
        .btn:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0,255,204,0.6); }
        .btn-danger { background: linear-gradient(45deg, #cc0000, #ff4d4d); box-shadow: 0 4px 15px rgba(255,77,77,0.4); color: white;}
        .btn-danger:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(255,77,77,0.6); }
        
        .sys-status { text-align: center; margin-top: 20px; font-weight: bold; color: #ffcc00; font-size: 0.95rem; background: rgba(0,0,0,0.5); padding: 10px; border-radius: 8px; border: 1px solid rgba(255,204,0,0.3);}
    </style>
</head>
<body>
    <div class="overlay"></div>
    <div class="glass-panel">
        <h2>Mission Control</h2>
        <div class="data-row"><span>Time (IST):</span><span class="value" id="time">--</span></div>
        <div class="data-row"><span>Temperature:</span><span class="value" id="temp">-- &deg;C</span></div>
        <div class="data-row"><span>Humidity:</span><span class="value" id="hum">-- %</span></div>
        <div class="data-row"><span>Sea Level Alt:</span><span class="value" id="alt">-- M</span></div>
        <div class="data-row"><span>Latitude:</span><span class="value" id="lat">--</span></div>
        <div class="data-row"><span>Longitude:</span><span class="value" id="lng">--</span></div>
        
        <div id="map"></div>
        <div class="sys-status" id="statusBox">System Normal</div>

        <div class="controls">
            <button class="btn" onclick="sendCommand('add')">Register Fingerprint</button>
            <div style="display:flex; gap:10px;">
                <input type="number" id="delId" placeholder="User ID" style="flex:1;">
                <button class="btn btn-danger" onclick="deleteFP()" style="flex:1;">Delete ID</button>
            </div>
        </div>
    </div>

    <script>
        // Map Setup
        var map = L.map('map').setView([20.5937, 78.9629], 4); 
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
        var marker = L.marker([20.5937, 78.9629]).addTo(map);
        var mapInitialized = false;

        // Smart Data Fetcher
        function fetchTelemetryData() {
            fetch('/data')
            .then(res => res.json())
            .then(data => {
                document.getElementById('temp').innerHTML = data.temperature + " &deg;C";
                document.getElementById('hum').innerHTML = data.humidity + " %";
                document.getElementById('alt').innerHTML = data.altitude + " M";
                document.getElementById('lat').innerText = data.latitude;
                document.getElementById('lng').innerText = data.longitude;
                document.getElementById('time').innerText = data.time;
                document.getElementById('statusBox').innerText = data.status;

                if(data.latitude !== "Acquiring..." && data.latitude !== "Searching...") {
                    var lat = parseFloat(data.latitude);
                    var lng = parseFloat(data.longitude);
                    marker.setLatLng([lat, lng]);
                    if(!mapInitialized) {
                        map.setView([lat, lng], 17); // Zoom close to location
                        mapInitialized = true;
                    }
                }
                setTimeout(fetchTelemetryData, 1000); 
            })
            .catch(err => { setTimeout(fetchTelemetryData, 2000); });
        }
        setTimeout(fetchTelemetryData, 1000);

        // Security Controls
        function sendCommand(action) { fetch('/cmd?action=' + action); }
        function deleteFP() {
            var id = document.getElementById('delId').value;
            if(id) { fetch('/cmd?action=delete&id=' + id); }
            else { alert('Enter User ID to delete'); }
        }
    </script>
</body>
</html>
)rawliteral";

// --- Server Endpoints ---
void handleRoot() { 
    server.send_P(200, "text/html", index_html); 
}

void handleData() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"temperature\":\"" + String(tempC) + "\",";
    json += "\"humidity\":\"" + String(hum) + "\",";
    json += "\"altitude\":\"" + alt_str + "\",";
    json += "\"latitude\":\"" + lat_str + "\",";
    json += "\"longitude\":\"" + lng_str + "\",";
    json += "\"time\":\"" + time_str + "\",";
    json += "\"status\":\"" + sysStatus + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleCommand() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("action")) {
        String action = server.arg("action");
        if (action == "add") {
            finger.getTemplateCount();
            newFpId = finger.templateCount + 1; 
            if(newFpId > 1000) { sysStatus = "Database Full!"; }
            else { 
                fpState = 1; 
                sysStatus = "ENROLL ID " + String(newFpId) + ": Place finger on sensor now"; 
            }
        } 
        else if (action == "delete" && server.hasArg("id")) {
            int delId = server.arg("id").toInt();
            if (delId == 1) {
                sysStatus = "ERROR: Cannot delete Master ID (1)";
            } else {
                if (finger.deleteModel(delId) == FINGERPRINT_OK) {
                    sysStatus = "ID " + String(delId) + " Deleted Successfully";
                } else {
                    sysStatus = "Error: ID " + String(delId) + " not found";
                }
            }
        }
    }
    server.send(200, "text/plain", "Command Received");
}

// --- Authentication Logic ---
bool authenticate() {
    Serial.println("\n[SECURE] Device Locked. Waiting for authorized fingerprint...");
    while (true) {
        uint8_t p = finger.getImage();
        if (p == FINGERPRINT_OK) {
            p = finger.image2Tz();
            if (p == FINGERPRINT_OK) {
                p = finger.fingerSearch();
                if (p == FINGERPRINT_OK) {
                    Serial.print("\n[SUCCESS] Unlocked by ID #"); Serial.println(finger.fingerID);
                    return true;
                } else {
                    Serial.println("[DENIED] Unrecognized Fingerprint.");
                    delay(1000);
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    dht.begin();
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    mySerial2.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
    finger.begin(57600);

    finger.getTemplateCount();
    if (finger.templateCount == 0) {
        Serial.println("\n[SETUP] Enrolling Master Fingerprint (ID #1)");
        while (finger.getImage() != FINGERPRINT_OK);
        finger.image2Tz(1);
        delay(2000);
        while (finger.getImage() != FINGERPRINT_NOFINGER);
        while (finger.getImage() != FINGERPRINT_OK);
        finger.image2Tz(2);
        finger.createModel();
        finger.storeModel(1);
        Serial.println("[SUCCESS] Master FP Saved!");
    }

    isUnlocked = authenticate();

    if (isUnlocked) {
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
        Serial.println("\n[SYSTEM] WiFi connected.");
        Serial.println("--> OPEN THIS IP IN BROWSER: http://" + WiFi.localIP().toString());

        server.on("/", handleRoot);
        server.on("/data", handleData);
        server.on("/cmd", handleCommand);
        server.begin();
    }
}

void loop() {
    if (isUnlocked) {
        server.handleClient();

        // 1. Fingerprint Non-Blocking Routine
        if (fpState == 1) {
            if (finger.getImage() == FINGERPRINT_OK) {
                finger.image2Tz(1);
                sysStatus = "Remove finger, then place it AGAIN";
                fpState = 2;
                delay(1500); 
            }
        } else if (fpState == 2) {
            uint8_t p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                finger.image2Tz(2);
                if (finger.createModel() == FINGERPRINT_OK) {
                    if (finger.storeModel(newFpId) == FINGERPRINT_OK) {
                        sysStatus = "SUCCESS! New User Saved as ID " + String(newFpId);
                    } else { sysStatus = "Error saving to memory"; }
                } else { sysStatus = "Error: Prints did not match. Try again."; }
                fpState = 0; 
            }
        }

        // 2. DHT11 Sensor Update (Every 2.5 Seconds)
        if (millis() - lastDhtRead > 2500) {
            float newTemp = dht.readTemperature();
            float newHum = dht.readHumidity();
            if (!isnan(newTemp)) tempC = newTemp;
            if (!isnan(newHum)) hum = newHum;
            lastDhtRead = millis();
        }

        // 3. Continuous GPS Reading
        while (gpsSerial.available() > 0) {
            gps.encode(gpsSerial.read());
        }

        // 4. GPS Data Extraction
        if (gps.location.isValid() && gps.location.isUpdated()) {
            lat_str = String(gps.location.lat(), 6);
            lng_str = String(gps.location.lng(), 6);
            
            if (gps.altitude.isValid()) {
                alt_str = String(gps.altitude.meters(), 1);
            }

            if (!isGpsLocked) {
                isGpsLocked = true;
                Serial.println("\n[ALERT] SATELLITE LINK ESTABLISHED!");
            }
        }
        
        // 5. IST Time Calculation (With Seconds)
        if (gps.time.isValid()) {
            int hr = gps.time.hour();
            int mn = gps.time.minute();
            int sc = gps.time.second();
            
            mn += 30;
            if (mn > 59) { mn -= 60; hr += 1; }
            hr += 5;
            if (hr > 23) hr -= 24;
            
            String ampm = hr >= 12 ? " PM" : " AM";
            hr = hr % 12;
            if (hr == 0) hr = 12;
            
            String hrStr = hr < 10 ? "0" + String(hr) : String(hr);
            String mnStr = mn < 10 ? "0" + String(mn) : String(mn);
            String scStr = sc < 10 ? "0" + String(sc) : String(sc);
            
            time_str = hrStr + ":" + mnStr + ":" + scStr + ampm;
        }
    }
}
