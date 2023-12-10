#include <SPI.h>
#include <SD.h>

const int SECOND = 1000;

// Custom sloppy ass Serial communication protocol H3XPI?
const String HEXLIMITER = ":H3X:";
const String GET = "GET";
const String PUT = "PUT";
const String LOG = "LOG";
const String DEFAULT_MACRO = "t0pSecretePa55w0rd!";

String lineBuffer = "";

bool sdFound = false;
unsigned long lastSecondStamp = 0;

void setup() {
  Serial.begin(9600);
  ensureSD();
  // TODO: Idk if this is actually wanted long-term!
  while (!Serial) {
    ; // Wait for something to connect before we wake up
  }
  ensureSD();
}

void loop() {
  if (millis() - lastSecondStamp > SECOND) {
    lastSecondStamp = millis();
    perSecondLoop();
  }

  readBuffer();

  // loop runs at about 30fps
  delay(33);
}

void perSecondLoop() {
  ensureSD();
}

// Reads the serial buffer and appends it to our String buffer.
//  We'll clear the buffer as we parse it
void readBuffer() {
  int bytesAvailable = Serial.available();
  if (bytesAvailable > 0)
  {
    lineBuffer += Serial.readString();

    if (lineBuffer.endsWith("\n")) {
      parseLine(lineBuffer);
      lineBuffer = "";
    }
  }
}

// Parses a line of SerialData
void parseLine(String line) {
  Serial.println("Parsing line: " + line);

  int hexDex = lineBuffer.indexOf(HEXLIMITER);
  if (hexDex == -1) {
    Serial.println("Failed to extract command from line: " + line);
    return;
  }

  String command = lineBuffer.substring(0, hexDex);
  String arg = lineBuffer.substring(hexDex + HEXLIMITER.length());
  Serial.println("Command is: " + command + ", arg is: " + arg);
  
  // SWITCH
  if (command == GET) {
    Serial.println(HEXLIMITER+readFromFile(arg)+HEXLIMITER);
  } 
  else if (command == PUT)  {
    int hexDex2 = lineBuffer.indexOf(HEXLIMITER, hexDex+HEXLIMITER.length());
    if (hexDex2 == -1) {
      Serial.println(HEXLIMITER+"ERROR"+HEXLIMITER+": Command is " + PUT + ", but I failed to find a second HEXLIMITER to parse fileName and contents");
      return;
    }

    String fileName = lineBuffer.substring(hexDex+HEXLIMITER.length(), hexDex2);
    String contents = lineBuffer.substring(hexDex2+HEXLIMITER.length(), lineBuffer.length()-1);
    Serial.println("Writing '" + contents + "' to: " + fileName);
    
    Serial.println(HEXLIMITER+writeToFile(fileName, contents)+HEXLIMITER);
  }
  else if (command == LOG) {
    Serial.println("===== CARD CONTENTS =====");
    logCardContents();
    Serial.println(HEXLIMITER+"DONE"+HEXLIMITER);
  }
  else {
    Serial.println(HEXLIMITER+"ERROR"+HEXLIMITER+": I don't know what to do with the command: " + command);
  }
}

// Writes a String to a file found by filename. Creates it if one does not exist
String writeToFile(String fileName, String contents) {
  File file = SD.open(fileName, O_READ | O_WRITE | O_CREAT); //FILE_WRITE includes O_APPEND which I don't want
  if (!file) {
    return "ERROR";
  }
  file.println(contents);
  file.close();
  return "DONE";
}

// Gets a single line from a file that is found by fileName
String readFromFile(String fileName) {
  File file = SD.open(fileName);
  if (file) {
    String response = file.readStringUntil('\n');
    file.close();
    return response;
  }
  else {
    if (!SD.exists(fileName)) {
      Serial.println("Requested file does not exist, initializing now!");
      file = SD.open(fileName, FILE_WRITE);
      Serial.println("Does the file exist? " + String(bool(file)));
      file.println(DEFAULT_MACRO);
      file.close();
      Serial.println("Now do I think it exists? " + String(SD.exists(fileName)));
      return DEFAULT_MACRO;
    }
    Serial.println("Failed to read from " + fileName);
    return "ERROR";
  }
}

// Attempts to find the SD, we run this in a once-per-second loop
void ensureSD()
{
  if (sdFound)
    return;

  SD.end();
  sdFound = SD.begin();
  if (!sdFound) {
    Serial.println("Failed to find SD card, will try again in a second");
  }
}

// Simple helper that prints the entire card's contents, starting from the root
void logCardContents() {
  if (!sdFound) {
    return;
  }
  Serial.println("Logging Card Contents:\n");
  File root = SD.open("/");
  printDirectory(root, 0);
  root.close();
}

// Simple helper that will recursively print the contents starting at a directory
void printDirectory(File dir, int numTabs) {
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}
