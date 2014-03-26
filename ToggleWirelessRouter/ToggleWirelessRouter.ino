/* 
  Toggle button for wireless router D-Link DIR-600
 
 Turns on or off wifi when pressing a pushbutton, reporting on and off state to Xively.
 It also polls a web server acting as a remote control.
 
 by Victor Nilsson  2014-03-24
 
 */

#include <SPI.h>
#include <Ethernet.h>
#include <aJSON.h>

#define DEBUG 0

#if DEBUG
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINT(x) Serial.print(x)
#else
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINT(x)
#endif

// Xively
#define APIKEY         "iILs0jI5riX3QdegHcqhnzdNb1ELzJwsYzaTKBKSAMp5nkk0"
#define FEEDID         286216979

// Wireless router login
#define ROUTER_USER    "admin"
#define ROUTER_PASSWD  "bf4edz8qtzdjtde"

byte mac[] = {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };//no need to define mac-address for the Galileo

// Server addresses
char router[] = "192.168.0.1";
char xively[] = "api.xively.com";
char webRemote[] = "ddwap.mah.se";

// initialize the library instance:
EthernetClient client;

// server address of current connection
char *connectedServer = NULL;

// for parsing HTTP response message
boolean readingHeader = false;
boolean chunked = false;
int messageLength = 0;

char data[2048] = "";
int dataLength = 0;

boolean wirelessEnabled = false;
boolean buttonPressed = false;

const int buttonPin = 2;     // the number of the pushbutton pin
const int ledPin =  13;      // the number of the LED pin

// for web remote control
unsigned long postingInterval=2000L; //milliseconds between connections 
unsigned long lastConnectionTime = 0;  // last time you connected to the server, in milliseconds
int webRemoteCount = -1;

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  
  //ethernet stuff
  if (Ethernet.begin(mac)==0) {
    DEBUG_PRINTLN("ethernet init failed");
    // no point in carrying on, so do nothing forevermore:
    for(;;)
      ;
  } else {
    DEBUG_PRINTLN("ethernet init succeded");
    DEBUG_PRINTLN(Ethernet.localIP());
  }
  // give the Ethernet shield time to initialize:
  delay(2000);
  
  routerLogin();
}

void loop() {
  // Read button state if not connected to router
  if (connectedServer != router && digitalRead(buttonPin) == LOW) {
    // button is pressed
    buttonPressed = true;
  }
  if (connectedServer) {
    char *server = connectedServer;
    // we will try to receive more data
    boolean moreData = true;
    
    // if there are incoming bytes available from the server, read them
    if (client.available()) {
      moreData = receiveData();
    } else if (!readingHeader && !chunked && messageLength < 1) {
      moreData = false;
    }
    
    // if the server's disconnected, stop the client:
    if (!client.connected()) {
      client.stop();
      connectedServer = NULL;
      moreData = false;
    } else if (!moreData && connectedServer != router) {
      //No more data to read, close connection
      closeConnection();
    }

    if (!moreData) {
      data[dataLength] = '\0';  // null-terminate string
      
      if (connectedServer == router) {
        //Persistent connection
        if (dataLength > 0) {
          if (!handleRouterResponse()) {
            //Failed
            closeConnection();
          }
        }
      } else if (server == webRemote) {
        // Output from web remote control
        aJsonObject *jsonObj = aJson.parse(data);
        if (jsonObj) {
          aJsonObject *active = aJson.getObjectItem(jsonObj, "active");
          aJsonObject *count  = aJson.getObjectItem(jsonObj, "count");
          if (webRemoteCount > 0 && active && count &&
              active->valueint != wirelessEnabled && count->valueint != webRemoteCount)
            buttonPressed = true;
          if (count)
            webRemoteCount = count->valueint;
          aJson.deleteItem(jsonObj);
        }
      }
      dataLength = 0;
    }
  } else if (buttonPressed) {
    // Toggle wireless
    routerLogin();
  } else if (millis() - lastConnectionTime > postingInterval) {
    contactWebRemote();
    lastConnectionTime = millis();
  }
}

boolean routerLogin()
{
  return routerRequest("/session.cgi",
                       "REPORT_METHOD=xml&ACTION=login_plaintext"
                       "&USER=" ROUTER_USER "&PASSWD=" ROUTER_PASSWD "&CAPTCHA=");
}

// Make a HTTP request using POST method
boolean routerRequest(const char *url, const char *data)
{
  if (connectedServer == router || connectToWeb(router)) {
    client.print("POST ");
    client.print(url);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(router);
    client.print("Content-Type: ");
    client.println(data[0] == '<' ? "text/xml" : "application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(strlen(data));
    client.println("Connection: keep-alive");
    client.println();
    client.print(data);
    initResponse();
    return true;
  }
  return false;
}

boolean connectToWeb(char *server)
{
  closeConnection();  // make sure no previous connection is still open
  
  DEBUG_PRINT("connecting to ");
  DEBUG_PRINT(server);
  DEBUG_PRINTLN("...");
  
  if (client.connect(server, 80)) {
    DEBUG_PRINTLN("connected");
    connectedServer = server;
    initResponse();
    return true;
  }
  // if you didn't get a connection to the server:
  DEBUG_PRINTLN("connection failed");
  return false;
}

void closeConnection()
{
  if (connectedServer) {
    client.flush();
    client.stop();
    connectedServer = NULL;
  }
}

// Prepare to receive a HTTP response
void initResponse()
{ 
  readingHeader = true;
  chunked = false;
  messageLength = 0;
}

// return true if there is more data to receive
boolean receiveData()
{
  static int chunkLength;
  
  char c = client.read();
  DEBUG_PRINT(c);
  
  if (dataLength + 1 < sizeof(data)) {
    data[dataLength++] = c;
    if (readingHeader) {
      parseHeader(c);
      chunkLength = -1;
      return true;
    }
    if (messageLength > 0) {
      messageLength--;
      if (c == '\t')  // remove whitespace
        dataLength--;
      return true;
    }
    if (chunked) {
      if (chunkLength >= 0) {
        dataLength--;
        // chunk is terminated by CRLF
        if (c == '\n') {
          if (chunkLength == 0)  // final chunk
            return false;
          chunkLength = -1;
        }
      } else if (c == '\n') {
        data[dataLength] = '\0';  // null-terminate string
        dataLength += chunkLength;
        // convert hexadecimal number
        messageLength = chunkLength = strtol(data + dataLength, NULL, 16);
      } else {
        chunkLength--;
      }
      return true;
    }
  }
  return false;
}

// Parse HTTP message headers
void parseHeader(char c)
{
  // check for empty line indicating the end of the header
  if (dataLength >= 4 && c == '\n' && !memcmp(data + dataLength - 4, "\r\n\r", 3)) {
    readingHeader = false;
    dataLength = 0;
  } else if (c == ':') {
    // parse header field name
    int i = dataLength - 1;
    while (i > 0 && data[i-1] != '\n')
      i--;
    if (isalpha(data[i])) {
      // move field name to start of buffer and convert to lowercase
      char *p = data;
      while (p[i] != ':') {
        *p = tolower(p[i]);
        p++;
      }
      *p = ':';
      dataLength = p - data + 1;
    }
  } else if (c == '\r') {
    data[dataLength] = '\0';  // null-terminate string
    
    if (!memcmp(data, "content-length: ", 16))
      messageLength = atoi(data + 16);
      
    else if (!strcmp(data, "transfer-encoding: chunked\r")) {
      chunked = true;
      messageLength = 0;
    }
  }
}

boolean handleRouterResponse()
{
  char *xml = strstr(data, "?>");  // skip xml declaration
  if (xml)
    xml = strchr(xml, '<');  // find root element
  if (xml) {
    if (!memcmp(xml, "<report>", 8)) {
      //Login report
      if (strstr(xml, "<RESULT>SUCCESS"))
        return routerRequest("/getcfg.php", "SERVICES=WIFI.WLAN-1");
    }
    else if (!memcmp(xml, "<postxml", 8)) {
      //Received configuration
      xml = strstr(xml, "<phyinf>");
      if (xml) {
        xml = strstr(xml, "</active>");
        if (xml) {
          // get current state from configuration:
          // <active>1</active> means wireless enabled
          // <active>0</active> means wireless disabled
          wirelessEnabled = xml[-1] - '0';
          // turn LED on or off to indicate current state
          digitalWrite(ledPin, wirelessEnabled);
          
          if (buttonPressed) {
            buttonPressed = false;
            // toggle active
            xml[-1] = '0' + (wirelessEnabled = !wirelessEnabled);
            return routerRequest("/hedwig.cgi", data);
          }
          sendToXively();
          return true;
        }
      }
    }
    else if (!memcmp(xml, "<hedwig>", 8)) {
      //Sent configuration
      return routerRequest("/pigwidgeon.cgi", "ACTIONS=SETCFG,SAVE,ACTIVATE&REPORT_METHOD=xml");
    }
    else if (!memcmp(xml, "<pigwidg", 8)) {
      //Saved configuration
      // turn LED on if wireless is now activated
      digitalWrite(ledPin, wirelessEnabled);
      sendToXively();
      return true;
    }
  }
  // FAILED
  return false;
}

void sendToXively()
{
  if (connectToWeb(xively)) {
    // Make a HTTP request:
    client.print("PUT /v2/feeds/");
    client.print(FEEDID);
    client.println(".csv HTTP/1.1");
    client.print("Host: ");
    client.println(xively);
    client.print("X-PachubeApiKey: ");
    client.println(APIKEY);
    client.println("Content-Length: 9");
    client.println("Content-Type: text/csv");
    client.println("Connection: close");
    client.println();

    // here's the actual content of the PUT request:
    client.print("sensor2,");
    client.print(wirelessEnabled ? "1" : "0");
  }
}

void contactWebRemote()
{
  String sensors = "enabled=";
  sensors += wirelessEnabled;
  DEBUG_PRINTLN(sensors);
  
  if (connectToWeb(webRemote)) {
    client.print("GET /ad5625/galileo.php?");
    client.print(sensors);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(webRemote);
    client.println("Connection: close");
    client.println();
  }
}

