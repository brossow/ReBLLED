#ifndef _BBLPRINTERDISCOVERY_H
#define _BBLPRINTERDISCOVERY_H

#include <WiFi.h>
#include <WiFiUdp.h>
#include "filesystem.h" // for saveFileSystem()
#include "printerModels.h"

#define BBL_SSDP_PORT 2021
#define BBL_SSDP_MCAST_IP IPAddress(239, 255, 255, 250)
#define BBL_DISCOVERY_INTERVAL 10000UL
#define BBL_DISCOVERY_INTERVAL_ONLINE 300000UL
#define BBL_MAX_PRINTERS 10

struct BBLPrinter
{
  IPAddress ip;
  char usn[64];
  char model[12]; // setup page model key, or the printer's own code when unknown
  char name[33];  // the name given to it in Bambu's app
};

static WiFiUDP bblUdp;
static bool bblUdpInitialized = false;
static unsigned long bblLastDiscovery = 0;

static BBLPrinter bblLastKnownPrinters[BBL_MAX_PRINTERS];
static int bblKnownPrinterCount = 0;

bool bblIsPrinterKnown(IPAddress ip, int *index = nullptr)
{
  for (int i = 0; i < bblKnownPrinterCount; i++)
  {
    if (bblLastKnownPrinters[i].ip == ip)
    {
      if (index)
        *index = i;
      return true;
    }
  }
  return false;
}

void bblPrintKnownPrinters()
{
  LogSerial.println("[BBLScan] Known printers:");
  if (bblKnownPrinterCount == 0)
  {
    LogSerial.println("  (none)");
  }
  for (int i = 0; i < bblKnownPrinterCount; i++)
  {
    LogSerial.printf("  [%d] IP: %s", i + 1, bblLastKnownPrinters[i].ip.toString().c_str());
    if (strlen(bblLastKnownPrinters[i].usn) > 0)
    {
      LogSerial.printf("  [USN: %s]", bblLastKnownPrinters[i].usn);
    }
    LogSerial.println();
  }
}

// Handles one SSDP reply/notify that is already read into 'buffer'.
static void bblHandleDiscoveryPacket(IPAddress senderIP, char *buffer)
{
  char usn[64], code[32], name[33], model[12] = "";
  bblssdp::header(buffer, "USN", usn, sizeof usn);
  if (bblssdp::header(buffer, "DevModel.bambu.com", code, sizeof code))
    bblssdp::modelKey(code, model, sizeof model);
  bblssdp::header(buffer, "DevName.bambu.com", name, sizeof name);
  String usnStr = usn;

  // The followed printer says what model it is (docs/REDESIGN.md: detected, with an override).
  if (model[0] && usnStr.length() > 0 && strcmp(printerConfig.serialNumber, usn) == 0 &&
      strcmp(printerVariables.detectedModel, model) != 0)
  {
    StateLock lock;
    strlcpy(printerVariables.detectedModel, model, sizeof(printerVariables.detectedModel));
    LogSerial.printf("[BBLScan] The printer reports its model as %s (%s)\n", model, code);
  }

  // IP update check for stored USN
  if (strlen(printerConfig.serialNumber) > 0 && usnStr.length() > 0 &&
      strcmp(printerConfig.serialNumber, usnStr.c_str()) == 0)
  {
    String currentIP = senderIP.toString();
    // Only follow an IP change when the configured printer has been unreachable for a
    // while. Any device on the LAN can answer SSDP with the printer's serial, and the
    // MQTT login (the access code) would then be sent to it.
    bool printerUnreachable = !printerVariables.online && printerVariables.disconnectMQTTms != 0 &&
                              (millis() - printerVariables.disconnectMQTTms) > 120000UL;
    if (String(printerConfig.printerIP) != currentIP && !printerUnreachable)
    {
      LogSerial.printf("[BBLScan] Printer serial answered from %s, but the configured printer is online - ignoring\n", currentIP.c_str());
    }
    else if (String(printerConfig.printerIP) != currentIP)
    {
      LogSerial.printf("[BBLScan] Detected matching USN with updated IP (%s → %s). Saving...\n", printerConfig.printerIP, currentIP.c_str());
      strlcpy(printerConfig.printerIP, currentIP.c_str(), sizeof(printerConfig.printerIP));
      saveFileSystem();
    }
  }

  int existingIndex = -1;
  bool isNewPrinter = !bblIsPrinterKnown(senderIP, &existingIndex);

  if (printerConfig.debuging || (printerConfig.debugingchange && isNewPrinter))
  {
    LogSerial.printf("[BBLScan]  IP: %s", senderIP.toString().c_str());
    if (usnStr.length())
    {
      LogSerial.printf("  [USN: %s]", usnStr.c_str());
    }
    LogSerial.println();
  }

  BBLPrinter *printer = nullptr;
  if (!isNewPrinter)
    printer = &bblLastKnownPrinters[existingIndex];
  else if (bblKnownPrinterCount < BBL_MAX_PRINTERS)
  {
    printer = &bblLastKnownPrinters[bblKnownPrinterCount++];
    printer->ip = senderIP;
    printer->usn[0] = printer->model[0] = printer->name[0] = 0;
  }
  if (printer) // keep what's newest (a name can be changed in Bambu's app)
  {
    StateLock lock; // the web server reads the list
    if (usn[0]) strlcpy(printer->usn, usn, sizeof(printer->usn));
    if (model[0]) strlcpy(printer->model, model, sizeof(printer->model));
    if (name[0]) strlcpy(printer->name, name, sizeof(printer->name));
  }
}

// Non-blocking: sends an M-SEARCH every interval and handles replies as they arrive on
// later loop() passes. (Previously this waited 5.5 s inside loop() every 10 s, freezing
// LED timers, door/finish handling, WiFi reconnects and the web status push.)
void bblSearchPrinters()
{
  if (!bblUdpInitialized)
  {
    bblUdp.beginMulticast(BBL_SSDP_MCAST_IP, BBL_SSDP_PORT);
    bblUdpInitialized = true;
  }

  // Search often until the configured printer is connected; after that only slowly
  // (the results feed the printer list in the setup page and IP-change following).
  unsigned long interval = printerVariables.online ? BBL_DISCOVERY_INTERVAL_ONLINE : BBL_DISCOVERY_INTERVAL;
  unsigned long now = millis();
  if (now - bblLastDiscovery >= interval)
  {
    bblLastDiscovery = now;
    static const char msearch[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:2021\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 5\r\n"
        "ST: urn:bambulab-com:device:3dprinter:1\r\n\r\n";
    for (int i = 0; i < 2; i++)
    {
      bblUdp.beginPacket(BBL_SSDP_MCAST_IP, BBL_SSDP_PORT);
      bblUdp.print(msearch);
      bblUdp.endPacket();
    }
    if (printerConfig.debuging)
    {
      LogSerial.println("[BBLScan] Searching for printers...");
      bblPrintKnownPrinters();
    }
  }

  // Handle whatever has arrived, a few packets per pass so loop() stays responsive.
  for (int i = 0; i < 5; i++)
  {
    int size = bblUdp.parsePacket();
    if (size <= 0)
      break;
    char buffer[768];
    int len = bblUdp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0)
      continue;
    buffer[len] = 0;
    bblHandleDiscoveryPacket(bblUdp.remoteIP(), buffer);
  }
}

#endif
