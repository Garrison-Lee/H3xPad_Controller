#include <SPI.h>
#include <SD.h>
#include <Keyboard.h>

const int SECOND = 1000;

// For MacroPad functioning
// TODO: Right now WindowsApp tells us fileName. We should take that over officially
const String TAP_MACRO_FILENAME = "TAP.TXT";
const String PRESS_MACRO_FILENAME = "PRESS.TXT";
String _tapMacro = "";
String _pressMacro = "";
// How long the btn is depressed before it becomes a press?
// TODO: Save/Load settings?
const unsigned int PRESS_THRESHOLD = 333;

// For physical button detection
const int btnPin = 7;
int btnState = 0;
unsigned long btnPressTime;

// API for our Windows Forms App (lol)
const String HEXLIMITER = ":H3X:";
const String INTERNAL_LIM = "/-/";
const String VERBOSE_LIM = "<esobreV>";
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
  // For Serial communication, in case user wants to update Macros
  Serial.begin(9600);
  ensureSD();
  // TODO: Idk if this is actually wanted long-term!
  while (!Serial) {
    ; // Wait for something to connect before we wake up
  }
  ensureSD();

  // For functioning as a Macro Pad
  Keyboard.begin();

  // For detecting button presses
  pinMode(btnPin, INPUT);
}

void loop() {
  unsigned long curTime = millis();

  if ((digitalRead(btnPin) != btnState)) {
    // btnState changed!
    if (btnState == 0) {
      // btn released, type out Macro!
      if (curTime - btnPressTime > PRESS_THRESHOLD) {
        sendMacroToKeyboard(1);
      } else {
        sendMacroToKeyboard(0);
      }
    } else {
      // btn pressed, cache the time
      btnPressTime = curTime;
    }
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
}

// Reads the serial buffer and appends it to our String buffer.
//  We'll clear the buffer as we parse it
void readBuffer() {
  int bytesAvailable = Serial.available();
  if (bytesAvailable > 0)
  {
    lineBuffer += Serial.readString();

    int eol = lineBuffer.indexOf("\n");
    if (eol != -1) // we have a line to parse!
    {
      parseLine(lineBuffer.substring(0, eol));
      lineBuffer = lineBuffer.substring(eol+1);
    }
  }
}

// Parses a line of SerialData
void parseLine(String line) {
  //Serial.print(VERBOSE_LIM + "Parsing line: " + line + VERBOSE_LIM);
  int hexDex = line.indexOf(HEXLIMITER);
  int closeDex = line.indexOf(HEXLIMITER, hexDex+HEXLIMITER.length());
  String excess = line.substring(closeDex + HEXLIMITER.length());
  if (excess.length() > 0) {
    Serial.print(VERBOSE_LIM + "NonTerminating WARNING: Dropping '" +excess+ "' from parsed line" + VERBOSE_LIM);
  }
  line = line.substring(hexDex+HEXLIMITER.length(), closeDex);

  if (hexDex == -1 || closeDex == -1) {
    Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
    Serial.print(VERBOSE_LIM+"- ERROR Description: Failed to extract command from line..."+VERBOSE_LIM);
    return;
  }

  int argDex = line.indexOf(INTERNAL_LIM);
  String command;
  String arg;
  if (argDex == -1) {
    command = line;
    arg = "";
  } else {
    command = line.substring(0, argDex);
    arg = line.substring(argDex+INTERNAL_LIM.length());
  }

  handleCommand(command, arg);
}

void handleCommand(String cmd, String arg) {
  // cmd will be the string up until the first internal delimeter
  // arg will be everything afterwards, and could contain more delimters to parse

  // *should* be redundant
  cmd.trim();
  arg.trim();

  // ==== GET ====
  //  Expects 1 argument fileName
  if (cmd == GET) 
  {
    if (arg.length() < 1) { 
      Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
      Serial.print(VERBOSE_LIM+"- ERROR Description: Failed to parse arg for GET command..."+VERBOSE_LIM); 
      return; 
    }
    String contents = readFromFile(arg);
    contents.trim(); //trim affects ending whitespace, we currently couldn't include an "enter" keystroke on the end
    if (contents == ERROR) {
      Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
      Serial.print(VERBOSE_LIM+"- ERROR Description: Failed to read '" + arg + "' from SD card!"+VERBOSE_LIM);
      return;
    }
    Serial.print(HEXLIMITER+contents+HEXLIMITER);
    return;
  } 

  // ==== PUT ====
  //  Expects 2 arguments fileName and fileContents
  else if (cmd == PUT)  
  {
    if (arg.length() < 1) { 
      Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
      Serial.print(VERBOSE_LIM+"- ERROR Description: Failed to parse arg for PUT command"+VERBOSE_LIM); 
      return; 
    }
    int limDex = arg.indexOf(INTERNAL_LIM);
    if (limDex == -1) { 
      Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
      Serial.print(VERBOSE_LIM+"- ERROR Description: Failed to parse second arg for PUT command"+VERBOSE_LIM); 
      return; 
    }
    String fileName = arg.substring(0, limDex);
    String contents = arg.substring(limDex + INTERNAL_LIM.length());
    fileName.trim();
    contents.trim();
    Serial.print(VERBOSE_LIM+"Saving '" + contents + "' to " + fileName+VERBOSE_LIM);
    Serial.print(HEXLIMITER+writeToFile(fileName, contents)+HEXLIMITER);
    return;
  }
  
  // ==== LOG ====
  else if (cmd == LOG) 
  {
    Serial.print(VERBOSE_LIM+"===== CARD CONTENTS ====="+VERBOSE_LIM);
    logCardContents();
    Serial.print(HEXLIMITER+OKK+HEXLIMITER);
    return;
  }

  // ==== READY ====
  else if (cmd == READY) 
  {
    Serial.print(HEXLIMITER+READY+HEXLIMITER);
    return;
  }

  else 
  {
    Serial.print(HEXLIMITER+ERROR+HEXLIMITER);
    Serial.print(VERBOSE_LIM+"- ERROR Description: I don't know what to do with the command: " + cmd+VERBOSE_LIM);
    return;
  }
}

// TODO: Add settings for print vs println
void sendMacroToKeyboard(int pressMacro) {
  if (pressMacro) {
    if (_pressMacro != "") {
      Keyboard.print(_pressMacro);
      return;
    } 
    else {
      readFromFile(PRESS_MACRO_FILENAME);
      if (_pressMacro != "") {
        Keyboard.print(_pressMacro);
        return;
      }
      Serial.print(VERBOSE_LIM+"Failed to find pressMacro to type!"+VERBOSE_LIM);
      return;
    }
  } 
  else {
    if (_tapMacro != "") {
      Keyboard.print(_tapMacro);
      return;
    }
    else {
      readFromFile(TAP_MACRO_FILENAME);
      if (_tapMacro != "") {
        Keyboard.print(_tapMacro);
        return;
      }
      Serial.print(VERBOSE_LIM+"Failed to find tapMacro to type!"+VERBOSE_LIM);
      return;
    }
  }
}

// Writes a String to a file found by filename. Creates it if one does not exist
String writeToFile(String fileName, String contents) {
  File file = SD.open(fileName, O_READ | O_WRITE | O_CREAT); //FILE_WRITE includes O_APPEND which I don't want
  if (!file) {
    return ERROR;
  }
  file.println(contents);
  file.close();
  Serial.print(VERBOSE_LIM+"Save operation completed, attempting to read back: "+VERBOSE_LIM);
  return readFromFile_AssertFileExists(fileName);
}

// Gets a single line from a file that is found by fileName
String readFromFile(String fileName) {
  File file = SD.open(fileName);
  if (file) {
    String response = file.readStringUntil('\n');
    file.close();
    response.trim(); // TODO: Do we need to support \n? We need to set it on the board a setting...
    if (fileName == TAP_MACRO_FILENAME) {
      _tapMacro = response;
    } else if (fileName == PRESS_MACRO_FILENAME) {
      _pressMacro = response;
    }
    return response;
  }
  else {
    if (!SD.exists(fileName)) {
      Serial.print(VERBOSE_LIM+"Requested file does not exist, initializing now!"+VERBOSE_LIM);
      file = SD.open(fileName, FILE_WRITE);
      file.println(DEFAULT_MACRO);
      file.close();
      if (fileName == TAP_MACRO_FILENAME) {
        _tapMacro = DEFAULT_MACRO;
      } else if (fileName == PRESS_MACRO_FILENAME) {
        _pressMacro = DEFAULT_MACRO;
      }
      return DEFAULT_MACRO;
    }
    return ERROR;
  }
}

String readFromFile_AssertFileExists(String fileName) {
  File file = SD.open(fileName);
  if (file) {
    String response = file.readStringUntil('\n');
    file.close();
    response.trim(); // TODO: Do we need to support \n? We need to set it on the board a setting...
    if (fileName == TAP_MACRO_FILENAME) {
      _tapMacro = response;
    } else if (fileName == PRESS_MACRO_FILENAME) {
      _pressMacro = response;
    }
    return response;
  }
  else {
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
    Serial.print(VERBOSE_LIM+"Board: I do not see an SD card or module, since this was going to be encased you should probably call Garrison at this point..."+VERBOSE_LIM);
  }
}

// Simple helper that prints the entire card's contents, starting from the root
void logCardContents() {
  if (!sdFound) {
    return;
  }
  Serial.print(VERBOSE_LIM+"=== Logging Card Contents ===\n"+VERBOSE_LIM);
  File root = SD.open("/");
  Serial.print(VERBOSE_LIM);
  printDirectory(root, 0);
  root.close();
  Serial.print(VERBOSE_LIM);
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
