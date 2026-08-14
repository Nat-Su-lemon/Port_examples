#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

String startWifiCaptivePortal(const char* apSSID = "GuestSetup",
                              const char* apPass = "",          // "" = open AP
                              const char* hostname = "setup.local") {
  WebServer server(80);
  DNSServer dns;
  String result = "";
  bool done = false;

  // Start AP
  WiFi.mode(WIFI_AP);
  if (strlen(apPass) >= 8) WiFi.softAP(apSSID, apPass);
  else                     WiFi.softAP(apSSID);          // open if no valid pass

  IPAddress apIP = WiFi.softAPIP();

  // DNS: resolve everything to the AP IP -> lets custom hostname + captive portal work
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

  server.on("/", [&]() { server.send(200, "text/html", page); });

  server.on("/save", HTTP_POST, [&]() {
    result = server.arg("pw");
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;margin-top:60px'>"
      "<h2>Received. You can close this page.</h2></body></html>");
    done = true;
  });

  // Captive-portal redirects (phones auto-open the page)
  server.onNotFound([&]() {
    server.sendHeader("Location", String("http://") + hostname, true);
    server.send(302, "text/plain", "");
  });

  server.begin();

  while (!done) {
    dns.processNextRequest();
    server.handleClient();
    delay(2);
  }

  delay(300);            // let final response flush
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  return result;
}
