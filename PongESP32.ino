#include <SPI.h>
#include "ESP32-RGB32x16MatrixPanel-I2S-DMA.h"

#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "font3x5.h"
#include "font5x5.h"
#include "blinky.h"
#include <SPIFFS.h>
#include <simpleDSTadjust.h>
/*
**The MIT License (MIT)
Copyright (c) 2018 by Daniel Eichhorn - ThingPulse
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
See more at https://thingpulse.com

Credits on the RGB display routines:
 Louis Beaudoin <https://github.com/pixelmatix/SmartMatrix/tree/teensylc>
 and Sprite_TM:       https://www.esp32.com/viewtopic.php?f=17&t=3188 and https://www.esp32.com/viewtopic.php?f=13&t=3256
 and mrFaptastic: https://github.com/mrfaptastic/ESP32-RGB64x32MatrixPanel-I2S-DMA

 Credits on Pong Clock
 Paul Kourany:  https://github.com/pkourany/RGBPongClock
 /*  RGB Pong Clock - Andrew Holmes @pongclock
**  Inspired by, and shamelessly derived from 
**      Nick's LED Projects
**  https://123led.wordpress.com/about/
**  
**  Videos of the clock in action:
**  https://vine.co/v/hwML6OJrBPw
**  https://vine.co/v/hgKWh1KzEU0
**  https://vine.co/v/hgKz5V0jrFn
**  I run this on a Mega 2560, your milage on other chips may vary,
**  Can definately free up some memory if the bitmaps are shrunk down to size.
**  Uses an Adafruit 16x32 RGB matrix availble from here:
**  http://www.phenoptix.com/collections/leds/products/16x32-rgb-led-matrix-panel-by-adafruit
**  This microphone:
**  http://www.phenoptix.com/collections/adafruit/products/electret-microphone-amplifier-max4466-with-adjustable-gain-by-adafruit-1063
**  a DS1307 RTC chip (not sure where I got that from - was a spare)
**  and an Ethernet Shield
**  http://hobbycomponents.com/index.php/dvbd/dvbd-ardu/ardu-shields/2012-ethernet-w5100-network-shield-for-arduino-uno-mega-2560-1280-328.html
** 
** All code adjusted for ESP32 and RGB32x16 for the Eindhoven IoT Workshop by Frank Beks https://github.com/galagaking
* 

*/

#include <ESPHTTPClient.h>
#include <JsonListener.h>

// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
//#include <coredecls.h>                  // settimeofday_cb()

#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"


/***************************
 * Begin Settings
 **************************/

// WIFI
//const char* WIFI_SSID = "<YOUR SSID>";
//const char* WIFI_PWD = "<YOUR WIFI CODE>";

const char* WIFI_SSID = "Warehouse of innovation";
const char* WIFI_PWD = "magazijn";

#define TZ              2       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries

// Setup
const int UPDATE_INTERVAL_SECS = 1 * 60; // Update every 1 minutes

// OpenWeatherMap Settings
// Sign up here to get an API key:
// https://docs.thingpulse.com/how-tos/openweathermap-key/
String OPEN_WEATHER_MAP_APP_ID = "<YOUR WEATHER API>";


/*
Go to https://openweathermap.org/find?q= and search for a location. Go through the
result set and select the entry closest to the actual location you want to display 
data for. It'll be a URL like https://openweathermap.org/city/2657896. The number
at the end is what you assign to the constant below.
 */
String OPEN_WEATHER_MAP_LOCATION_ID = "2756253";

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
String OPEN_WEATHER_MAP_LANGUAGE = "nl";
#define endpoint "http://api.openweathermap.org/data/2.5/weather?id=2756253&appid=74926df1f34f28ad6ca624530ab7fefe&units=metric&lang=nl"
const uint8_t MAX_FORECASTS = 4;

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

const boolean IS_METRIC = true;

// Adjust according to your language
const String WDAY_NAMES[] = {"ZON", "MAA", "DIN", "WOE", "DON", "VRI", "ZAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OKT", "NOV", "DEC"};

struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600}; // Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 2, 0};       // Central European Time = UTC/GMT +1 hour
#define UTC_OFFSET +1
#define NTP_SERVERS "0.ch.pool.ntp.org", "1.ch.pool.ntp.org", "2.ch.pool.ntp.org"


simpleDSTadjust dstAdjusted(StartRule, EndRule);

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

#if defined(ESP8266)
Ticker display_ticker;
#else
long timeSinceLastWUpdate = 0;
#endif

//declaring prototypes
void updateData();
void drawDateTime();
void setReadyForWeatherUpdate();
uint8_t fdow(unsigned long);

#define DEBUGME

// allow us to use itoa() in this scope
extern char* itoa(int a, char* buffer, unsigned char radix);

#ifdef DEBUGME
	#define DEBUGp(message)		Serial.print(message)
	#define DEBUGpln(message)	Serial.println(message)
#else
	#define DEBUGp(message)
	#define DEBUGpln(message)
#endif

RGB32x16MatrixPanel_I2S_DMA display;

// Some standard colors
uint16_t myRED = display.color565(255, 0, 0);
uint16_t myGREEN = display.color565(0, 255, 0);
uint16_t myBLUE = display.color565(0, 0, 255);
uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myYELLOW = display.color565(255, 255, 0);
uint16_t myCYAN = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK = display.color565(0, 0, 0);

uint16_t myCOLORS[8]={myRED,myGREEN,myBLUE,myWHITE,myYELLOW,myCYAN,myMAGENTA,myBLACK};

uint8_t static weather_icons[]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00
  ,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xdf,0x07,0xdf,0x07,0xdf,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x20,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xdf,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00
  ,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00
  ,0x00,0x20,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x07,0xdf,0x07,0xdf,0x07,0xff,0xff,0xe0,0xff,0xe0,0x00,0x00
  ,0x00,0x00,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0xff,0xe0,0x00,0x20,0x00,0x00,0x07,0xdf,0x07,0xdf,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xdf,0x07,0xdf,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0xff,0xff,0x07,0xff,0x07,0xff,0x07,0xdf,0x07,0xff,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xdf,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00
  ,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x07,0xff,0x00,0x20,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xdf,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xdf,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0x07,0xdf,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00
  ,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0xff,0xe0,0xff,0xe0,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00
  ,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xdf,0x07,0xdf,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x07,0xff,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00
  ,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x07,0xdf,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xdf,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0x07,0xdf,0x07,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00
  ,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

uint8_t icon_index=0;

#define SHOWCLOCK 100
#define MAX_CLOCK_MODE		7                 // Number of clock modes


/********** RGB565 Color definitions **********/
#define Black           0x0000
#define Navy            0x000F
#define DarkGreen       0x03E0
#define DarkCyan        0x03EF
#define Maroon          0x7800
#define Purple          0x780F
#define Olive           0x7BE0
#define LightGrey       0xC618
#define DarkGrey        0x7BEF
#define Blue            0x001F
#define Green           0x07E0
#define Cyan            0x07FF
#define Red             0xF800
#define Magenta         0xF81F
#define Yellow          0xFFE0
#define White           0xFFFF
#define Orange          0xFD20
#define GreenYellow     0xAFE5
#define Pink			0xF81F
/**********************************************/

#define X_MAX 31                         // Matrix X max LED coordinate (for 2 displays placed next to each other)
#define Y_MAX 15
#define BAT1_X 2                         // Pong left bat x pos (this is where the ball collision occurs, the bat is drawn 1 behind these coords)
#if (X_MAX == 63)
#define BAT2_X 60
#else
#define BAT2_X 28
#endif

/********** PACMAN definitions **********/


#define usePACMAN			// Uncomment to enable PACMAN animations

int powerPillEaten = 0;

/********* USING SPECIAL MESSAGES **********/

#define USING_SPECIAL_MESSAGES

#ifdef USING_SPECIAL_MESSAGES

struct SpecialDays {
  int month, day;
  char* message;
};

const SpecialDays ourHolidays[] = {  //keep message to < 40 chars
  { 0, 0, "HAVE A NICE DAY"},
  { 1, 1, "HAPPY NEW YEAR"},
  { 2, 14, "HAPPY ST PATRICKS DAY"},
  { 4, 1, "HAPPY BIRTHDAY TOM"},
  { 7, 4, "HAPPY 4TH OF JULY"},
  { 7, 15, "HAPPY ANNIVERSARY"},
  { 8, 12, "HAPPY BIRTHDAY GRAMPA"},
  { 9, 26, "HAPPY BIRTHDAY CHASE AND CHANDLER"},
  {10, 31, "HAPPY HALLOWEEN"},
  {11, 1, "HAPPY BIRTHDAY PATCHES"},
  {12, 18, "Birthday of Pong!"},
  {12, 25, "MERRY CHRISTMAS"} 
};
#endif

int stringPos;

int badWeatherCall;
char w_temp[8][7] = {""};
char w_id[8][4] = {""};

char *dstAbbrev;
char time_str[11];
time_t now;
struct tm * timeinfo; 

boolean wasWeatherShownLast= true;
unsigned long lastWeatherTime =0;

int mode_changed = 0;			// Flag if mode changed.
bool mode_quick = false;		// Quick weather display
int clock_mode = 2;				// Default clock mode (1 = pong)
uint16_t showClock = SHOWCLOCK;		// Default time to show a clock face
unsigned long modeSwitch;
unsigned long updateCTime;		// 24hr timer for resyncing cloud time


/************  PLASMA definitions **********/

static const int8_t PROGMEM sinetab[256] = {
     0,   2,   5,   8,  11,  15,  18,  21,
    24,  27,  30,  33,  36,  39,  42,  45,
    48,  51,  54,  56,  59,  62,  65,  67,
    70,  72,  75,  77,  80,  82,  85,  87,
    89,  91,  93,  96,  98, 100, 101, 103,
   105, 107, 108, 110, 111, 113, 114, 116,
   117, 118, 119, 120, 121, 122, 123, 123,
   124, 125, 125, 126, 126, 126, 126, 126,
   127, 126, 126, 126, 126, 126, 125, 125,
   124, 123, 123, 122, 121, 120, 119, 118,
   117, 116, 114, 113, 111, 110, 108, 107,
   105, 103, 101, 100,  98,  96,  93,  91,
    89,  87,  85,  82,  80,  77,  75,  72,
    70,  67,  65,  62,  59,  56,  54,  51,
    48,  45,  42,  39,  36,  33,  30,  27,
    24,  21,  18,  15,  11,   8,   5,   2,
     0,  -3,  -6,  -9, -12, -16, -19, -22,
   -25, -28, -31, -34, -37, -40, -43, -46,
   -49, -52, -55, -57, -60, -63, -66, -68,
   -71, -73, -76, -78, -81, -83, -86, -88,
   -90, -92, -94, -97, -99,-101,-102,-104,
  -106,-108,-109,-111,-112,-114,-115,-117,
  -118,-119,-120,-121,-122,-123,-124,-124,
  -125,-126,-126,-127,-127,-127,-127,-127,
  -128,-127,-127,-127,-127,-127,-126,-126,
  -125,-124,-124,-123,-122,-121,-120,-119,
  -118,-117,-115,-114,-112,-111,-109,-108,
  -106,-104,-102,-101, -99, -97, -94, -92,
   -90, -88, -86, -83, -81, -78, -76, -73,
   -71, -68, -66, -63, -60, -57, -55, -52,
   -49, -46, -43, -40, -37, -34, -31, -28,
   -25, -22, -19, -16, -12,  -9,  -6,  -3
};

const float radius1  = 65.2, radius2  = 92.0, radius3  = 163.2, radius4  = 176.8,
            centerx1 = 64.4, centerx2 = 46.4, centerx3 =  93.6, centerx4 =  16.4,
            centery1 = 34.8, centery2 = 26.0, centery3 =  56.0, centery4 = -11.6;
float       angle1   =  0.0, angle2   =  0.0, angle3   =   0.0, angle4   =   0.0;
long        hueShift =  0;
/*******************************************/

/*************** Night Mode ****************/
struct TimerObject{
  int hour, minute;
};

TimerObject clock_on  = { 7, 0};  //daytime mode start time
TimerObject clock_off = {22, 0};  //night time mode start time
/*******************************************/

int Power_Mode = 1;


/***************************
 * End Settings
 **************************/


#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)



 

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}
void setReadyForWeatherUpdate();

void setup() {


  Serial.begin(115200);


  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID,WIFI_PWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  display.begin();

  Serial.println();
  Serial.print("Width: ");
  Serial.println(display.width());
  Serial.print("Height: ");
  Serial.println(display.height());
  heap_caps_check_integrity_all(true);
  display.clearDisplay();
    Serial.print("Pixel draw latency in us: ");
  unsigned long start_timer=micros();
  display.drawPixel(1,1,0);
  unsigned long delta_timer=micros()-start_timer;
  Serial.println(delta_timer);
  Serial.print("Display update latency in us: ");
  start_timer=micros();
  display.clearDisplay();
  delta_timer=micros()-start_timer;
  Serial.println(delta_timer);

  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  updateNTP(); // Init the NTP time
  printTime(0); // print initial time time now.

  yield();

  // print each letter with a rainbow color
   display.setCursor(0,0);  
  display.setTextColor(display.color444(15,0,0));
  display.print('1');
  display.setTextColor(display.color444(15,4,0)); 
  display.print('6');
  display.setTextColor(display.color444(15,15,0));
  display.print('x');
  display.setTextColor(display.color444(8,15,0)); 
  display.print('3');
  display.setTextColor(display.color444(0,15,0));  
  display.print('2');
  display.setCursor(0,8);  
  display.setTextColor(display.color444(0,15,15)); 
  display.print("*");
  display.setTextColor(display.color444(0,8,15)); 
  display.print('R');
  display.setTextColor(display.color444(0,0,15));
  display.print('G');
  display.setTextColor(display.color444(8,0,15)); 
  display.print("B");
  display.setTextColor(display.color444(15,0,8)); 
  display.println("*");
  display.swapBuffer(false);
  delay(1000);




  heap_caps_check_integrity_all(true);

    Serial.println(String(ESP.getFreeHeap()));

  delay(100);

  Serial.println(String(ESP.getFreeHeap()));
  display.swapBuffer(false);
  display.clearDisplay();
  display.swapBuffer(false);
  display.clearDisplay();
  timeSinceLastWUpdate = millis()+1000L*UPDATE_INTERVAL_SECS;
}

void loop() {

if (millis() - modeSwitch > 20000UL) {	//Switch modes every 5 mins (12000UL)
      clock_mode++;
      mode_changed = 1;
      modeSwitch = millis();
      if (clock_mode > MAX_CLOCK_MODE - 1)
        clock_mode = 0;
      DEBUGp("Switch mode to ");
      DEBUGpln(clock_mode);
    }

    DEBUGp("in loop ");
    //reset clock type clock_mode
    switch (clock_mode) {
      case 0:
        actual_weather();
        break;
      case 1:
		  pong();
        break;
      case 2:
        actual_weather();
        break;
      case 3:
        normal_clock();
        break;
      case 4:
		pong();
        break;
      case 5:
        plasma();
        break;
      case 6:
        marquee();
        break;
      default:
        normal_clock();
        break;
    }

    //if the mode hasn't changed, show the date
   // pacClear();
    if (mode_changed == 0) {
      display_date();
      pacClear();
    }
    else {
      //the mode has changed, so don't bother showing the date, just go to the new mode.
      mode_changed = 0; //reset mode flag.
    }
}

//Runs pacman or other animation, refreshes weather data
void pacClear(){
	DEBUGpln("in pacClear");
	//refresh weather if we havent had it for 30 mins
	//or the last time we had it, it was bad,
	//or weve never had it before.
	if((millis()>lastWeatherTime+1800000) || lastWeatherTime==0)
	{
		lastWeatherTime=millis();
		getWeather();
	} 

	if(!wasWeatherShownLast){
		showWeather();
		wasWeatherShownLast = true;
		DEBUGpln("get Weather");
	}
	else{
		wasWeatherShownLast = false;
		pacMan();
	}
}


//*****************Weather Stuff*********************

void quickWeather(){
	getWeather();
  showWeather();
  yield();
	delay(500);
}

void getWeather(){
	DEBUGpln("in getWeather");
	updateData();
}

void showWeather(){
  byte dow = timeinfo->tm_wday;
  String WCopy;
  char dayname[4];
  DEBUGpln("in showWeather");
 
	for(int i = 0 ; i<MAX_FORECASTS; i++){
		WCopy=String(forecasts[i].temp, 0);
		DEBUGpln("HighTemp:");
		DEBUGpln(WCopy);
    DEBUGpln("Observationtime ");
    DEBUGp(forecasts[i].observationTimeText);
	DEBUGp(' ');
	DEBUGpln(forecasts[i].observationTime);
	DEBUGpln(fdow(forecasts[i].observationTime));
	DEBUGpln(forecasts[i].main);
	DEBUGpln(forecasts[i].rain);

		WCopy.toCharArray(w_temp[i],WCopy.length()+1);
		int numTemp = atoi(w_temp[i]);
		//fix within range to generate colour value
		if (numTemp<-14) numTemp=-10;
		if (numTemp>34) numTemp =30;
		//add 14 so it falls between 0 and 48
		numTemp = numTemp +14;
		//divide by 3 so value between 0 and 16
		numTemp = numTemp / 3;

		int tempColor;
		if(numTemp<8){
			tempColor = display.Color333(0,numTemp/2,7);
		}
		else{
			tempColor = display.Color333(7,(7-numTemp/2) ,0);
		}
    cls();
	  	String day= WDAY_NAMES[fdow(forecasts[i].observationTime)];
		DEBUGpln(day);
  		day.toUpperCase();
		day.toCharArray(dayname,3);
		//Display the day on the top line.
		if(i==0){ //TODAY
			drawString(2,2,(char*)"Nu",51,display.Color333(1,1,1));
		}
		else{
			drawString(2,2,dayname,51,display.Color333(0,1,0));
		}

		//put the temp underneath

		boolean positive = !(w_temp[i][0]=='-');
		for(int t=0; t<7; t++){
			if(w_temp[i][t]=='-'){
				display.drawLine(3,10,4,10,tempColor);
			}
			else if(!(w_temp[i][t]==0)){
				vectorNumber(w_temp[i][t]-'0',t*4+2+(positive*2),8,tempColor,1,1);
			}
		}
    display.swapBuffer(false);
    display.swapBuffer(true);
	WCopy=forecasts[i].iconMeteoCon;
	DEBUGpln(WCopy);
	WCopy.toCharArray(w_id[i],WCopy.length()+1);
	DEBUGpln(w_id[i][0]);
	drawWeatherIcon(16,0,w_id[i][0],forecasts[i].rain,forecasts[i].clouds);
	}
}

void drawWeatherIcon(uint8_t x, uint8_t y, char id,uint8_t i_rain, uint8_t i_cloud){
	unsigned long start = millis();
	static int rain[12];

	for(int r=0; r<13; r++){
		//rain[r]=random(9,18);
		rain[r]=random(9,15);
	}
	int rainColor = display.Color333(0,0,1);
	byte intensity=i_rain; //mm per 3h
	if (intensity>9)
		intensity=9;

	int deep =0;
	boolean raining = false;
	DEBUGp("in drawWeatherIcon... ");
	DEBUGp(id);
	DEBUGp(' ');
	DEBUGp(intensity);
	DEBUGp(' ');
	DEBUGpln(i_cloud);
	//translate night->day
	if (id=='C')
		id='B';
	if (id=='4')
		id='H';
	if (id=='5')
		id='N';
	if (id=='%')
		id='Y';
	if (id=='8')
		id='R';
	if (id=='7')
		id='Q';
	if (id=='6')
		id='P';
	if (id=='#')
		id='W';

	while(millis()<start+2000){
		switch(id){
		case 'P':
			//Thunder
			display.fillRect(x,y,16,16,display.Color333(0,0,0));
			display.drawBitmap(x,y,cloud_outline,16,16,display.Color333(1,1,1));
			if(random(0,10)==3){
				int pos = random(-5,5);
				display.drawBitmap(pos+x,y,lightning,16,16,display.Color333(1,1,1));
			}
			raining = true;
			break;
		case 'Q':
			//drizzle
			display.fillRect(x,y,16,16,display.Color333(0,0,0));
			display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
			raining=true;
			intensity=1;
			break;
		case 'R':
			//rain
			display.fillRect(x,y,16,16,display.Color333(0,0,0));

			if(intensity<3){
				display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
			}
			else{
				display.drawBitmap(x,y,cloud_outline,16,16,display.Color333(1,1,1));
			}
			raining = true;
			break;
		case 'W':
			//snow
			rainColor = display.Color333(4,4,4);
			display.fillRect(x,y,16,16,display.Color333(0,0,0));

			deep = (millis()-start)/500;
			if(deep>6) deep=6;

			if(intensity<3){
				display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
				display.fillRect(x,y+16-deep/2,16,deep/2,rainColor);
			}
			else{
				display.drawBitmap(x,y,cloud_outline,16,16,display.Color333(1,1,1));
				display.fillRect(x,y+16-(deep),16,deep,rainColor);
			}
			raining = true;
			break;
		case 'M':
			//atmosphere
			display.drawRect(x,y,16,16,display.Color333(1,0,0));
			drawString(x+2,y+6,(char*)"FOG",51,display.Color333(1,1,1));
			break;
		case 'Y':
			//cloud
			display.fillRect(x,y,16,16,display.Color333(0,0,1));
			if(id==800){
				display.drawBitmap(x,y,big_sun,16,16,display.Color333(2,2,0));
			}
			else{
				if(i_cloud<20){
					display.drawBitmap(x,y,big_sun,16,16,display.Color333(2,2,0));
					display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
				}
				else{
					if(i_cloud>=20){
						display.drawBitmap(x,y,small_sun,16,16,display.Color333(1,1,0));
					}
					display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
					display.drawBitmap(x,y,cloud_outline,16,16,display.Color333(0,0,0));
				}
			}
			break;
		case 'X':
			//extreme
			display.fillRect(x,y,16,16,display.Color333(0,0,0));
			display.drawRect(x,y,16,16,display.Color333(7,0,0));
			if(id==906){
				raining =true;
				intensity=3;
				display.drawBitmap(x,y,cloud,16,16,display.Color333(1,1,1));
			};
			break;
		default:
			display.fillRect(x,y,16,16,display.Color333(0,1,1));
			display.drawBitmap(x,y,big_sun,16,16,display.Color333(2,2,0));
			break;
		}
		if(raining){
			for(int r = 0; r<13; r++){
				display.drawPixel(x+r+2, rain[r]++, rainColor);
				if(rain[r]==16) rain[r]=9;
				//if(rain[r]==20) rain[r]=9;
			}
		}
		display.swapBuffer(false);
		yield();	//Give the background process some lovin'
		delay(( 50 -( intensity * 10 )) < 0 ? 0: 50-intensity*10);
	}
}
//*****************End Weather Stuff*********************


void scrollBigMessage(char *m){
	display.setTextSize(1);
	int l = (strlen(m)*-6) - (X_MAX+1);
	for(int i = X_MAX+1; i > l; i--){
		cls();
		display.setCursor(i,1);
		display.setTextColor(display.Color333(2,2,2));
		display.print(m);
		display.swapBuffer(false);
		delay(50);
		yield();
	}
}

void scrollMessage(char* top, char* bottom ,uint8_t top_font_size,uint8_t bottom_font_size, uint16_t top_color, uint16_t bottom_color){

	int l = ((strlen(top)>strlen(bottom)?strlen(top):strlen(bottom))*-5) - 32;

	for(int i=32; i > l; i--){

		if (mode_changed == 1 || mode_quick)
			return;

		cls();

		drawString(i,2,top,top_font_size, top_color);
		drawString(i,10,bottom, bottom_font_size, bottom_color);
		display.swapBuffer(false);
		delay(50);
		yield();
	}

}

void pacMan(){
#if defined (usePACMAN)
	DEBUGpln("in pacMan");
	if(powerPillEaten>0){
		for(int i =(X_MAX+1)+(powerPillEaten*17); i>-17; i--){
			long nowish = millis();
			cls();
    
			drawPac(i,0,-1);
			if(powerPillEaten>0) drawScaredGhost(i-17,0);
			if(powerPillEaten>1) drawScaredGhost(i-34,0);
			if(powerPillEaten>2) drawScaredGhost(i-51,0);
			if(powerPillEaten>3) drawScaredGhost(i-68,0);

			display.swapBuffer(false);
			while(millis()-nowish<50) yield();	//Give the background process some lovin'
		}
		powerPillEaten = 0;
	}
	else{

		int hasEaten = 0;

		int powerPill = random(0,5);
		int numGhosts=random(0,4);
		if(powerPill ==0){
			if(numGhosts==0) numGhosts++;
			powerPillEaten = numGhosts;
		}

		for(int i=-17; i<(X_MAX+1)+(numGhosts*17); i++){
			cls();
      long nowish = millis();
			for(int j = 0; j<(X_MAX/5);j++){

				if( j*5> i){
					if(powerPill==0 && j==4){
						display.fillCircle(j*5,8,2,display.Color333(7,3,0));
					}
					else{
						display.fillRect(j*5,8,2,2,display.Color333(7,3,0));
					}
				}
			}

			if(i==19 && powerPill == 0) hasEaten=1;
			drawPac(i,0,1);
			if(hasEaten == 0){
				if(numGhosts>0) drawGhost(i-17,0,display.Color333(3,0,3));
				if(numGhosts>1) drawGhost(i-34,0,display.Color333(3,0,0));
				if(numGhosts>2) drawGhost(i-51,0,display.Color333(0,3,3));
				if(numGhosts>3) drawGhost(i-68,0,display.Color333(7,3,0));
			}
			else{
				if(numGhosts>0) drawScaredGhost(i-17-(i-19)*2,0);
				if(numGhosts>1) drawScaredGhost(i-34-(i-19)*2,0);
				if(numGhosts>2) drawScaredGhost(i-51-(i-19)*2,0);
				if(numGhosts>3) drawScaredGhost(i-68-(i-19)*2,0);
			}
			display.swapBuffer(false);
			while(millis()-nowish<50) yield();	//Give the background process some lovin'
		}
	}
#endif //usePACMAN
}

#if defined (usePACMAN)
void drawPac(int x, int y, int z){
	int c = display.Color333(3,3,0);
	if(x>-16 && x<(X_MAX+1)){
		if(abs(x)%4==0){
			display.drawBitmap(x,y,(z>0?pac:pac_left),16,16,c);
		}
		else if(abs(x)%4==1 || abs(x)%4==3){
			display.drawBitmap(x,y,(z>0?pac2:pac_left2),16,16,c);
		}
		else{
			display.drawBitmap(x,y,(z>0?pac3:pac_left3),16,16,c);
		}
	}
}

void drawGhost( int x, int y, int color){
	if(x>-16 && x<(X_MAX+1)){
		if(abs(x)%8>3){
			display.drawBitmap(x,y,blinky,16,16,color);
		}
		else{
			display.drawBitmap(x,y,blinky2,16,16,color);
		}
		display.drawBitmap(x,y,eyes1,16,16,display.Color333(3,3,3));
		display.drawBitmap(x,y,eyes2,16,16,display.Color333(0,0,7));
	}
}

void drawScaredGhost( int x, int y){
	if(x>-16 && x<(X_MAX+1)){
		if(abs(x)%8>3){
			display.drawBitmap(x,y,blinky,16,16,display.Color333(0,0,7));
		}
		else{
			display.drawBitmap(x,y,blinky2,16,16,display.Color333(0,0,7));
		}
		display.drawBitmap(x,y,scared,16,16,display.Color333(7,3,2));
	}
}
#endif  //usePACMAN


void cls(){
	  display.clearDisplay(); //
}
void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void pong(){
	DEBUGpln("in Pong");
	display.setTextSize(1);
	display.setTextColor(display.Color333(2, 2, 2));

	float ballpos_x, ballpos_y;
	float ballvel_x, ballvel_y;
	int bat1_y = 5;  //bat starting y positions
	int bat2_y = 5;
	int bat1_target_y = 5;  //bat targets for bats to move to
	int bat2_target_y = 5;
	byte bat1_update = 1;  //flags - set to update bat position
	byte bat2_update = 1;
	byte bat1miss, bat2miss; //flags set on the minute or hour that trigger the bats to miss the ball, thus upping the score to match the time.
	byte restart = 1;   //game restart flag - set to 1 initially to setup 1st game

	cls();
//loop to display the clock for a set duration of SHOWCLOCK
	for (int show = 0; show < SHOWCLOCK ; show++)
  {
	long showTime = millis();
    now = dstAdjusted.time(&dstAbbrev);
    timeinfo = localtime (&now);
    while((millis() - showTime) < 6*showClock)
	{
		cls();
		//draw pitch centre line
		int adjust = 0;
		if(timeinfo->tm_sec%2==0)adjust=1;
		for (byte i = 0; i < (Y_MAX+1); i++) {
			if ( i % 2 == 0 ) { //plot point if an even number
				display.drawPixel((X_MAX+1)/2,i+adjust,display.Color333(0,4,0));
			}
		}

		//main pong game loop
		if (mode_changed == 1)
		return;
		if(mode_quick){
			mode_quick = false;
			display_date();
			quickWeather();
			pong();
			return;
		}

		int ampm=0;
		//update score / time
		byte mins =  timeinfo->tm_min;   
		byte hours = timeinfo->tm_hour;
		if (hours > 12) {
			hours = hours - ampm * 12;
		}
		if (hours < 1) {
			hours = hours + ampm * 12;
		}

		char buffer[3];

		itoa(hours,buffer,10);
		//fix - as otherwise if num has leading zero, e.g. "03" hours, itoa coverts this to chars with space "3 ".
		if (hours < 10) {
			buffer[1] = buffer[0];
			buffer[0] = '0';
		}
		vectorNumber(buffer[0]-'0',((X_MAX+1)/2-2*4),1,display.Color333(1,1,1),1,1);
		vectorNumber(buffer[1]-'0',((X_MAX+1)/2-1*4),1,display.Color333(1,1,1),1,1);

		itoa(mins,buffer,10);
		if (mins < 10) {
			buffer[1] = buffer[0];
			buffer[0] = '0';
		}
		vectorNumber(buffer[0]-'0',((X_MAX+1)/2+2),1,display.Color333(1,1,1),1,1);
		vectorNumber(buffer[1]-'0',((X_MAX+1)/2+6),1,display.Color333(1,1,1),1,1);

		//if restart flag is 1, setup a new game
		if (restart) {
			//set ball start pos
			ballpos_x = (X_MAX+1)/2;
			ballpos_y = random (4,Y_MAX/2);

			//pick random ball direction
			if (random(0,2) > 0) {
				ballvel_x = 1;
			}
			else {
				ballvel_x = -1;
			}
			if (random(0,2) > 0) {
				ballvel_y = 0.5;
			}
			else {
				ballvel_y = -0.5;
			}
			//draw bats in initial positions
			bat1miss = 0;
			bat2miss = 0;
			//reset game restart flag
			restart = 0;
		}

		//if coming up to the minute: secs = 59 and mins < 59, flag bat 2 (right side) to miss the return so we inc the minutes score
		if (timeinfo->tm_sec == 59 && timeinfo->tm_min < 59){
			bat1miss = 1;
		}
		// if coming up to the hour: secs = 59  and mins = 59, flag bat 1 (left side) to miss the return, so we inc the hours score.
		if (timeinfo->tm_sec == 59 && timeinfo->tm_min == 59){
			bat2miss = 1;
		}

		//AI - we run 2 sets of 'AI' for each bat to work out where to go to hit the ball back
		//very basic AI...
		// For each bat, First just tell the bat to move to the height of the ball when we get to a random location.
		//for bat1
		if (ballpos_x == random(((X_MAX+1)/2)+2,X_MAX+1)){
			bat1_target_y = ballpos_y;
		}
		//for bat2
		if (ballpos_x == random(4,(X_MAX+1)/2)){
			bat2_target_y = ballpos_y;
		}

		//when the ball is closer to the left bat, run the ball maths to find out where the ball will land
		if (ballpos_x == (((X_MAX+1)/2)-1) && ballvel_x < 0) {

			byte end_ball_y = pong_get_ball_endpoint(ballpos_x, ballpos_y, ballvel_x, ballvel_y);

			//if the miss flag is set,  then the bat needs to miss the ball when it gets to end_ball_y
			if (bat1miss == 1){
				bat1miss = 0;
				if ( end_ball_y > (Y_MAX+1)/2){
					bat1_target_y = random (0,(Y_MAX+1)/2);
				}
				else {
					bat1_target_y = (Y_MAX+1)/2 + random (0,3);
				}
			}
			//if the miss flag isn't set,  set bat target to ball end point with some randomness so its not always hitting top of bat
			else {
				bat1_target_y = end_ball_y - random (0, 6);
				//check not less than 0
				if (bat1_target_y < 0){
					bat1_target_y = 0;
				}
				if (bat1_target_y > (Y_MAX+1)-6){
					bat1_target_y = (Y_MAX+1)-6;
				}
			}
		}

		//right bat AI
		//if positive velocity then predict for right bat - first just match ball height
		//when the ball is closer to the right bat, run the ball maths to find out where it will land
		if (ballpos_x == (((X_MAX+1)/2)+1) && ballvel_x > 0) {

			byte end_ball_y = pong_get_ball_endpoint(ballpos_x, ballpos_y, ballvel_x, ballvel_y);

			//if flag set to miss, move bat out way of ball
			if (bat2miss == 1){
				bat2miss = 0;
				//if ball end point above 8 then move bat down, else move it up- so either way it misses
				if (end_ball_y > (Y_MAX+1)/2){
					bat2_target_y = random (0,3);
				}
				else {
					bat2_target_y = (Y_MAX+1)/2 + random (0,3);
				}
			}
			else {
				//set bat target to ball end point with some randomness
				bat2_target_y =  end_ball_y - random (0,6);
				//ensure target between 0 and 15
				if (bat2_target_y < 0){
					bat2_target_y = 0;
				}
				if (bat2_target_y > (Y_MAX-5)){
					bat2_target_y = (Y_MAX-5);
				}
			}
		}

		//move bat 1 towards target
		//if bat y greater than target y move down until hit 0 (dont go any further or bat will move off screen)
		if (bat1_y > bat1_target_y && bat1_y > 0 ) {
			bat1_y--;
			bat1_update = 1;
		}

		//if bat y less than target y move up until hit 10 (as bat is 6)
		if (bat1_y < bat1_target_y && bat1_y < 10) {
			bat1_y++;
			bat1_update = 1;
		}

		//draw bat 1
		if (bat1_update){
			display.fillRect(BAT1_X-1,bat1_y,2,6,display.Color333(0,0,4));
		}

		//move bat 2 towards target (dont go any further or bat will move off screen)
		//if bat y greater than target y move down until hit 0
		if (bat2_y > bat2_target_y && bat2_y > 0 ) {
			bat2_y--;
			bat2_update = 1;
		}

		//if bat y less than target y move up until hit max of 10 (as bat is 6)
		if (bat2_y < bat2_target_y && bat2_y < (Y_MAX-5)) {
			bat2_y++;
			bat2_update = 1;
		}

		//draw bat2
		if (bat2_update){
			display.fillRect(BAT2_X+1,bat2_y,2,6,display.Color333(0,0,4));
		}

		//update the ball position using the velocity
		ballpos_x =  ballpos_x + ballvel_x;
		ballpos_y =  ballpos_y + ballvel_y;

		//check ball collision with top and bottom of screen and reverse the y velocity if either is hit
		if (ballpos_y <= 0 ){
			ballvel_y = ballvel_y * -1;
			ballpos_y = 0; //make sure value goes no less that 0
		}

		if (ballpos_y >= Y_MAX){
			ballvel_y = ballvel_y * -1;
			ballpos_y = Y_MAX; //make sure value goes no more than 15
		}

		//check for ball collision with bat1. check ballx is same as batx
		//and also check if bally lies within width of bat i.e. baty to baty + 6. We can use the exp if(a < b && b < c)
		if ((int)ballpos_x == BAT1_X+1 && (bat1_y <= (int)ballpos_y && (int)ballpos_y <= bat1_y + 5) ) {

			//random if bat flicks ball to return it - and therefor changes ball velocity
			if(!random(0,3)) { //not true = no flick - just straight rebound and no change to ball y vel
				ballvel_x = ballvel_x * -1;
			}
			else {
				bat1_update = 1;
				byte flick;  //0 = up, 1 = down.

				if (bat1_y > 1 || bat1_y < (Y_MAX+1)/2){
					flick = random(0,2);   //pick a random dir to flick - up or down
				}

				//if bat 1 or 2 away from top only flick down
				if (bat1_y <=1 ){
					flick = 0;   //move bat down 1 or 2 pixels
				}
				//if bat 1 or 2 away from bottom only flick up
				if (bat1_y >=  (Y_MAX+1)/2 ){
					flick = 1;  //move bat up 1 or 2 pixels
				}

				switch (flick) {
					//flick up
				case 0:
					bat1_target_y = bat1_target_y + random(1,3);
					ballvel_x = ballvel_x * -1;
					if (ballvel_y < 2) {
						ballvel_y = ballvel_y + 0.2;
					}
					break;

					//flick down
				case 1:
					bat1_target_y = bat1_target_y - random(1,3);
					ballvel_x = ballvel_x * -1;
					if (ballvel_y > 0.2) {
						ballvel_y = ballvel_y - 0.2;
					}
					break;
				}
			}
		}

		//check for ball collision with bat2. check ballx is same as batx
		//and also check if bally lies within width of bat i.e. baty to baty + 6. We can use the exp if(a < b && b < c)
		if ((int)ballpos_x == BAT2_X && (bat2_y <= (int)ballpos_y && (int)ballpos_y <= bat2_y + 5) ) {

			//random if bat flicks ball to return it - and therefor changes ball velocity
			if(!random(0,3)) {
				ballvel_x = ballvel_x * -1;    //not true = no flick - just straight rebound and no change to ball y vel
			}
			else {
				bat1_update = 1;
				byte flick;  //0 = up, 1 = down.

				if (bat2_y > 1 || bat2_y < (Y_MAX+1)/2){
					flick = random(0,2);   //pick a random dir to flick - up or down
				}
				//if bat 1 or 2 away from top only flick down
				if (bat2_y <= 1 ){
					flick = 0;  //move bat up 1 or 2 pixels
				}
				//if bat 1 or 2 away from bottom only flick up
				if (bat2_y >=  (Y_MAX+1)/2 ){
					flick = 1;   //move bat down 1 or 2 pixels
				}

				switch (flick) {
					//flick up
				case 0:
					bat2_target_y = bat2_target_y + random(1,3);
					ballvel_x = ballvel_x * -1;
					if (ballvel_y < 2) {
						ballvel_y = ballvel_y + 0.2;
					}
					break;

					//flick down
				case 1:
					bat2_target_y = bat2_target_y - random(1,3);
					ballvel_x = ballvel_x * -1;
					if (ballvel_y > 0.2) {
						ballvel_y = ballvel_y - 0.2;
					}
					break;
				}
			}
		}

		//plot the ball on the screen
		byte plot_x = (int)(ballpos_x + 0.5f);
		byte plot_y = (int)(ballpos_y + 0.5f);

		display.drawPixel(plot_x,plot_y,display.Color333(4, 0, 0));

		//check if a bat missed the ball. if it did, reset the game.
		if ((int)ballpos_x == 0 ||(int) ballpos_x == (X_MAX+1)){
			restart = 1;
		}
		display.swapBuffer(false);
		yield();	//Give the background process some lovin'
		delay(40);

	}
  }
}
byte pong_get_ball_endpoint(float tempballpos_x, float  tempballpos_y, float  tempballvel_x, float tempballvel_y) {

	//run prediction until ball hits bat
	while (tempballpos_x > BAT1_X && tempballpos_x < BAT2_X  ){
		tempballpos_x = tempballpos_x + tempballvel_x;
		tempballpos_y = tempballpos_y + tempballvel_y;
		//check for collisions with top / bottom
		if (tempballpos_y <= 0 || tempballpos_y >= 15){
			tempballvel_y = tempballvel_y * -1;
		}
	}
	return tempballpos_y;
}

void actual_weather()
{
	// displays 
	DEBUGpln("in actual weather");
	cls();
	display.setTextWrap(false); // Allow text to run off right edge
	display.setTextSize(1);
	display.setTextColor(myCYAN);
    String tempd= String(currentWeather.temp, 1) ;
    String windSpeed = String(currentWeather.windSpeed , 1);
	display.setCursor(0,0);
	display.print(tempd);
	display.print(char(247));
	display.print('C');
	display.setCursor(0,8);
	display.setTextColor(myRED);
	display.print(windSpeed);
	display.swapBuffer(false);
	delay(5000);
}

void normal_clock()
{
	DEBUGpln("in normal_clock");
	display.setTextWrap(false); // Allow text to run off right edge
	display.setTextSize(2);
	display.setTextColor(display.Color333(2, 3, 2));

	cls();
	byte hours = timeinfo->tm_hour;
	byte mins = timeinfo->tm_min;


	int  msHourPosition = 0;
	int  lsHourPosition = 0;
	int  msMinPosition = 0;
	int  lsMinPosition = 0;
	int  msLastHourPosition = 0;
	int  lsLastHourPosition = 0;
	int  msLastMinPosition = 0;
	int  lsLastMinPosition = 0;

	//Start with all characters off screen
	int c1 = -17;
	int c2 = -17;
	int c3 = -17;
	int c4 = -17;

	float scale_x =2;
	float scale_y =3;

	char lastHourBuffer[3]="  ";
	char lastMinBuffer[3] ="  ";

	long showTime = millis();

    while((millis() - showTime) < 600*SHOWCLOCK)
	{
    now = dstAdjusted.time(&dstAbbrev);
    timeinfo = localtime (&now);

		cls();

		if (mode_changed == 1)
		return;
		if(mode_quick){
			mode_quick = false;
			display_date();
			quickWeather();
			normal_clock();
			return;
		}

		//udate mins and hours with the new time

		mins = timeinfo->tm_min;
		hours = timeinfo->tm_hour;
		char buffer[3];

		itoa(hours,buffer,10);
		//fix - as otherwise if num has leading zero, e.g. "03" hours, itoa coverts this to chars with space "3 ".
		if (hours < 10) {
			buffer[1] = buffer[0];
			buffer[0] = '0';
		}

		if(lastHourBuffer[0]!=buffer[0] && c1==0) c1= -17;
		if( c1 < 0 )c1++;
		msHourPosition = c1;
		msLastHourPosition = c1 + 17;

		if(lastHourBuffer[1]!=buffer[1] && c2==0) c2= -17;
		if( c2 < 0 )c2++;
		lsHourPosition = c2;
		lsLastHourPosition = c2 + 17;

		//update the display
		//shadows first
		vectorNumber((lastHourBuffer[0]-'0'), 2, 2+msLastHourPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((lastHourBuffer[1]-'0'), 9, 2+lsLastHourPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((buffer[0]-'0'), 2, 2+msHourPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((buffer[1]-'0'), 9, 2+lsHourPosition, display.Color333(0,0,1),scale_x,scale_y);

		vectorNumber((lastHourBuffer[0]-'0'), 1, 1+msLastHourPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((lastHourBuffer[1]-'0'), 8, 1+lsLastHourPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((buffer[0]-'0'), 1, 1+msHourPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((buffer[1]-'0'), 8, 1+lsHourPosition, display.Color333(1,1,1),scale_x,scale_y);


		if(c1==0) lastHourBuffer[0]=buffer[0];
		if(c2==0) lastHourBuffer[1]=buffer[1];

		display.fillRect(16,5,2,2,display.Color333(0,0,timeinfo->tm_sec%2));
		display.fillRect(16,11,2,2,display.Color333(0,0,timeinfo->tm_sec%2));

		display.fillRect(15,4,2,2,display.Color333(timeinfo->tm_sec%2,timeinfo->tm_sec%2,timeinfo->tm_sec%2));
		display.fillRect(15,10,2,2,display.Color333(timeinfo->tm_sec%2,timeinfo->tm_sec%2,timeinfo->tm_sec%2));

		itoa (mins, buffer, 10);
		if (mins < 10) {
			buffer[1] = buffer[0];
			buffer[0] = '0';
		}

		if(lastMinBuffer[0]!=buffer[0] && c3==0) c3= -17;

		if( c3 < 0 )c3++;
		msMinPosition = c3;
		msLastMinPosition= c3 + 17;

		if(lastMinBuffer[1]!=buffer[1] && c4==0) c4= -17;
		if( c4 < 0 )c4++;
		lsMinPosition = c4;
		lsLastMinPosition = c4 + 17;

		vectorNumber((buffer[0]-'0'), 19, 2+msMinPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((buffer[1]-'0'), 26, 2+lsMinPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((lastMinBuffer[0]-'0'), 19, 2+msLastMinPosition, display.Color333(0,0,1),scale_x,scale_y);
		vectorNumber((lastMinBuffer[1]-'0'), 26, 2+lsLastMinPosition, display.Color333(0,0,1),scale_x,scale_y);

		vectorNumber((buffer[0]-'0'), 18, 1+msMinPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((buffer[1]-'0'), 25, 1+lsMinPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((lastMinBuffer[0]-'0'), 18, 1+msLastMinPosition, display.Color333(1,1,1),scale_x,scale_y);
		vectorNumber((lastMinBuffer[1]-'0'), 25, 1+lsLastMinPosition, display.Color333(1,1,1),scale_x,scale_y);

		if(c3==0) lastMinBuffer[0]=buffer[0];
		if(c4==0) lastMinBuffer[1]=buffer[1];

		display.swapBuffer(false);
		yield();	//Give the background process some lovin'
		delay(50);
	}
}

//Draw number n, with x,y as top left corner, in chosen color, scaled in x and y.
//when scale_x, scale_y = 1 then character is 3x5
void vectorNumber(int n, int x, int y, int color, float scale_x, float scale_y){

	switch (n){
	case 0:
		display.drawLine(x ,y , x , y+(4*scale_y) , color);
		display.drawLine(x , y+(4*scale_y) , x+(2*scale_x) , y+(4*scale_y), color);
		display.drawLine(x+(2*scale_x) , y , x+(2*scale_x) , y+(4*scale_y) , color);
		display.drawLine(x ,y , x+(2*scale_x) , y , color);
		break;
	case 1:
		display.drawLine( x+(1*scale_x), y, x+(1*scale_x),y+(4*scale_y), color);
		display.drawLine(x , y+4*scale_y , x+2*scale_x , y+4*scale_y,color);
		display.drawLine(x,y+scale_y, x+scale_x, y,color);
		break;
	case 2:
		display.drawLine(x ,y , x+2*scale_x , y , color);
		display.drawLine(x+2*scale_x , y , x+2*scale_x , y+2*scale_y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		display.drawLine(x , y+2*scale_y, x , y+4*scale_y,color);
		display.drawLine(x , y+4*scale_y , x+2*scale_x , y+4*scale_y,color);
		break;
	case 3:
		display.drawLine(x ,y , x+2*scale_x , y , color);
		display.drawLine(x+2*scale_x , y , x+2*scale_x , y+4*scale_y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x+scale_x , y+2*scale_y, color);
		display.drawLine(x , y+4*scale_y , x+2*scale_x , y+4*scale_y,color);
		break;
	case 4:
		display.drawLine(x+2*scale_x , y , x+2*scale_x , y+4*scale_y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		display.drawLine(x ,y , x , y+2*scale_y , color);
		break;
	case 5:
		display.drawLine(x ,y , x+2*scale_x , y , color);
		display.drawLine(x , y , x , y+2*scale_y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		display.drawLine(x+2*scale_x , y+2*scale_y, x+2*scale_x , y+4*scale_y,color);
		display.drawLine( x , y+4*scale_y , x+2*scale_x , y+4*scale_y,color);
		break;
	case 6:
		display.drawLine(x ,y , x , y+(4*scale_y) , color);
		display.drawLine(x ,y , x+2*scale_x , y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		display.drawLine(x+2*scale_x , y+2*scale_y, x+2*scale_x , y+4*scale_y,color);
		display.drawLine(x+2*scale_x , y+4*scale_y , x, y+(4*scale_y) , color);
		break;
	case 7:
		display.drawLine(x ,y , x+2*scale_x , y , color);
		display.drawLine( x+2*scale_x, y, x+scale_x,y+(4*scale_y), color);
		break;
	case 8:
		display.drawLine(x ,y , x , y+(4*scale_y) , color);
		display.drawLine(x , y+(4*scale_y) , x+(2*scale_x) , y+(4*scale_y), color);
		display.drawLine(x+(2*scale_x) , y , x+(2*scale_x) , y+(4*scale_y) , color);
		display.drawLine(x ,y , x+(2*scale_x) , y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		break;
	case 9:
		display.drawLine(x ,y , x , y+(2*scale_y) , color);
		display.drawLine(x , y+(4*scale_y) , x+(2*scale_x) , y+(4*scale_y), color);
		display.drawLine(x+(2*scale_x) , y , x+(2*scale_x) , y+(4*scale_y) , color);
		display.drawLine(x ,y , x+(2*scale_x) , y , color);
		display.drawLine(x+2*scale_x , y+2*scale_y , x , y+2*scale_y, color);
		break;
	}
}


/*
* flashing_cursor
* print a flashing_cursor at xpos, ypos and flash it repeats times
*/
void flashing_cursor(byte xpos, byte ypos, byte cursor_width, byte cursor_height, byte repeats)
{

	for (byte r = 0; r <= repeats; r++) {
		display.fillRect(xpos,ypos,cursor_width, cursor_height, display.Color333(0,3,0));
		display.swapBuffer(true);

		if (repeats > 0) {
			delay(400);
		}
		else {
			delay(70);
		}

		display.fillRect(xpos,ypos,cursor_width, cursor_height, display.Color333(0,0,0));
		display.swapBuffer(true);

		//if cursor set to repeat, wait a while
		if (repeats > 0) {
			delay(400);
		}
		yield();	//Give the background process some lovin'
	}
}


void drawString(int x, int y, char* c,uint8_t font_size, uint16_t color)
{
	// x & y are positions, c-> pointer to string to disp, update_s: false(write to mem), true: write to disp
	//font_size : 51(ascii value for 3), 53(5) and 56(8)
	for(uint16_t i=0; i< strlen(c); i++)
	{
		drawChar(x, y, c[i],font_size, color);
		x+=calc_font_displacement(font_size); // Width of each glyph
	}
}

int calc_font_displacement(uint8_t font_size)
{
	switch(font_size)
	{
	case 51:
		return 4;  //5x3 hence occupies 4 columns ( 3 + 1(space btw two characters))
		break;

	case 53:
		return 6;
		break;
		//case 56:
		//return 6;
		//break;
	default:
		return 6;
		break;
	}
}

void drawChar(int x, int y, char c, uint8_t font_size, uint16_t color)  // Display the data depending on the font size mentioned in the font_size variable
{

	uint8_t dots;
	if ((c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z')) {
		c &= 0x1F;   // A-Z maps to 1-26
	}
	else if (c >= '0' && c <= '9') {
		c = (c - '0') + 27;
	}
	else if (c == ' ') {
		c = 0; // space
	}
	else if (c == '#'){
		c=37;
	}
	else if (c=='/'){
		c=37;
	}

	switch(font_size)
	{
	case 51:  // font size 3x5  ascii value of 3: 51

		if(c==':'){
			display.drawPixel(x+1,y+1,color);
			display.drawPixel(x+1,y+3,color);
		}
		else if(c=='-'){
			display.drawLine(x,y+2,3,0,color);
		}
		else if(c=='.'){
			display.drawPixel(x+1,y+2,color);
		}
		else if(c==39 || c==44){
			display.drawLine(x+1,y,2,0,color);
			display.drawPixel(x+2,y+1,color);
		}
		else{
			for (uint8_t row=0; row< 5; row++) {
				dots = font3x5[(uint8_t)c][row];
				for (uint8_t col=0; col < 3; col++) {
					int x1=x;
					int y1=y;
					if (dots & (4>>col))
					display.drawPixel(x1+col, y1+row, color);
				}
			}
		}
		break;

	case 53:  // font size 5x5   ascii value of 5: 53

		if(c==':'){
			display.drawPixel(x+2,y+1,color);
			display.drawPixel(x+2,y+3,color);
		}
		else if(c=='-'){
			display.drawLine(x+1,y+2,3,0,color);
		}
		else if(c=='.'){
			display.drawPixel(x+2,y+2,color);
		}
		else if(c==39 || c==44){
			display.drawLine(x+2,y,2,0,color);
			display.drawPixel(x+4,y+1,color);
		}
		else{
			for (uint8_t row=0; row< 5; row++) {
				dots = font5x5[(uint8_t)c][row];
				for (uint8_t col=0; col < 5; col++) {
					int x1=x;
					int y1=y;
					if (dots & (64>>col))  // For some wierd reason I have the 5x5 font in such a way that.. last two bits are zero..
					display.drawPixel(x1+col, y1+row, color);
				}
			}
		}

		break;
	default:
		break;
	}
}

void plasma()
{
	int           x1, x2, x3, x4, y1, y2, y3, y4, sx1, sx2, sx3, sx4;
	unsigned char x, y;
	long          value;
	unsigned long slowFrameRate = millis();

	cls();
	DEBUGpln("In plasma");
	for (int show = 0; show < SHOWCLOCK ; show++) {
	long showTime = millis();
    now = dstAdjusted.time(&dstAbbrev);
    timeinfo = localtime (&now);
    while((millis() - showTime) < 6*showClock)
	{
		if (mode_changed == 1)
		return;
		if(mode_quick){
			mode_quick = false;
			display_date();
			quickWeather();
			return;
		}

		if (millis() - slowFrameRate >= 150) {   //FB was 150
			sx1 = (int)(cos(angle1) * radius1 + centerx1);
			sx2 = (int)(cos(angle2) * radius2 + centerx2);
			sx3 = (int)(cos(angle3) * radius3 + centerx3);
			sx4 = (int)(cos(angle4) * radius4 + centerx4);
			y1  = (int)(sin(angle1) * radius1 + centery1);
			y2  = (int)(sin(angle2) * radius2 + centery2);
			y3  = (int)(sin(angle3) * radius3 + centery3);
			y4  = (int)(sin(angle4) * radius4 + centery4);

			for(y=0; y<(display.height()); y++) {
				x1 = sx1; x2 = sx2; x3 = sx3; x4 = sx4;
				for(x=0; x<display.width(); x++) {
					value = hueShift
					+ (int8_t)pgm_read_byte(sinetab + (uint8_t)((x1 * x1 + y1 * y1) >> 4))
					+ (int8_t)pgm_read_byte(sinetab + (uint8_t)((x2 * x2 + y2 * y2) >> 4))
					+ (int8_t)pgm_read_byte(sinetab + (uint8_t)((x3 * x3 + y3 * y3) >> 5))
					+ (int8_t)pgm_read_byte(sinetab + (uint8_t)((x4 * x4 + y4 * y4) >> 5));
					display.drawPixel(x, y, display.ColorHSV(value * 3, 255, 255, true));
					x1--; x2--; x3--; x4--;
				}
				y1--; y2--; y3--; y4--;
			}

			angle1 += 0.03;
			angle2 -= 0.07;
			angle3 += 0.13;
			angle4 -= 0.15;
			hueShift += 2;

			display.fillRect(7, 0, 19, 7, 0);

			int mins = timeinfo->tm_min;
			int hours = timeinfo->tm_hour;
			char buffer[3];

			itoa(hours,buffer,10);
			//fix - as otherwise if num has leading zero, e.g. "03" hours, itoa coverts this to chars with space "3 ".
			if (hours < 10) {
				buffer[1] = buffer[0];
				buffer[0] = '0';
			}
			vectorNumber(buffer[0]-'0',8,1,display.Color333(229,0,0),1,1);
			vectorNumber(buffer[1]-'0',12,1,display.Color333(229,0,0),1,1);

			itoa(mins,buffer,10);
			//fix - as otherwise if num has leading zero, e.g. "03" hours, itoa coverts this to chars with space "3 ".
			if (mins < 10) {
				buffer[1] = buffer[0];
				buffer[0] = '0';
			}
			vectorNumber(buffer[0]-'0',18,1,display.Color333(229,0,0),1,1);
			vectorNumber(buffer[1]-'0',22,1,display.Color333(229,0,0),1,1);

			display.drawPixel(16,2,display.Color333(229,0,0));
			display.drawPixel(16,4,display.Color333(229,0,0));

			display.swapBuffer(false);

			slowFrameRate = millis();
		}
		yield();	//Give the background process some lovin'
		delay(25);
	}
	}
}

void marquee()
{
	char topLine[40] = {""};
#ifdef USING_SPECIAL_MESSAGES
  	char* botmLine = getMessageOfTheDay();
#else
  	char botmLine[40] = "INSERT YOUR SPECIAL MESSAGE HERE";
#endif
	String tFull;

	for (int show = 0; show < SHOWCLOCK/20 ; show++)
  {

		if (mode_changed == 1)
			return;
		if(mode_quick){
			mode_quick = false;
			display_date();
			quickWeather();
			//marquee();
			return;
		}
		//sprintf(topLine, "AA %02d-%02d-%04d %02d:%02d:%02d%s %s\n",timeinfo->tm_mday, timeinfo->tm_mon+1, timeinfo->tm_year+1900, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, dstAbbrev);
		tFull = "Pong Clock";
		tFull.toUpperCase();
		tFull.toCharArray(topLine, tFull.length()+1);
		tFull = "";

		//scrollBigMessage(topLine);
		scrollMessage(topLine, botmLine, 51, 53, Green, Navy);

		delay(50);

		yield();
  }
}

void display_date()
{
	DEBUGpln("in display_date");
	uint16_t color = display.Color333(0,1,0);

  //to be defined
}



#ifdef USING_SPECIAL_MESSAGES

char* getMessageOfTheDay()
{

  DEBUGpln("getMessageOfTheDay");
  for (int i = 1; i < sizeof(ourHolidays) / sizeof(ourHolidays[0]); i++)
  {

    if (ourHolidays[i].month == (timeinfo->tm_mon+1) && ourHolidays[i].day == timeinfo->tm_mday)
      return ourHolidays[i].message;
  }
  return ourHolidays[0].message;
}

#endif

void nitelite()
{
  static int lastSecond = 60;
  int nowTime = dstAdjusted.time(&dstAbbrev);
  int nowHour = timeinfo->tm_hour;
  nowHour %= 12;
  if (nowHour == 0) nowHour = 12;
  int nowMinute = timeinfo->tm_min;
  int nowSecond = timeinfo->tm_sec;
  if(lastSecond != nowSecond)
  {
    cls();
    char nowBuffer[5] = "";
    sprintf(nowBuffer, "%2d%02d", nowHour, nowMinute);
    display.fillCircle(7, 6, 7, display.Color333(10, 10, 10)); // moon
    display.fillCircle(9, 4, 7, display.Color333(0, 0, 0));    // cutout the crescent
    display.drawPixel(16, 3, display.Color333(10, 10, 10));    // stars
    display.drawPixel(30, 2, display.Color333(10, 10, 10));
    display.drawPixel(19, 6, display.Color333(10, 10, 10));
    display.drawPixel(21, 1, display.Color333(10, 10, 10));
    vectorNumber(nowBuffer[0] - '0', 15, 11, display.Color333(10, 10, 10), 1, 1);
    vectorNumber(nowBuffer[1] - '0', 19, 11, display.Color333(10, 10, 10), 1, 1);
    vectorNumber(nowBuffer[2] - '0', 25, 11, display.Color333(10, 10, 10), 1, 1);
    vectorNumber(nowBuffer[3] - '0', 29, 11, display.Color333(10, 10, 10), 1, 1);
    display.drawPixel(23, 12, (nowSecond % 2)? display.Color333(5, 5, 5) : display.Color333(0, 0, 0));
    display.drawPixel(23, 14, (nowSecond % 2)? display.Color333(5, 5, 5) : display.Color333(0, 0, 0));
    display.swapBuffer(false);
  }
  lastSecond = nowSecond;
}

// Input a value 0 to 24 to get a color value.
// The colours are a transition r - g - b - back to r.
uint16_t Wheel(byte WheelPos) {
  if(WheelPos < 8) {
   return display.color444(15 - WheelPos*2, WheelPos*2, 0);
  } else if(WheelPos < 16) {
   WheelPos -= 8;
   return display.color444(0, 15-WheelPos*2, WheelPos*2);
  } else {
   WheelPos -= 16;
   return display.color444(0, WheelPos*2, 7 - WheelPos*2);
  }
}

void updateData() {
  now = dstAdjusted.time(&dstAbbrev);
  timeinfo = localtime (&now);	
  Serial.println("Updating time...");
  sprintf(time_str, "%2d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  Serial.println(time_str);
  Serial.println( "Updating conditions...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  readyForWeatherUpdate = false;
  drawDateTime();
  delay(1000);
}

void drawDateTime() {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];

  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  Serial.println(buff);
  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
    display.setCursor(0,8);  
  display.setTextColor(display.color444(0,15,0));
  display.print(buff);
  Serial.println(buff);

}
void printTime(time_t offset)
{
  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev)+offset;
  struct tm *timeinfo = localtime (&t);
  
  int hour = (timeinfo->tm_hour+11)%12+1;  // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s\n",timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_year+1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour>=12?"pm":"am", dstAbbrev);
  Serial.print(buf);
}
void updateNTP() {
  
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);

  delay(500);
  while (!time(nullptr)) {
    Serial.print("#");
    delay(1000);
  }
}
//given unix time t, returns day of week Sun-Sat as an integer 0-6
uint8_t fdow(unsigned long t)
{
    return ((t / 86400) + 4) % 7;
}
union single_double{
  uint8_t two[2];
  uint16_t one;
} this_single_double;

// This draws the weather icons
void draw_weather_icon (uint8_t icon)
{
  if (icon>10)
  icon=10;
  for (int yy=0; yy<10;yy++)
  {
    for (int xx=0; xx<10;xx++)
    {
      uint16_t byte_pos=(xx+icon*10)*2+yy*220;
      this_single_double.two[1]=weather_icons[byte_pos];
      this_single_double.two[0]=weather_icons[byte_pos+1];
      display.drawPixel(1+xx,yy,this_single_double.one);
    }
  }
}
