/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at http://blog.squix.ch

Adapter by Bodmer to use latest JPEGDecoder library.
*/

#include <Arduino.h>

//SPIFFS stuff
// Call up the SPIFFS FLASH filing system this is part of the ESP Core
// Define no globals so the File type does not clach with the SD library
// we must then use the SPIFFS namespace fs::File
// ##################################################################################
// IMPORTANT
// Use ESP8266 board Core version 2.3.0 to avoid a clash on the "File" definition!
// ##################################################################################
#define FS_NO_GLOBALS
#include <FS.h>

#include <SPI.h>

// Wifi Libraries
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>


// Easy Wifi Setup
#include <WiFiManager.h>

// Go to settings to change important parameters
#include "settings.h"

// Project libraries
#include "WifiLocator.h"
#include "PlaneSpotter.h" // Pulls in the TFT library

#include "artwork.h"
#include "AdsbExchangeClient.h"
#include "GeoMap.h"

// Initialize the TFT
TFT_ILI9341_ESP tft = TFT_ILI9341_ESP();

WifiLocator locator;
AdsbExchangeClient adsbClient;
GeoMap geoMap(MapProvider::Google, GOOGLE_API_KEY, MAP_WIDTH, MAP_HEIGHT);
//GeoMap geoMap(MapProvider::MapQuest, MAP_QUEST_API_KEY, MAP_WIDTH, MAP_HEIGHT);
PlaneSpotter planeSpotter(&tft, &geoMap);


Coordinates mapCenter;

// Check http://www.virtualradarserver.co.uk/Documentation/Formats/AircraftList.aspx
// to craft this query to your needs
// lat=47.424341887&lng=8.56877803&fDstL=0&fDstU=10&fAltL=0&fAltL=1500&fAltU=10000
//const String QUERY_STRING = "fDstL=0&fDstU=20&fAltL=0&fAltL=1000&fAltU=10000";
// airport zürich is on 1410ft => hide landed airplanes
const String QUERY_STRING = "fAltL=1500&trFmt=sa";

void downloadCallback(String filename, uint32_t bytesDownloaded, uint32_t bytesTotal);
ProgressCallback _downloadCallback = downloadCallback;

Coordinates northWestBound;
Coordinates southEastBound;


void setup() {

  // Start serial communication
  Serial.begin(115200);
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
  // The LED pin needs to set HIGH
  // Use this pin to save energy
//  pinMode(LED_PIN, D8);
//  digitalWrite(LED_PIN, HIGH);

  // Init TFT
  tft.begin();
  tft.setRotation(3);  // landscape
  //tft.cp437(false);
  tft.setFreeFont(&Dialog_plain_9);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(320);
  tft.drawString( "Loading Splash...", 160, 220, 1 );

  // Init file system
  if (!SPIFFS.begin()) { Serial.println("initialisation failed!"); return;}

  JpegDec.decodeArray(splash, splash_len);
  planeSpotter.jpegInfo();
  planeSpotter.renderJPEG(30 , 75);

  //tft.drawRect(30,75,260,92, TFT_GREEN); // Jpeg bounding box for tests

  tft.drawString("Connecting to WiFi...", 160, 220, 1 );
  
  
  WiFiManager wifiManager;
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  
  //wifiManager.setAPCallback(configModeCallback);

  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();

  // Set center of the map by using a WiFi fingerprint
  // Hardcode the values if it doesn't work or you want another location
  locator.updateLocation();
  mapCenter.lat = locator.getLat().toFloat();
  mapCenter.lon = locator.getLon().toFloat();

  tft.drawString("Loading map...", 160, 220, 1 );

  delay(500);
  
  geoMap.downloadMap(mapCenter, MAP_ZOOM, _downloadCallback);

  northWestBound = geoMap.convertToCoordinates({0,0});
  southEastBound = geoMap.convertToCoordinates({MAP_WIDTH, MAP_HEIGHT});
  tft.fillRect(0, geoMap.getMapHeight(), tft.width(), tft.height() - geoMap.getMapHeight(), TFT_BLACK);
}


void loop() {
  //Serial.println("Heap: " + String(ESP.getFreeHeap()));
  adsbClient.updateVisibleAircraft(QUERY_STRING + "&lat=" + String(mapCenter.lat, 6) + "&lng=" + String(mapCenter.lon, 6) + "&fNBnd=" + String(northWestBound.lat, 9) + "&fWBnd=" + String(northWestBound.lon, 9) + "&fSBnd=" + String(southEastBound.lat, 9) + "&fEBnd=" + String(southEastBound.lon, 9));

  Aircraft closestAircraft = adsbClient.getClosestAircraft(mapCenter.lat, mapCenter.lon);

  long startMillis = millis();
  planeSpotter.drawSPIFFSJpeg(geoMap.getMapName(), 0, 0);
  Serial.println(String(millis()-startMillis) + "ms for Jpeg drawing");
  //uint32_t pplot = millis();
  for (int i = 0; i < adsbClient.getNumberOfAircrafts(); i++) {
    Aircraft aircraft = adsbClient.getAircraft(i);
    AircraftHistory history = adsbClient.getAircraftHistory(i);
    planeSpotter.drawAircraftHistory(aircraft, history);
    planeSpotter.drawPlane(aircraft, aircraft.call == closestAircraft.call);
  }
  //Serial.print("Time to plot planes is: "); Serial.println(millis() - pplot);
  
  if (adsbClient.getNumberOfAircrafts()) {
    String fromString = planeSpotter.drawInfoBox(closestAircraft);
    // Use print stream so the line wraps (tft_->print does not work, kludge is to get the String returned so we can use the print class!)
    tft.setCursor(0, 228);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(BL_DATUM);
    tft.setTextWrap(1);
    // Using tft.print() means background is not plotted so we must blank out the old text
    // the advantage though is that it auto-wraps to the next line.
    tft.fillRect(0, 220, tft.width(), tft.height() - 220, TFT_BLACK);
    tft.print(fromString);
  }
  else tft.fillRect(0, geoMap.getMapHeight(), tft.width(), tft.height() - geoMap.getMapHeight(), TFT_BLACK);
  
  // Draw center of map
  CoordinatesPixel p = geoMap.convertToPixel(mapCenter);
  tft.fillCircle(p.x, p.y, 2, TFT_BLUE); 

  Serial.println(String(millis()-startMillis) + "ms for all drawing");
  delay(2000);

}


void downloadCallback(String filename, uint32_t bytesDownloaded, uint32_t bytesTotal) {
  Serial.println(String(bytesDownloaded) + " / " + String(bytesTotal));
  int width = 320;
  int progress = width * bytesDownloaded / bytesTotal;
  tft.fillRect(10, 220, progress, 5, TFT_WHITE);
  JpegDec.decodeArray(plane, plane_len);
  planeSpotter.renderJPEG(15 + progress, 220 - 15);
}


