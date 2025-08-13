#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_Fingerprint.h>
#include <SPIFFS.h>

// — Sensor & Server Setup —
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger(&mySerial);
WebServer server(80);

const char* ssid     = "ESP32-Fingerprint";
const char* password = "12345678";

const int MAX_USERS = 50;
String userNames[MAX_USERS];
int enrollID = 0;

// Logs
struct LogEntry { int id; unsigned long time; };
const int LOG_MAX = 50;
LogEntry logs[LOG_MAX];
int logCount = 0;

// Scan state
int lastDetected = -1;
unsigned long lastScanTime = 0;

// — Helpers: HTML Header & Footer —
String htmlHeader(const String& title) {
  return  
    "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>" + title + "</title>"
    "<style>"
      "body{font-family:Arial,sans-serif;margin:0;background:#f5f5f5;color:#333}"
      ".navbar{background:#004080;color:#fff;padding:1rem;}"
      ".navbar a{color:#fff;margin-right:1rem;text-decoration:none;font-weight:bold;}"
      ".container{max-width:800px;margin:2rem auto;background:#fff;padding:2rem;"
                 "box-shadow:0 0 10px rgba(0,0,0,0.1);}"
      "h1,h2,h3{color:#004080;margin-top:0}"
      "table{width:100%;border-collapse:collapse;margin-bottom:1rem}"
      "th,td{border:1px solid #ddd;padding:0.5rem;text-align:left}"
      "th{background:#f0f0f0}"
      ".button{display:inline-block;padding:0.5rem 1rem;background:#004080;"
              "color:#fff;text-decoration:none;border-radius:4px;margin:0.5rem 0;}"
      ".button:hover{background:#003060}"
      "form label{display:block;margin-top:0.5rem}"
      "form input[type=text],form input[type=number]{width:100%;padding:0.5rem;"
              "margin-top:0.25rem;border:1px solid #ccc;border-radius:4px}"
      "form input[type=submit]{margin-top:1rem;background:#004080;color:#fff;"
              "border:none;padding:0.75rem 1.5rem;border-radius:4px;cursor:pointer}"
      "form input[type=submit]:hover{background:#003060}"
      "#status{font-size:1.1rem;margin-bottom:1rem}"
    "</style>"
    "</head><body>"
    "<nav class='navbar'>"
      "<a href='/'>Home</a>"
      "<a href='/enroll'>Enroll</a>"         // ← Enroll button
      "<a href='/users'>Users</a>"
      "<a href='/logs'>Logs</a>"
      "<a href='/punch'>Punch Times</a>"
      "<a href='/clear'>Clear DB</a>"
    "</nav>"
    "<div class='container'>";
}

String htmlFooter() {
  return "</div></body></html>";
}

// — Setup & Loop —
void setup() {
  Serial.begin(115200);
  delay(200);

  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  finger.begin(57600);
  if (!finger.verifyPassword()) {
    Serial.println("Sensor not found!");
    while (1) delay(1);
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }
  loadUserData();

  WiFi.softAP(ssid, password, 1);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",       handleRoot);
  server.on("/enroll", handleEnroll);
  server.on("/users",  handleUserList);
  server.on("/delete", handleDelete);
  server.on("/logs",   handleLogs);
  server.on("/status", handleStatus);
  server.on("/punch",  handlePunch);
  server.on("/clear",  handleClearDatabase);
  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastScanTime > 200) {
    lastScanTime = millis();
    int id = getFingerprintID();
    if (id >= 0 && id < MAX_USERS && id != lastDetected) {
      lastDetected = id;
      logEvent(id, lastScanTime);
    }
  }
}

// — Handlers —

void handleRoot() {
  String html = htmlHeader("Fingerprint Server")
    + "<h2>Welcome</h2>"
    "<p id='status'>Waiting for finger...</p>"
    "</div>"
    "<script>"
      "setInterval(()=>{"
        "fetch('/status').then(r=>r.text()).then(t=>{"
          "let [id,name]=t.split('|');"
          "let el=document.getElementById('status');"
          "if(id>=0) el.innerText=`Detected ID: ${id} Name: ${name||'(unknown)'}`;"
          "else el.innerText='Waiting for finger...';"
        "});"
      "},500);"
    "</script>";
  server.send(200, "text/html", html);
}

void handleEnroll() {
  // show form if no args
  if (!server.hasArg("id") || !server.hasArg("name")) {
    String html = htmlHeader("Enroll User")
      + "<h2>Enroll New User</h2>"
      "<form action='/enroll' method='get'>"
        "ID: <input type='number' name='id' min='0' max='" + String(MAX_USERS-1) + "' required><br>"
        "Name: <input type='text'   name='name' required><br>"
        "<input type='submit' value='Enroll'>"
      "</form>"
      + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }
  // perform enrollment
  int id = server.arg("id").toInt();
  String name = server.arg("name");
  String res = enrollFingerprint(id);
  if (res.startsWith("Success")) {
    userNames[id] = name;
    if (id >= enrollID) enrollID = id + 1;
    saveUserData();
  }
  String html = htmlHeader("Enroll Result")
    + "<h2>Enrollment</h2>"
    "<p>" + res + "</p>"
    "<p><a class='button' href='/enroll'>Back to Enroll</a> "
    "<a class='button' href='/'>Home</a></p>"
    + htmlFooter();
  server.send(200, "text/html", html);
}

void handleUserList() {
  String html = htmlHeader("Users")
    + "<h2>Enrolled Users</h2>"
    "<table><tr><th>ID</th><th>Name</th><th>Action</th></tr>";
  for (int i = 0; i < enrollID; i++) {
    if (userNames[i].length()) {
      html += "<tr><td>" + String(i) + "</td>"
           + "<td>" + userNames[i] + "</td>"
           + "<td><a class='button' href='/delete?id=" + String(i) + "'>Delete</a></td></tr>";
    }
  }
  html += "</table><p><a class='button' href='/'>Home</a></p>" + htmlFooter();
  server.send(200, "text/html", html);
}

void handleDelete() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt();
  String msg;
  if (id < 0 || id >= MAX_USERS || !userNames[id].length()) msg = "Invalid ID";
  else if (finger.deleteModel(id) != FINGERPRINT_OK) msg = "Delete failed";
  else { userNames[id] = ""; saveUserData(); msg = "Deleted ID " + String(id); }
  String html = htmlHeader("Delete User")
    + "<h2>Delete</h2><p>" + msg + "</p>"
    "<p><a class='button' href='/users'>Back to Users</a></p>"
    + htmlFooter();
  server.send(200, "text/html", html);
}

void handleLogs() {
  String html = htmlHeader("Logs")
    + "<h2>Detection Logs</h2>"
    "<table><tr><th>Time</th><th>ID</th><th>Name</th></tr>";
  for (int i = 0; i < logCount; i++) {
    int id = logs[i].id;
    String name = (id >= 0 && id < MAX_USERS) ? userNames[id] : "";
    html += "<tr><td>" + formatTime(logs[i].time) + "</td>"
         +   "<td>" + String(id) + "</td>"
         +   "<td>" + (name.length() ? name : "(unknown)") + "</td></tr>";
  }
  html += "</table><p><a class='button' href='/'>Home</a></p>" + htmlFooter();
  server.send(200, "text/html", html);
}

void handleStatus() {
  int id = lastDetected;
  String name = (id >= 0 && id < MAX_USERS) ? userNames[id] : "";
  server.send(200, "text/plain", String(id) + "|" + name);
}

void handlePunch() {
  if (!server.hasArg("id")) {
    String html = htmlHeader("Punch Times")
      + "<h2>Punch Times</h2>"
      "<form action='/punch' method='get'>"
        "<label>User ID:</label>"
        "<input type='number' name='id' min='0' max='" + String(MAX_USERS-1) + "' required>"
        "<input type='submit' value='Show Times'>"
      "</form>"
      "<p><a class='button' href='/'>Home</a></p>"
      + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }
  int pid = server.arg("id").toInt();
  unsigned long e[2] = {0, 0};
  int fnd = 0;
  for (int i = 0; i < logCount && fnd < 2; i++) {
    if (logs[i].id == pid) e[fnd++] = logs[i].time;
  }
  String inT  = (fnd > 0 ? formatTime(e[0]) : "N/A");
  String outT = (fnd > 1 ? formatTime(e[1]) : "N/A");
  String html = htmlHeader("Punch Times")
    + "<h2>Punch Times for ID " + String(pid) + "</h2>"
    "<p>Punch-In: " + inT + "</p>"
    "<p>Punch-Out: " + outT + "</p>"
    "<p><a class='button' href='/punch'>Back</a> <a class='button' href='/'>Home</a></p>"
    + htmlFooter();
  server.send(200, "text/html", html);
}

void handleClearDatabase() {
  // erase sensor DB
  if (finger.emptyDatabase() != FINGERPRINT_OK) {
    for (int i = 0; i < MAX_USERS; i++) finger.deleteModel(i);
  }
  // clear local storage
  for (int i = 0; i < MAX_USERS; i++) userNames[i] = "";
  enrollID = 0;
  saveUserData();
  logCount = 0;
  // confirmation
  String html = htmlHeader("Database Cleared")
    + "<h2>All fingerprints erased</h2>"
    "<p>User list and logs have been reset.</p>"
    "<p><a class='button' href='/'>Home</a></p>"
    + htmlFooter();
  server.send(200, "text/html", html);
}

// — Core Functions —

int getFingerprintID() {
  if (finger.getImage()        != FINGERPRINT_OK) return -1;
  if (finger.image2Tz()        != FINGERPRINT_OK) return -1;
  if (finger.fingerSearch()    == FINGERPRINT_OK)  return finger.fingerID;
  return -1;
}

String enrollFingerprint(int id) {
  while (finger.getImage() != FINGERPRINT_OK);
  if (finger.image2Tz(1) != FINGERPRINT_OK)   return "Error: first scan failed";
  delay(2000);
  while (finger.getImage() != FINGERPRINT_OK);
  if (finger.image2Tz(2) != FINGERPRINT_OK)   return "Error: second scan failed";
  if (finger.createModel() != FINGERPRINT_OK) return "Error: model creation failed";
  if (finger.storeModel(id) != FINGERPRINT_OK) return "Error: storing failed";
  return "Success: ID " + String(id) + " enrolled";
}

void logEvent(int id, unsigned long t) {
  if (logCount < LOG_MAX) logs[logCount++] = {id, t};
  else {
    for (int i = 1; i < LOG_MAX; i++) logs[i - 1] = logs[i];
    logs[LOG_MAX - 1] = {id, t};
  }
}

String formatTime(unsigned long ms) {
  unsigned long s = ms / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s / 60) % 60;
  unsigned long sec = s % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
  return String(buf);
}

void loadUserData() {
  File f = SPIFFS.open("/user_data.txt", "r");
  if (!f) return;
  enrollID = 0;
  while (f.available() && enrollID < MAX_USERS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) userNames[enrollID++] = line;
  }
  f.close();
}

void saveUserData() {
  File f = SPIFFS.open("/user_data.txt", "w");
  if (!f) return;
  for (int i = 0; i < enrollID; i++) f.println(userNames[i]);
  f.close();
}
