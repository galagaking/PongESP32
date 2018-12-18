#include "ExampleParser.h"
#include "JsonStreamingParser.h"
#include "JsonListener.h"
#include <WiFi.h>
#include <WiFiClient.h>

String CurrentKey;
void ExampleListener::whitespace(char c) {
  Serial.println("whitespace");
}

void ExampleListener::startDocument() {
  Serial.println("start document");
}

void ExampleListener::key(String key) {
  CurrentKey=key;
}

void ExampleListener::value(String value) {
  if (CurrentKey=="Temp")
  {
    Serial.println(value);
    Temperature=value;
  }
  else
  if (CurrentKey=="Wind")
  {
    Serial.println(value);
    WindSpeed=value;
  }
}

void ExampleListener::endArray() {
  Serial.println("end array. ");
}

void ExampleListener::endObject() {
  Serial.println("end object. ");
}

void ExampleListener::endDocument() {
  Serial.println("end document. ");
}

void ExampleListener::startArray() {
   Serial.println("start array. ");
}

void ExampleListener::startObject() {
   Serial.println("start object. ");
}

void ExampleListener::doUpdate(String url) {
  Serial.println("DoUpdate");
  JsonStreamingParser parser;
  parser.setListener(this);
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect("www.buienradar.nl", httpPort)) {
    Serial.println("connection failed");
    return;
  }

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: buienradar.nl\r\n" +
               "Connection: close\r\n\r\n");      
  int retryCounter = 0;
  while(!client.available()) {
    delay(1000);
    retryCounter++;
    if (retryCounter > 10) {
      return;
    }
  }

  int pos = 0;
  boolean isBody = false;
  char c;

  int size = 0;
  client.setNoDelay(false);
  while(client.connected()) {
    while((size = client.available()) > 0) {
      c = client.read();
      Serial.print(c);
      if (c == '{' || c == '[') {
        isBody = true;
      }
      if (isBody) {
        parser.parse(c);
      }
    }
  }
}
