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

// --- Variabel Global ---
String MAC_;
String nama;
char eviltwinpath[32];
char pishingpath[32];
File webportal;
String tes_password;
String data_pishing;
String LOG;
String json_data;
String fileList = ""; // Tambahin ini biar gak error 'fileList' not declared
bool hidden_target = false;
bool rogueap_continues = false;

extern bool progmemToSpiffs(const char* adr, int len, String path);
#include "webfiles.h"

extern Scan   scan;
extern CLI    cli;
extern Attack attack;

namespace wifi {
    wifi_mode_t   mode;
    ap_settings_t ap_settings;
    ESP8266WebServer server(80);
    DNSServer dns;
    IPAddress ip WEB_IP_ADDR;
    IPAddress netmask(255, 255, 255, 0);

    // --- Fungsi Helper Dasar ---
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

    // --- Deklarasi Fungsi-fungsi Handler (Agar startAP bisa kenal) ---
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

    void send_eviltwin() {
        if(!sendWebSpiffs("/eviltwin.html")) server.send(404, "text/plain", "File Missing");
    }

    void send_rogueap() {
        if(!sendWebSpiffs("/rogue.html")) server.send(404, "text/plain", "File Missing");
    }

    void handleDelete() {
        String path = server.arg("path");
        if(LittleFS.remove(path)) server.send(200, "text/plain", "Deleted");
        else server.send(500, "text/plain", "Delete Failed");
    }

    void sendFSjson() {
        server.send(200, "application/json", json_data);
    }

    void pathsave() {
        // Logika save path lu di sini
        server.send(200, "text/plain", "Saved");
    }

    // --- Fungsi Utama ---
    void startAP() {
        WiFi.softAPConfig(ip, ip, netmask);
        WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
        dns.start(53, "*", ip);
        
        server.on("/filelist", handleFileListJS);
        server.on("/delete", handleDelete);
        server.on("/filemanager.json", sendFSjson);
        server.on("/websetting", HTTP_POST, pathsave);

        server.on("/", HTTP_GET, []() {
            if(!attack.eviltwin && !attack.pishing){
                if(!sendWebSpiffs("/index.html")) server.send(200, "text/html", "Web Files Missing");
            } else {
                if(attack.eviltwin) send_eviltwin();
                if(attack.pishing) send_rogueap();
            }
        });

        server.begin();
        mode = wifi_mode_t::ap;
    }

    void begin() {
        // Inisialisasi awal
        mode = wifi_mode_t::off;
    }

    void update() {
        if (mode != wifi_mode_t::off) {
            server.handleClient();
            dns.processNextRequest();
        }
    }
}
