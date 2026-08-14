#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

String startWifiCaptivePortal(const char* apSSID = "GuestSetup",
                              const char* apPass = "") {      // "" = open AP
  WebServer server(80);
  DNSServer dns;
  String result = "";
  bool done = false;

  // Start AP
  WiFi.mode(WIFI_AP);
  if (strlen(apPass) >= 8) WiFi.softAP(apSSID, apPass);
  else                     WiFi.softAP(apSSID);
  delay(500);

  IPAddress apIP = WiFi.softAPIP();
  String ipStr = apIP.toString();

  // Wildcard DNS: resolve every domain to the AP IP
  dns.start(53, "*", apIP);

  const char* page =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'><title>WiFi Setup</title></head>"
    "<body style='font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px'>"
    "<h2>Guest WiFi Password</h2>"
    "<form action='/save' method='POST'>"
    "<input name='pw' type='text' style='width:100%;padding:8px;font-size:16px' "
    "placeholder='Enter password' autofocus>"
    "<button type='submit' style='margin-top:12px;padding:10px 20px;font-size:16px'>"
    "Submit</button></form></body></html>";

  // Main page
  server.on("/", [&]() { server.send(200, "text/html", page); });

  // Save handler
  server.on("/save", HTTP_POST, [&]() {
    result = server.arg("pw");
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;margin-top:60px'>"
      "<h2>Received. You can close this page.</h2></body></html>");
    done = true;
  });

  // --- Captive portal probe URLs: redirect all to the form ---
  auto redirect = [&]() {
    server.sendHeader("Location", String("http://") + ipStr, true);
    server.send(302, "text/plain", "");
  };

  server.on("/generate_204", redirect);      // Android
  server.on("/gen_204", redirect);           // Android (older)
  server.on("/hotspot-detect.html", redirect); // Apple iOS/macOS
  server.on("/library/test/success.html", redirect); // Apple (alt)
  server.on("/connecttest.txt", redirect);   // Windows
  server.on("/ncsi.txt", redirect);          // Windows (older)
  server.on("/redirect", redirect);          // Windows
  server.on("/canonical.html", redirect);    // Firefox
  server.on("/success.txt", redirect);       // Firefox

  // Catch-all: anything else also goes to the form
  server.onNotFound(redirect);

  server.begin();

  // Serve until user submits
  while (!done) {
    dns.processNextRequest();
    server.handleClient();
    delay(2);
  }

  delay(300);            // flush final response
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  return result;
}
