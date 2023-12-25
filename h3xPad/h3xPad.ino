#include <SPI.h>
#include <SD.h>
#include <Keyboard.h>
#include <avr/wdt.h>

// 1k ms
const int SECOND = 1000;
uint32_t lastSecondStamp = 0;
const uint32_t THIRTY_MINUTES = 1800000; // 1,800,000 ms = 30min

// === MACRO PAD STUFF ===
// For MacroPad functioning
const int MAX_MACRO_LENGTH = 64;
const int TAP_MACRO_ID = 0;
const int PRESS_MACRO_ID = 1;
const char *TAP_MACRO_FILENAME = "TAP.TXT";
const char *PRESS_MACRO_FILENAME = "PRS.TXT";
const int FILENAME_LENGTH = 8; // 7 + '\0'
char _tapMacro[MAX_MACRO_LENGTH+1] = ""; //+1 for the terminating char
char _pressMacro[MAX_MACRO_LENGTH+1] = "";
// How long the btn is depressed before it becomes a press?
// TODO: Save/Load settings?
const uint16_t PRESS_THRESHOLD = 400; // ms
// For physical button detection
const uint8_t PIN_BUTTON = 7;
int lastButtonState = 0;
int buttonState = 0;
/*ooh hoo look at this fancy long*/ 
uint32_t buttonPressStartTime;
bool busyTyping = false; // So we can ignore button mashing

// === RGB STUFF ===
// For that flashy RGB goodness everyone is going to want so I had better just include
//  the lil module I bought just has a GND pin and one analog pin per R,G,B, ez <3
const uint8_t PIN_R = 18; // Screen print on board reads A0
const uint8_t PIN_G = 19; // A1
const uint8_t PIN_B = 20; // A2

// === "API" (lol) ===
// App will send us things wrapped in tags which we can deal with as they come
// TODO: You know you want to! Ditch the strings and do it with raw bytes, chicken

const char *OPEN_MAIN = "<H3X>";
const char *CLOSE_MAIN = "</H3X>";
const char *OPEN_VERBOSE = "<VBE>";
const char *CLOSE_VERBOSE = "</VBE>";
const char *INTERNAL_DELIMITER = "<->";
// This length would allow for a max-length macro, an open/close tag-pair, 3 internal delimiters, 3 keywords, and a terminating char
const int BUFFER_LENGTH = MAX_MACRO_LENGTH + 30;
char _buffer[BUFFER_LENGTH] = "";

// Keywords
const char *GET = "GET";
const char *PUT = "PUT";
const char *LOG = "LOG";
const char *READY = "RDY";
const char *ERROR = "ERR";
const char *OKK = "OKK";
const char *DEFAULT_MACRO = "password";

// SAVE / LOAD
bool sdFound = false;

void setup() {
  // For Serial communication, in case user wants to update Macros, 
  //  and so that we can read from disk
  Serial.begin(9600);
  ensureSD();

  // For functioning as a Macro Pad
  Keyboard.begin();
  // For detecting button presses
  pinMode(PIN_BUTTON, INPUT);

  // For that juicy RGB
  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  // INTIAL COLOR BEFORE ANY PRESSES
  setLED(0, 10, 10);

  _buffer[BUFFER_LENGTH-1] = '\0';
}

// Just a convenience method that wraps our message in the appropriate tag(s)
void sendResponse(const char *msg) { 
  Serial.print(OPEN_MAIN);
  Serial.print(msg);
  Serial.print(CLOSE_MAIN);
}
void sendVerbose(const char *msg) {
  Serial.print(OPEN_VERBOSE);
  Serial.print(msg);
  Serial.print(CLOSE_VERBOSE);
}
void sendVerbose(const __FlashStringHelper *msg) {
  Serial.print(OPEN_VERBOSE);
  Serial.print(msg);
  Serial.print(CLOSE_VERBOSE);
}

void loop() {
  uint32_t curTime = millis();

  // Button press detection!
  buttonState = digitalRead(PIN_BUTTON);
  if ((buttonState != lastButtonState)) {
    lastButtonState = buttonState;
    if (buttonState == 0) {
      // DEFAULT COLOR! TODO: Can we variablize or even make this a setting we read/write?
      setLED(0, 10, 10);
      if (curTime - buttonPressStartTime < 25) {
        sendVerbose(F("button BOUNCED, ignoring!"));
      } else {
        // btn released, type out Macro!
        sendVerbose(F("button RELEASED"));
        sendMacroToKeyboard(curTime - buttonPressStartTime > PRESS_THRESHOLD ? PRESS_MACRO_ID : TAP_MACRO_ID);
      }
    } else {
      // PRESSED COLOR!
      setLED(0, 5, 0);
      // btn pressed, cache the time
      sendVerbose(F("button PRESSED"));
      buttonPressStartTime = curTime;
    }
  }
  
  // Change LED to indicate long press time has been reached
  if (buttonState == 1 && curTime - buttonPressStartTime > PRESS_THRESHOLD) {
    // LONG PRESS INDICATION COLOR!
    setLED(5, 0, 5);
  }

  if (curTime - lastSecondStamp > SECOND) {
    lastSecondStamp = curTime;
    perSecondLoop();
  }

  readBuffer();
}

// TODO: 30hz loop? timed events?
void perSecondLoop() {
  ensureSD();

  if (millis() > THIRTY_MINUTES) {
    unsigned long time;
    time = millis() + 5000;
    wdt_enable(WDTO_15MS);
    while (millis() < time) {
      // We wait 5 seconds with a 15ms watchdog set up
    }
    sendVerbose(F("ERROR: Tried and failed to RESET!"));
  }
}

// Yup, those inputs are as easy as you think. 0,0,0 is black/off
// 255,255,255 is white and brightest. etc.
// NOTE: Args are signed ints because 255 is max value our light takes
//  and these are 1-byte ints which have a max value of 255 when signed
void setLED(int R, int G, int B) {
  // TODO: Minimum value that actually lights up seems to be 128 (255/2?)
  // As-is, a value of 0 will get mapped to 127 which is actually off. Everything
  //  else begins at 128 which seems to be the lowest "ON" value
  analogWrite(PIN_R, map(R, 0, 255, 127, 255));
  analogWrite(PIN_G, map(G, 0, 255, 127, 255));
  analogWrite(PIN_B, map(B, 0, 255, 127, 255));
}

// Reads the serial buffer and appends it to our String buffer.
//  We'll clear the buffer as we parse it
void readBuffer() {
  static uint8_t pos = 0;
  uint8_t bytesAvailable = Serial.available();
  if (bytesAvailable > 0)
  {
    _buffer[pos] = Serial.read();
    pos++;
    if (pos >= BUFFER_LENGTH-1) {
      sendVerbose(F("Buffer overflow! Resetting"));
      pos = 0;
    }
    //Serial.print(_buffer);

    char *cmdStart = strstr(_buffer, OPEN_MAIN);
    if (cmdStart) {
      // TODO: Worth it to make that length a const?
      cmdStart += strlen(OPEN_MAIN); // We actually want to drop the open tag when parsing
      char *cmdEnd = strstr(_buffer, CLOSE_MAIN);
      if (cmdEnd) {
        int cmdLength = cmdEnd - cmdStart;
        char cmd[cmdLength+1];
        strncpy(cmd, cmdStart, cmdLength);
        cmd[cmdLength] = '\0';
        parseCommand(cmd);
        // Reset our entire buffer to 0's/null-chars because otherwise we'll immediately "see" the previous command
        // NOTE: If this ever changes s.t. this loop runs slower than individual characters are received, then
        //  this part will need to change to move the trailing characters up instead of resetting the buffer completely
        memset(_buffer, 0, sizeof(_buffer));
        pos = 0;
      } // else we are mid-tag, waiting for a close tag to appear!
    } // else do nothing, openTag not found, we are still waiting!
  }
}

// Parses a line of SerialData
void parseCommand(char *fullCommand) {
  sendVerbose(F("Parsing command: "));
  sendVerbose(fullCommand);

  char *cmd = fullCommand;
  char *arg = strstr(cmd, INTERNAL_DELIMITER);
  int cmdLength = strlen(cmd);
  if (arg) {
    cmdLength = arg - cmd;
    arg += strlen(INTERNAL_DELIMITER);
  }

  handleCommand(cmd, cmdLength, arg);
}

void handleCommand(const char *command, int cmdLength, const char *arg) {
  // cmd will be the string up until the first internal delimeter
  // arg will be everything afterwards, and could contain more delimters to parse
  char cmd[cmdLength+1];
  cmd[cmdLength] = '\0';
  strncpy(cmd, command, cmdLength);

  sendVerbose(F("Handling command: "));
  sendVerbose(cmd);
  sendVerbose(F("Command's args: "));
  sendVerbose(arg);
  // ==== GET ====
  //  Expects 1 argument: fileName
  if (strcmp(cmd, GET) == 0) 
  {
    if (!arg) { 
      sendVerbose(F("ERROR - GET requires one arg"));
      sendResponse(ERROR);
      return; 
    }
    // TODO: Need slightly more if we ever get multiple options
    char *contents = readFromFile(atoi(arg) == TAP_MACRO_ID ? TAP_MACRO_FILENAME : PRESS_MACRO_FILENAME);
    if (strcmp(contents, ERROR) == 0) {
      sendVerbose(F("ERROR - Failed to read from SD card!"));
      sendResponse(ERROR);
      return;
    }
    sendResponse(contents);
    return;
  } 

  // ==== PUT ====
  //  Expects 2 arguments fileName and fileContents
  else if (strcmp(cmd, PUT) == 0)  
  {
    // Note, char right at arg will be 0 or 1 to indicate PressOrTapMacroID
    // so like 0<->password\0 might be an example of what arg points to
    if (!arg) { 
      sendVerbose(F("ERROR - PUT requires args"));
      sendResponse(ERROR);
      return; 
    }
    char *writeContents = strstr(arg, INTERNAL_DELIMITER);
    if (!writeContents) { 
      sendVerbose(F("ERROR - PUT requires 2 args"));
      sendResponse(ERROR);
      return; 
    }
    writeContents += strlen(INTERNAL_DELIMITER);
    sendResponse(writeToFile(atoi(arg) == TAP_MACRO_ID ? TAP_MACRO_FILENAME : PRESS_MACRO_FILENAME, writeContents));
    return;
  }
  
  // ==== LOG ====
  else if (strcmp(cmd, LOG) == 0) 
  {
    logCardContents();
    sendResponse(OKK);
    return;
  }

  // ==== READY ====
  else if (strcmp(cmd, READY) == 0) 
  {
    sendResponse(READY);
    return;
  }

  else 
  {
    sendVerbose(F("ERROR - Unrecognized command"));
    sendResponse(ERROR);
    return;
  }
}

void typeOut(const char *macro) {
  busyTyping = true;
  const char *p;
  p = macro;
  while (*p) {
    Keyboard.write(*p);
    p++;
    delay(10);
  }
  busyTyping = false;
}

void sendMacroToKeyboard(int macroId) {
  if (busyTyping) {
    return;
  }

  if (macroId == TAP_MACRO_ID) {
    if (!_tapMacro[0]) {
      readFromFile(TAP_MACRO_FILENAME);
      if (!_tapMacro[0]) {
        sendVerbose(F("Failed to find tapMacro!"));
        return;
      }
    }
    typeOut(_tapMacro);
  } 
  else {
    if (!_pressMacro[0]) {
      readFromFile(PRESS_MACRO_FILENAME);
      if (!_pressMacro[0]) {
        sendVerbose(F("Failed to find pressMacro!"));
        return;
      }
    }
    typeOut(_pressMacro);
  }
}

// Writes a String to a file found by filename. Creates it if one does not exist
const char* writeToFile(const char *fileName, const char *contents) {
  sendVerbose(F("Writing contents to file!"));
  // First we delete the existing version because there is shitty ass support for overwriting otherwise
  SD.remove(fileName);
  File file = SD.open(fileName, O_READ | O_WRITE | O_CREAT); //FILE_WRITE includes O_APPEND which I don't want
  if (!file) {
    sendVerbose(F("Failed to open file"));
    return ERROR;
  }
  file.print(contents);
  file.close();
  sendVerbose(F("Save operation completed, attempting to read back..."));
  return readFromFile_AssertFileExists(fileName);
}

// Gets a single line from a file that is found by fileName
char* readFromFile(const char *fileName) {
  File file = SD.open(fileName);
  if (file) {
    char contents[MAX_MACRO_LENGTH + 1] = "";
    contents[MAX_MACRO_LENGTH] = '\0';
    int fileSize = file.available();
    if (fileSize > 0) {
      file.read(contents, fileSize);
    } else {
      strcpy(contents, "BLANK");
    }
    file.close();
    if (fileName == TAP_MACRO_FILENAME) {
      strcpy(_tapMacro, contents);
      return _tapMacro;
    } else if (fileName == PRESS_MACRO_FILENAME) {
      strcpy(_pressMacro, contents);
      return _pressMacro;
    }
    strcpy(_tapMacro, ERROR);
    sendVerbose(F("ERROR - Unknown fileName to read from"));
    return _tapMacro;
  }
  else {
    if (!SD.exists(fileName)) {
      sendVerbose(F("Requested file does not exist, initializing now!"));
      file = SD.open(fileName, FILE_WRITE);
      file.print(DEFAULT_MACRO);
      file.close();
      if (fileName == TAP_MACRO_FILENAME) {
        strcpy(_tapMacro, DEFAULT_MACRO);
        return _tapMacro;
      } else if (fileName == PRESS_MACRO_FILENAME) {
        strcpy(_pressMacro, DEFAULT_MACRO);
        return _pressMacro;
      }
      sendVerbose(F("ERROR - Unkown fileName was used..."));
      strcpy(_tapMacro, ERROR);
      return _tapMacro;
    }
    sendVerbose(F("ERROR - Failed to read from existing file"));
    strcpy(_tapMacro, ERROR);
    return _tapMacro;
  }
}

char* readFromFile_AssertFileExists(const char *fileName) {
  File file = SD.open(fileName);
  if (file) {
    char contents[MAX_MACRO_LENGTH + 1] = "";
    contents[MAX_MACRO_LENGTH] = '\0';
    int fileSize = file.available();
    if (fileSize > 0) {
      file.read(contents, fileSize);
    } else {
      strcpy(contents, "BLANK");
    }
    file.close();
    if (fileName == TAP_MACRO_FILENAME) {
      strcpy(_tapMacro, contents);
      return _tapMacro;
    } else if (fileName == PRESS_MACRO_FILENAME) {
      strcpy(_pressMacro, contents);
      return _pressMacro;
    }
    strcpy(_tapMacro, ERROR);
    sendVerbose(F("ERROR - Unknown fileName to read from"));
    return _tapMacro;
  }
  
  sendVerbose(F("ERROR - Failed to open a file we asserted must exist"));
  strcpy(_tapMacro, ERROR);
  return _tapMacro;
}

// Attempts to find the SD, we run this in a once-per-second loop
void ensureSD()
{
  if (sdFound)
    return;

  sdFound = SD.begin(10);
  if (!sdFound) {
    sendVerbose(F("Failed to find an SD card or SD Module!"));
  } else {
    logCardContents();
  }
}

// Simple helper that prints the entire card's contents, starting from the root
void logCardContents() {
  if (!sdFound) {
    return;
  }
  sendVerbose(F("=== Logging Card Contents ==="));
  File root = SD.open("/");
  Serial.print(OPEN_VERBOSE);
  printDirectory(root, 0);
  root.close();
  Serial.print(CLOSE_VERBOSE);
}

// Simple helper that will recursively print the contents starting at a directory
void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();
    if (!entry) {
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
