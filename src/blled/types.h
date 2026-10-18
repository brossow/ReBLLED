#ifndef _TYPES
#define _TYPES

#ifdef __cplusplus
extern "C"
{
#endif

    // What the printer reports (mqttmanager.h). LED decisions are made in lights.h.
    typedef struct PrinterVaraiblesStruct{
        String parsedHMSlevel = "";
        uint64_t parsedHMScode = 0;            //8 bytes per code stored
        String gcodeState = "FINISH";           // so the printer's first report isn't taken as a print just finishing
        int stage = -1;                         // stg_cur; -1 = idle (0 would read as "printing" before the first report)
        int overridestage = 999;
        bool printerledstate = true;            // the printer's own (chamber) light
        bool hmsstate = false;
        bool online = false;
        bool accessRejected = false;            // last connect refused the access code
        bool certRejected = false;              // the printer's certificate didn't check out: code not sent
        unsigned long disconnectMQTTms = 0;     // when the connection to the printer was lost
        bool doorOpen = false;
        unsigned long lastdoorClosems = 0;
        unsigned long lastdoorOpenms = 0;
        char detectedModel[12] = "";            // model code the printer reports (stage 5); "" = unknown
    } PrinterVariables;
    PrinterVariables printerVariables;

    // Serial lookup for setup ("Look up", GET /api/lookup): the web server asks, the MQTT task
    // (which owns TLS) connects to the address and reads the printer's certificate.
    typedef struct PrinterLookupStruct{
        char ip[16];
        volatile uint8_t state;                 // 0 idle, 1 asked, 2 running, 3 done
        bool answered;                          // something accepted a TLS connection
        bool trusted;                           // its certificate is signed by Bambu Lab
        char serial[40];                        // the name on the certificate
        unsigned long doneMs;
    } PrinterLookup;
    PrinterLookup printerLookup = {};

    typedef struct SecurityVariables{
                // Security
        char HTTPUser[40] = "";             //http basic auth username
        char HTTPPass[40] = "";              //http basic auth password
    }SecurityVariables;
    SecurityVariables securityVariables;



    typedef struct GlobalVariablesStruct{
        char SSID[32];
        char APPW[64];
        String FWVersion = STRVERSION;
        String Host = "BLLED";
        bool started = false;
    } GlobalVariables;

    GlobalVariables globalVariables;

    // Connection settings, plus runtime mirrors of a few v2 settings that older code reads
    // (lights.h keeps them in sync). The LED settings themselves are v2::Settings (settings.h).
    typedef struct PrinterConfigStruct
    {
        char printerIP[16];             //BBLP IP Address - used for MQTT reports
        char accessCode[9];             //BBLP Access Code - used for MQTT reports
        char serialNumber[16];          //BBLP Serial Number - used for MQTT reports
        char BSSID[18];                 //Nominated AP to connect to (Useful if multiple accesspoints with same name)
        bool rescanWiFiNetwork = false; //Scans available WiFi networks for strongest signal
        // mirrors of v2 settings
        bool debuging = false;
        bool debugingchange = false;
        bool mqttdebug = false;
        String hmsIgnoreList;
    } PrinterConfig;

    PrinterConfig printerConfig;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif