#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <lwip/napt.h> 
#include <lwip/dns.h>

// Set your Access Point credentials
const char* ssid = "Aether_Control_Node";
const char* password = "adminpassword"; // Must be at least 8 characters

ESP8266WebServer server(80);

// NAPT Routing Variables
#define NAPT 1000
#define NAPT_PORT 10
bool naptEnabled = false;

// Connection State Machine Variables
String pendingSSID = "";
String pendingPass = "";
bool isConnecting = false;
unsigned long connectionStartTime = 0;

// Store the aesthetic HTML/CSS directly in PROGMEM
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control Center</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600&display=swap');
        
        body {
            margin: 0; padding: 0; font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
            color: #ffffff; display: flex; justify-content: center; align-items: center;
            min-height: 100vh; overflow-x: hidden;
        }

        .orb { position: fixed; border-radius: 50%; filter: blur(80px); z-index: 0; }
        .orb-1 { width: 300px; height: 300px; background: #00d2ff; top: -100px; left: -100px; animation: float 6s ease-in-out infinite; }
        .orb-2 { width: 400px; height: 400px; background: #3a7bd5; bottom: -150px; right: -100px; animation: float 8s ease-in-out infinite reverse; }

        @keyframes float { 0% { transform: translateY(0px); } 50% { transform: translateY(30px); } 100% { transform: translateY(0px); } }

        .glass-panel {
            position: relative; z-index: 1; background: rgba(255, 255, 255, 0.05);
            backdrop-filter: blur(16px); border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 20px; padding: 30px; width: 90%; max-width: 400px;
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37); margin: 20px 0;
        }

        h1 { font-size: 24px; font-weight: 600; margin-bottom: 5px; text-align: center; letter-spacing: 1px; }
        p.subtitle { text-align: center; color: rgba(255, 255, 255, 0.6); font-size: 14px; margin-bottom: 25px; }

        .data-card {
            background: rgba(0, 0, 0, 0.2); border-radius: 12px; padding: 15px; margin-bottom: 12px;
            display: flex; justify-content: space-between; align-items: center;
            border: 1px solid rgba(255, 255, 255, 0.05); transition: transform 0.2s;
        }
        .data-card:hover { background: rgba(0, 0, 0, 0.3); }
        .data-label { font-size: 14px; color: #aaa; word-break: break-all; margin-right: 10px; }
        .data-value { font-size: 16px; font-weight: 600; color: #00d2ff; white-space: nowrap; }

        .signal-weak { color: #ff4757; } .signal-med { color: #ffa502; } .signal-strong { color: #2ed573; }

        button {
            width: 100%; padding: 15px; margin-top: 15px; border: none; border-radius: 12px;
            background: linear-gradient(90deg, #00d2ff 0%, #3a7bd5 100%); color: white;
            font-size: 16px; font-weight: 600; cursor: pointer; box-shadow: 0 4px 15px rgba(0, 210, 255, 0.3);
            transition: all 0.3s ease;
        }
        button:hover { box-shadow: 0 6px 20px rgba(0, 210, 255, 0.5); transform: translateY(-2px); }
        button:disabled { background: #555; box-shadow: none; cursor: not-allowed; transform: none; }
        
        .btn-small { width: auto; padding: 8px 15px; font-size: 12px; margin-top: 0; margin-left: 10px; }
        .section-title { margin-top: 30px; margin-bottom: 15px; font-size: 18px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 5px; }
        
        #scan-results { max-height: 250px; overflow-y: auto; padding-right: 5px; }
        #scan-results::-webkit-scrollbar { width: 6px; }
        #scan-results::-webkit-scrollbar-track { background: rgba(0,0,0,0.1); border-radius: 10px; }
        #scan-results::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.2); border-radius: 10px; }
    </style>
</head>
<body>
    <div class="orb orb-1"></div>
    <div class="orb orb-2"></div>

    <div class="glass-panel">
        <h1>NODE STATUS</h1>
        <p class="subtitle">System Architecture Dashboard</p>

        <div class="data-card" style="border-color: rgba(0, 210, 255, 0.3);">
            <span class="data-label">Internet Uplink</span>
            <span class="data-value" id="uplink">Checking...</span>
        </div>
        <div class="data-card">
            <span class="data-label">Uptime</span>
            <span class="data-value" id="uptime">0s</span>
        </div>
        <div class="data-card">
            <span class="data-label">Free RAM</span>
            <span class="data-value" id="ram">0 KB</span>
        </div>
        <div class="data-card">
            <span class="data-label">Connected Clients</span>
            <span class="data-value" id="clients">0</span>
        </div>

        <div class="section-title">Environment Scan</div>
        
        <button id="scan-btn" onclick="scanNetworks()">Scan Airwaves</button>
        
        <div id="scan-results" style="display: none; margin-top: 15px;"></div>
    </div>

    <script>
        function fetchData() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('uptime').innerText = data.uptime;
                    document.getElementById('ram').innerText = data.ram + ' KB';
                    document.getElementById('clients').innerText = data.clients;
                    
                    let uplinkEl = document.getElementById('uplink');
                    uplinkEl.innerText = data.uplink;
                    
                    if(data.connected) {
                        uplinkEl.style.color = "#2ed573"; 
                    } else if (data.uplink === "Connecting...") {
                        uplinkEl.style.color = "#ffa502"; 
                    } else {
                        uplinkEl.style.color = "#ff4757"; 
                    }
                })
                .catch(err => console.log("Status fetch error"));
        }
        
        setInterval(fetchData, 2000); 
        window.onload = fetchData;

        function scanNetworks() {
            const btn = document.getElementById('scan-btn');
            const resultsDiv = document.getElementById('scan-results');
            
            btn.innerText = "Scanning Airwaves...";
            btn.disabled = true;
            resultsDiv.style.display = 'none';

            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    let html = "";
                    if(data.length === 0) {
                        html = "<p style='text-align:center; color:#aaa;'>No networks found.</p>";
                    } else {
                        data.forEach(net => {
                            let signalColor = "signal-strong";
                            if (net.rssi < -80) signalColor = "signal-weak";
                            else if (net.rssi < -65) signalColor = "signal-med";

                            html += `
                            <div class="data-card">
                                <div>
                                    <div class="data-label" style="color:#fff; font-weight:600;">${net.ssid || 'Hidden'}</div>
                                    <div class="data-label ${signalColor}" style="font-size:12px;">${net.rssi} dBm</div>
                                </div>
                                <button class="btn-small" onclick="connectTo('${net.ssid}')">Connect</button>
                            </div>`;
                        });
                    }
                    resultsDiv.innerHTML = html;
                    resultsDiv.style.display = 'block';
                    btn.innerText = "Rescan Airwaves";
                    btn.disabled = false;
                })
                .catch(err => {
                    btn.innerText = "Scan Failed - Try Again";
                    btn.disabled = false;
                });
        }

        function connectTo(ssid) {
            let pass = prompt("Enter password for: " + ssid);
            if (pass !== null) {
                document.getElementById('uplink').innerText = "Connecting...";
                document.getElementById('uplink').style.color = "#ffa502";
                
                fetch('/connect', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass)
                }).then(() => {
                    alert("Establishing Uplink. NOTE: If your phone disconnects, just reconnect to Aether_Control_Node.");
                });
            }
        }
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleData() {
  long uptimeSecs = millis() / 1000;
  String uptimeStr = String(uptimeSecs / 60) + "m " + String(uptimeSecs % 60) + "s";
  uint32_t freeRam = ESP.getFreeHeap() / 1024;
  uint8_t clients = WiFi.softAPgetStationNum();
  
  String uplinkStatus = "Offline Mode";
  bool isConnected = false;

  // Simplified Error Detection Logic for older cores
  if (WiFi.status() == WL_CONNECTED) {
    if (naptEnabled) {
      uplinkStatus = "Online: " + WiFi.localIP().toString();
      isConnected = true;
    } else {
      uplinkStatus = "Starting Routing...";
    }
  } else if (isConnecting) {
    uplinkStatus = "Connecting...";
    // 15 second timeout safety
    if (millis() - connectionStartTime > 15000) { 
      isConnecting = false;
      uplinkStatus = "Connection Failed"; // Generic error for wrong pass or bad network
    }
  }

  String json = "{\"uptime\":\"" + uptimeStr + "\",\"ram\":\"" + String(freeRam) + "\",\"clients\":\"" + String(clients) + "\",\"uplink\":\"" + uplinkStatus + "\",\"connected\":" + (isConnected ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleConnect() {
  if (server.hasArg("ssid")) {
    pendingSSID = server.arg("ssid");
    pendingPass = server.hasArg("pass") ? server.arg("pass") : "";
    
    Serial.println("Dashboard request: Connect to " + pendingSSID);
    
    naptEnabled = false; 
    
    isConnecting = true;
    connectionStartTime = millis();

    server.send(200, "text/plain", "Connecting");
    
    WiFi.disconnect(); 
    delay(100);
    WiFi.begin(pendingSSID.c_str(), pendingPass.c_str());

  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();

  Serial.print("Setting up Node AP... ");
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Node IP address: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/scan", handleScan);
  server.on("/connect", HTTP_POST, handleConnect); 

  server.begin();
  Serial.println("Dashboard started.");
}

void loop() {
  server.handleClient();

  if (isConnecting && WiFi.status() == WL_CONNECTED) {
      isConnecting = false;
  }

  // --- THE NAT ROUTER ENGINE ---
  if (WiFi.status() == WL_CONNECTED && !naptEnabled) {
    Serial.println("Internet Uplink Established!");
    
    err_t ret = ip_napt_init(NAPT, NAPT_PORT);
    if (ret == ERR_OK) {
      ret = ip_napt_enable_no(SOFTAP_IF, 1);
      if (ret == ERR_OK) {
        Serial.println("NAT Engine Started: Internet is now shared!");
        naptEnabled = true;
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED && naptEnabled) {
    Serial.println("Uplink Lost.");
    naptEnabled = false;
  }
}