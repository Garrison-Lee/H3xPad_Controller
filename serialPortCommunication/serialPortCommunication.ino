#include <SPI.h>
#include <SD.h>

const int SECOND = 1000;

// Custom sloppy ass Serial communication protocol H3XPI?
const String HEXLIMITER = ":H3X:";
const String GET = "GET";
const String PUT = "PUT";
const String LOG = "LOG";
const String READY = "RDY";
const String ERROR = "ERR";
const String OKK = "OKK";
const String DEFAULT_MACRO = "t0pSecretePa55w0rd";

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

void handleCommand(String cmd, String arg) {
  // cmd will be the String between the first two HEXLIMITERS
  // arg will be the remainder of that String, so from immediately after the second HEXLIMITER through the end

  // ==== GET ====
  //  Expects 1 argument fileName
  if (cmd == GET) 
  {
    int firstDex = arg.indexOf(HEXLIMITER);
    if (firstDex == -1) { 
      Serial.println(HEXLIMITER+ERROR+HEXLIMITER+"-Description: Failed to parse arg for GET command"); 
      return; 
    }
    String firstArg = arg.substring(0, firstDex);
    String contents = readFromFile(firstArg);
    contents.trim(); //trim affects ending whitespace, we currently couldn't include an "enter" keystroke on the end
    if (contents == ERROR) {
      Serial.println(HEXLIMITER+ERROR+HEXLIMITER+"-Description: Failed to read from file: " + firstArg);
      return;
    }
    Serial.println(HEXLIMITER+contents+HEXLIMITER);
    return;
  } 

  // ==== PUT ====
  //  Expects 2 arguments fileName and fileContents
  else if (cmd == PUT)  
  {
    int firstDex = arg.indexOf(HEXLIMITER);
    if (firstDex == -1) { 
      Serial.println(HEXLIMITER+ERROR+HEXLIMITER + "-Description: Failed to parse arg for PUT command"); 
      return; 
    }
    int secondDex = arg.indexOf(HEXLIMITER, arg.indexOf(HEXLIMITER) + HEXLIMITER.length());
    if (secondDex == -1) { 
      Serial.println(HEXLIMITER+ERROR+HEXLIMITER + "-Description: Failed to parse arg for PUT command"); 
      return; 
    }
    String fileName = arg.substring(0, firstDex);
    String contents = arg.substring(firstDex + HEXLIMITER.length(), secondDex);
    Serial.println("Saving '" + contents + "' to: " + fileName);
    Serial.println(HEXLIMITER+writeToFile(fileName, contents)+HEXLIMITER);
    return;
  }
  
  // ==== LOG ====
  else if (cmd == LOG) 
  {
    Serial.println("===== CARD CONTENTS =====");
    logCardContents();
    Serial.println(HEXLIMITER+OKK+HEXLIMITER);
    return;
  }

  // ==== READY ====
  else if (cmd == READY) 
  {
    Serial.println(HEXLIMITER+READY+HEXLIMITER);
    return;
  }

  else 
  {
    Serial.println(HEXLIMITER+ERROR+HEXLIMITER+": I don't know what to do with the command: " + cmd);
    return;
  }
}

// Parses a line of SerialData
void parseLine(String line) {
  //Serial.println("Parsing line: " + line);
  int hexDex = lineBuffer.indexOf(HEXLIMITER);
  int closeDex = lineBuffer.indexOf(HEXLIMITER, hexDex+HEXLIMITER.length());
  if (hexDex == -1 || closeDex == -1) {
    Serial.println(HEXLIMITER+ERROR+HEXLIMITER+"-Description: Failed to extract command from line");
    return;
  }

  String command = lineBuffer.substring(HEXLIMITER.length(), closeDex);
  String arg = lineBuffer.substring(closeDex + HEXLIMITER.length());
  
  handleCommand(command, arg);
}

// Writes a String to a file found by filename. Creates it if one does not exist
String writeToFile(String fileName, String contents) {
  File file = SD.open(fileName, O_READ | O_WRITE | O_CREAT); //FILE_WRITE includes O_APPEND which I don't want
  if (!file) {
    return ERROR;
  }
  file.println(contents);
  file.close();
  return OKK;
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
      file.println(DEFAULT_MACRO);
      file.close();
      return DEFAULT_MACRO;
    }
    return ERROR;
  }
}

// Attempts to find the SD, we run this in a once-per-second loop
void ensureSD()
{
  if (sdFound)
    return;

  sdFound = SD.begin(10);
  if (!sdFound) {
    Serial.println("Board: I do not see an SD card or module, since this was going to be encased you should probably call Garrison at this point...");
  }
}

// Simple helper that prints the entire card's contents, starting from the root
void logCardContents() {
  if (!sdFound) {
    return;
  }
  Serial.println("=== Logging Card Contents: ===");
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
