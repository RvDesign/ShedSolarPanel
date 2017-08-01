#include <SPI.h>
#include <Adafruit_MAX31855.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Time.h>
#include <DS1307RTC.h>

#include <Soladin.h>
//#include <SoftwareSerial.h>
#include <NeoSWSerial.h>
#define SoftwareSerial NeoSWSerial

// Define IO pins
//MAX31855 thermocouple
#define VICTRONMPPT_RX 2
#define SOLADIN_RX     3
#define TK1_CS         4
#define TK_DATA          5
#define DO_PV_OR_CHARGER 6
#define DO_LIGHT_ON      7
#define SOLADIN_TX       8
#define FREE_IN_OUT_9    9
#define RF24_CE    10
#define RF24_MOSI  11
#define RF24_MISO  12
#define RF24_SCK   13

// Soladin Soft Serial
//VICTRON Soft Serial
//INPUTS
// OUTPuTS

#define VICTRONMPPT_TX A0
#define IN_RAIN     A1
#define IN_LOAD     A2
#define IN_AUX2     A3
#define RTC_SDA     A4
#define RTC_SCL     A5
#define RF24_CN     A6
#define TK_CLK      A7

unsigned long gTime;

boolean gChargerON;
int gPanelVoltage;
unsigned long gLightOnTime;

enum STATE
{
  eSwitchToCharger = 1,
  eWaitCharger,
  eReadPVvoltage,
  eCheckPVvoltage,
  eCheckChargerState,
  eNoSoladinAvailable,
};

STATE eState;

enum LIGHTSTATE
{
  eLightON = 1,
  eLightOFF,
  eSunRise,
  eSunDown,
};
LIGHTSTATE eLightState, prevLightState;
// Memory pool for JSON object tree.
//
// Inside the brackets, 500 is the size of the pool in bytes.
// If the JSON object is more complex, you need to increase that value.
StaticJsonBuffer<64> gJsonBuffer;

void (*SoftReset)(void) = 0; //declare reset function @ address 0

void Log(const char* module, const char* message)
{

  Serial.print(millis());
  Serial.print(F("\tLog: ["));

  Serial.print(F("] "));

  if (strlen(module) > 0)
  {
    Serial.print(F(": "));
    Serial.print(module);
    Serial.print(" ");
  }

  Serial.println(message);
}

class clsReadTemperature
{
  public:
    clsReadTemperature(int aMAXCLK, int aMAXCS, int aMAXDO, const char* aId) :
      mTime(millis()), mThermocouple(aMAXCLK, aMAXCS, aMAXDO), mJson(
          gJsonBuffer.createObject()), mId(aId)
  {

  }
    ~clsReadTemperature()
    {

    }
    void readTemperature()
    {
      if (millis() > mTime)
      {
        mTime = millis() + 5000;
        mJson["temperature sensor"] = mId;
        mJson["internal temperature"] = mThermocouple.readInternal();
        double c = mThermocouple.readCelsius();
        if (isnan(c))
        {
          mJson["error"] = "sensor not working";
        }
        else
        {
          mJson["external temperature"] = c;
        }
        mJson.prettyPrintTo(Serial);
      }
    }

  private:
    unsigned long mTime;
    Adafruit_MAX31855 mThermocouple;
    JsonObject& mJson;
    const char* mId;

};

//clsReadTemperature gReadTemperatureTK1(TK_CLK, TK1_CS, TK_DATA, "solarpanel");

class clsReadTime
{
  public:
    clsReadTime()
    {
      mTime = millis();
      mPrint = false;
    }
    void readTime()
    {
      if (millis() > mTime)
      {
        mTime = millis() + 10000;
        tmElements_t lTm;

        if (RTC.read(lTm))
        {
          if (mPrint)
          {
            Serial.print("Ok, Time = ");
            print2digits(lTm.Hour);
            Serial.write(':');
            print2digits(lTm.Minute);
            Serial.write(':');
            print2digits(lTm.Second);
            Serial.print(", Date (D/M/Y) = ");
            print2digits(lTm.Day);
            Serial.write('/');
            print2digits(lTm.Month);
            Serial.write('/');
            Serial.print(tmYearToCalendar(lTm.Year));
            Serial.println();
          }
        }
        else
        {
          if (RTC.chipPresent())
          {
            Log("", "DS1307 is stopped.  Please run the SetTime");
            setupRTC();

          }
          else
          {
            Log("", "DS1307 read error!  Please check the circuitry.");
          }
        }
      }
    }
  private:
    const char* mMonthName[12] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov",
      "Dec" };
    tmElements_t mTm;
    unsigned long mTime;
    bool mPrint;
    bool setupRTC()
    {
      bool lRet = false;

      // get the date and time the compiler was run
      if (getDate(__DATE__) && getTime(__TIME__))
      {
        lRet = true;
        // and configure the RTC with this info
        RTC.write(mTm);
      }
      return lRet;
    }

    bool getTime(const char *str)
    {
      int Hour, Min, Sec;

      if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
      mTm.Hour = Hour;
      mTm.Minute = Min;
      mTm.Second = Sec;
      return true;
    }

    bool getDate(const char *str)
    {
      char Month[12];
      int Day, Year;
      uint8_t lMonthIndex;

      if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
      for (lMonthIndex = 0; lMonthIndex < 12; lMonthIndex++)
      {
        if (strcmp(Month, mMonthName[lMonthIndex]) == 0) break;
      }
      if (lMonthIndex >= 12) return false;
      mTm.Day = Day;
      mTm.Month = lMonthIndex + 1;
      mTm.Year = CalendarYrToTm(Year);
      return true;
    }
    void print2digits(int number)
    {
      if (number >= 0 && number < 10)
      {
        Serial.write('0');
      }
      Serial.print(number);
    }

};

//clsReadTime gReadTime;

class clsProcessSoladin
{
  public:
    clsProcessSoladin()
    {
      mSoladinCom = NULL;
      mTime = millis();
      eSoladinState = eMakeConnection;
      mDataValid = false;
    }

    ~clsProcessSoladin()
    {
      if (mSoladinCom != NULL)
      {
        //delete mSoladinCom;
      }
    }
    void Setup()
    {
      mSoladinCom = new SoftwareSerial(SOLADIN_RX, SOLADIN_TX); // serial connect to pin 2 RX and 3 TX
      mSoladinCom->begin(9600);
      mSoladinInverter.begin(mSoladinCom);

      mDataValid = false;

    }
    void Loop()
    {
      switch (eSoladinState)
      {
        case eMakeConnection:
          {
            mSoladinCom->listen();
            Log(mId, "Cmd Probe");

            mDataValid = false;

            if (mSoladinInverter.query(PRB))
            { // Try connecting to slave
              eSoladinState = eConnected;
            }
            else
            {
              mTime = millis() + 5000;
              eSoladinState = eRetryToConnect;
              Log(mId, "...RetryToConnect");
            }
            break;
          }

        case eRetryToConnect:
          {
            if (millis() > mTime)
            {
              eSoladinState = eMakeConnection;
            }
            break;
          }

        case eConnected:
          {

            Log(mId, "...Connected");

            eSoladinState = eReadDeviceStatus;

            break;
          }

        case eReadDeviceStatus:
          {
            Log(mId, "ReadDeviceStatus");
            mSoladinCom->listen();
            if (mSoladinInverter.query(DVS))
            {
              eSoladinState = eWait;

              mTime = millis() + 2000;
            }
            else
            {
              eSoladinState = eMakeConnection;
            }

            break;
          }
        case eWait:
          {
            if (millis() > mTime)
            {
              eSoladinState = ePrintData;
              mDataValid = true;
            }
            break;
          }
        case ePrintData:
          {
            Log(mId, "PrintData");
            Serial.print("PV= ");
            Serial.print(float(mSoladinInverter.PVvolt) / 10);
            Serial.print("V;   ");
            Serial.print(float(mSoladinInverter.PVamp) / 100);
            Serial.println("A");

            Serial.print("AC= ");
            Serial.print(mSoladinInverter.Gridpower);
            Serial.print("W;  ");
            Serial.print(float(mSoladinInverter.Gridfreq) / 100);
            Serial.print("Hz;  ");
            Serial.print(mSoladinInverter.Gridvolt);
            Serial.println("Volt");

            Serial.print("Device Temperature= ");
            Serial.print(mSoladinInverter.DeviceTemp);
            Serial.println(" Celcius");

            Serial.print("AlarmFlag= ");
            Serial.println(mSoladinInverter.Flag, BIN);

            Serial.print("Total Power= ");
            Serial.print(float(mSoladinInverter.Totalpower) / 100);
            Serial.println("kWh");
            // I really don't know, wy i must split the sprintf ?
            Serial.print("Total Operating time= ");
            char timeStr[14];
            sprintf(timeStr, "%04ld:", (mSoladinInverter.TotalOperaTime / 60));
            Serial.print(timeStr);
            sprintf(timeStr, "%02ld hh:mm ", (mSoladinInverter.TotalOperaTime % 60));
            Serial.println(timeStr);
            Serial.println();

            eSoladinState = eMakeConnection;
            break;
          }
        default:
          {
            //nothing here
          }
      }

    }
    int getPanelVoltage()
    {
      // returns in 100'th volts
      return mSoladinInverter.PVvolt * 10;
    }
    boolean isDataValid()
    {
      return mDataValid;
    }

  private:
    unsigned long mTime = 0l; // Time used in the statemachine.
    const char* mId = "soladin";SoftwareSerial* mSoladinCom = NULL;
    Soladin mSoladinInverter;             // copy of soladin class
    boolean mDataValid;
    enum SOLADINSTATE
    {
      eMakeConnection = 1,
      eRetryToConnect,
      eConnected,
      eReadDeviceStatus,
      eWait,
      ePrintData
    };
    SOLADINSTATE eSoladinState;
};

clsProcessSoladin gProcessSoladin;

class clsVictronMPPT
{

  public:
    clsVictronMPPT() :
      mReceivedStringLength(0), 
      mHexChars      { "0123456789ABCDEF" }, 
      mGetMPPTChargerVolts      { ":7D5ED008C\n" }, 
      mGetMPPTChargerCurrent      { ":7D7ED008A\n" }, 
      mGetMPPTPanelVolts      { ":7BBED00A6\n" }, 
      mGetMPPTPanelPower      { ":7BCED00A5\n" }, 
      mGetMPPTDeviceMode      { ":7000200004C\n" }, 
      mGetMPPTDeviceState      { ":70102004B\n" }, 
      mGetMPPTLoadOuputState      { ":7A8ED00B9\n" },
      mGetMPPTMaxCurrent      { ":7F0ED0071\n" }, 
      mGetMPPTBatterySafeMode      { ":7FFED0062\n" },
      mGetMPPTExtraInfo      { ":7D4ED008D\n" },
      mGetMPPTAbsorptionVolts      { ":7F7ED006A\n" },
      mGetMPPTLoadCurrent      { ":7DAED0087\n" },
      mMPPT(NULL), mPanelVolts(0), mPanelPower(0), mChargerVolts(
          0), mChargerAmps(0), mChargerMode(0), mChargerState(0), mLoadCurrent(
            0), mValidData(false), mTime(0), mCharNumber(0)

          {
            // avoided any Arduino String functions. Use char arrays throughout. Arduino adds ‘\0’ to signify end of string.
#define RECEIVEDSTRING_LENGTH 25

          }

    ~clsVictronMPPT()
    {
      /*     if (mMPPT != NULL)
             {
             delete mMPPT;
             mMPPT = NULL;
             }
       */
    }

    void setup()
    {
      mMPPT = new SoftwareSerial(VICTRONMPPT_TX, VICTRONMPPT_RX);
      mMPPT->begin(19200);

      clearReceivedString();
      mValidData = false;
    }

    void loop()
    {
      if (millis() > mTime)
      {
        mTime = millis() + 1000;
        //Log("MPPT", "Loop");

        mValidData = false;
        Log("MPPT", "volts");
        // get Volts
        if (readVictron(*mMPPT, mGetMPPTChargerVolts, 14, mChargerVolts, 1, 1,
              true))
        {

          Log("MPPT", "state");
          // get State
          if (readVictron(*mMPPT, mGetMPPTDeviceState, 12, mChargerState, 1, 1,
                true))
          {
            mValidData = true;

            Log("MPPT", "PANEL volts");
            // get Panel Volts
            if (readVictron(*mMPPT, mGetMPPTPanelVolts, 14, mPanelVolts, 1, 1,
                  true))
            {

              Log("MPPT", "amps");
              // get Amps
              if (readVictron(*mMPPT, mGetMPPTChargerCurrent, 14, mChargerAmps, 1,
                    1, true))
              {
                Log("MPPT", "load current");
                // get State
                if (readVictron(*mMPPT, mGetMPPTLoadCurrent, 12, mLoadCurrent, 1,
                      1, true))
                {
                  mValidData = true;
                }
              }
            }
          }
        }

        if (mValidData)
        {

          Serial.print("getBateryVoltage() ");
          Serial.print((float)getBatteryVoltage()/100.0);
          Serial.println("V");

          Serial.print("getBateryamps() ");
          Serial.print((float)getBatteryAmps()/10.0);
          Serial.println("A");

          Serial.print("getPanelVoltage() ");
          Serial.print((float)getPanelVoltage()/100.0);
          Serial.print("V ");
          Serial.print(getPanelPower());
          Serial.println("W");

          Serial.print("getState() ");
          Serial.print(getState());
          Serial.println("-");
        }
        else
        {

          //Serial.println("No Data received of MPPT device.");
        }
      }
    }

    boolean isDataValid()
    {
      return mValidData;
    }

    boolean isFloat()
    {
      // Returns true when the chargers in in float state.
      return (mChargerState >= 5 ? true : false);
    }

    int getState()
    {
      return mChargerState;
    }

    int getBatteryVoltage()
    {
      return mChargerVolts;
    }

    int getChargerAmps()
    {
      return mChargerAmps;
    }
    int getLoadCurrent()
    {
      return mLoadCurrent;
    }
    int getBatteryAmps()
    {
      return mChargerAmps - mLoadCurrent;
    }
    int getPanelVoltage()
    {
      // Return in 10th volts
      return mPanelVolts;
    }

    float getPanelPower()
    {
      return ((float)mChargerAmps * (float)mChargerVolts / 1000.0);
    }

  private:

    // avoided any Arduino String functions. Use char arrays throughout. Arduino adds ‘\0’ to signify end of string.
    char mReceivedString[RECEIVEDSTRING_LENGTH];
    int mReceivedStringLength;

    // all communication is ascii, for Victron its ascii representation of hex, not hex
    // part of simple conversion from byte to ascii hex
    const char mHexChars[17];

    // Victron MPPT hex protocol commands
    // has newline which Victron expects, use print as println without  ‘\n’ produces unreliable responses
    const char mGetMPPTChargerVolts[12];
    const char mGetMPPTChargerCurrent[12];
    const char mGetMPPTPanelVolts[12];
    const char mGetMPPTPanelPower[12];
    const char mGetMPPTDeviceMode[14];
    const char mGetMPPTDeviceState[12];
    const char mGetMPPTLoadOuputState[12];

    const char mGetMPPTMaxCurrent[12];
    const char mGetMPPTBatterySafeMode[12];
    const char mGetMPPTExtraInfo[12];
    const char mGetMPPTAbsorptionVolts[12];
    const char mGetMPPTLoadCurrent[12];

    SoftwareSerial* mMPPT;

    int mPanelVolts;
    int mPanelPower;
    int mChargerVolts;
    int mChargerAmps;
    int mChargerMode;
    int mChargerState;
    int mLoadCurrent;
    boolean mValidData;
    unsigned long mTime;
    int mCharNumber;

    boolean victronCheckSum(char* testString, int testStringLength)
    {

      // checks existing check sum
      // using byte instead of int avoids explicitly wrapping checkSum as it overflows
      byte checkSum = 85;

      checkSum = checkSum - x2i(&testString[1], 1);

      for (int i = 2; i < testStringLength; i += 2)
      {
        checkSum = checkSum - x2i(&testString[i], 2);
      }

      if (checkSum == 0)
      {
        return true;
      }
      else
      {
        return false;
      }
    }

    // borrowed from http://forum.arduino.cc/index.php?topic=123486.0
    // converts ascii hex to integer
    // use instead of strtoul so can refer to chars in a char array
    // modified to limit length of "substring" of char array and avoid overflows
    byte x2i(char *s, byte numChars)
    {
      byte x = 0;
      for (byte i = 0; i < numChars; i++)
      {
        char c = *s;
        if (c >= '0' && c <= '9')
        {
          x *= 16;
          x += c - '0';
        }
        else if (c >= 'A' && c <= 'F')
        {
          x *= 16;
          x += (c - 'A') + 10;
        }
        else if (c >= 'a' && c <= 'f')
        {
          x *= 16;
          x += (c - 'a') + 10;
        }
        else break;
        s++;
      }
      return x;
    }
    void clearReceivedString()
    {

      // just in case, clear string before use
      memset(mReceivedString, ' ', RECEIVEDSTRING_LENGTH);
      mReceivedString[RECEIVEDSTRING_LENGTH - 1] = '\0';
      mCharNumber = 0;
    }

    boolean addreadchar(char aChar)
    {
      if (mCharNumber < RECEIVEDSTRING_LENGTH)
      {
        mReceivedString[mCharNumber] = aChar;
        mCharNumber++;
      }
      return true;
    }
    boolean readVictron(SoftwareSerial &Victron, const char* request,
        int expectedLength, int &value, int multiplier, int tries,
        boolean printRead)
    {

      // the guts of the communication with Victron, send message and process response

      clearReceivedString();
      char characterRead = ' ';

      // set true when ‘:’ read
      boolean startFound = false;
      unsigned long oldMillis = millis();

      // count number of tries
      int counterTries = 0;

      // count number of characters received
      int counterChars = 0;

      // for return
      bool success = false;

      Victron.listen();
      //delay(10);

      // printRead is rudimentary debug so we can see what is happening
      if (printRead)
      {
        Log("Vict", request);
      }

      // main send/receive loop
      while (counterTries < tries && success == false)
      {
        // keep listening to Victron for up to 15mS.
        oldMillis = millis() + 15;

        // send request to Victron
        Victron.print(request);
        //delay(1);
        if (expectedLength > 0)
        {
          // keep listening to Victron
          while (millis() < oldMillis)
          {
            counterChars = 0;
            while (Victron.available())
            {
              characterRead = Victron.read();
              counterChars += 1;
              if (printRead)
              {
                Serial.print(characterRead);
              }
              if (characterRead == ':')
              {
                if (printRead)
                {
                  Log("Vict", " start ");
                }
                startFound = true;
                addreadchar(characterRead);
              }
              else if ((characterRead == '\n' && startFound == true)
                  || mCharNumber > expectedLength)
              {
                addreadchar(characterRead);

                if (characterRead == '\n' && startFound == true)
                {
                  if (printRead)
                  {
                    Log("Vict", "Success");
                  }
                  success = true;
                  break;
                }
                if (printRead)
                {
                  Log("Vict", " line end or count ");            
                }
                break;
              }
              else if (startFound)
              {
                addreadchar(characterRead);
              }
              else if (counterChars > 20)
              {
                // avoid mReceivedString overflow
                if (printRead)
                {
                  Log("Vict", " No start or end ");
                }
                break;
              }
            }
            // 25msec should be enough but no need to wait
            if (mCharNumber > expectedLength)
            {
              if (printRead)
              {
                Serial.print("expected, ");
                Serial.print(expectedLength);
                Serial.print(" ");
                Serial.print(mCharNumber);
                Serial.print("\n");

                Log("Vict", " Received");
              }
              break;
            }
          }
        }
        counterTries += 1;
        if (printRead)
        {
          Serial.print(counterTries);
          Serial.println(" Tries ");
        }
      }

      // response has been received, now check it

      // is response expected length, checksum correct, matches query
      if (mCharNumber - 1 == expectedLength)
      {
        // check checksum
        if (victronCheckSum(mReceivedString, expectedLength))
        {
          // Victron response contains request register
          boolean validAnswer = true;
          for (int i = 2; i < 6; i++)
          {
            if (request[i] != mReceivedString[i])
            {
              validAnswer = false;
              return validAnswer;
            }
          }
          // response is correct for request so convert ascii hex to integer and apply multiplier
          if (validAnswer)
          {
            if (expectedLength == 14)
            {
              value = (x2i(&mReceivedString[10], 2) * 256
                  + x2i(&mReceivedString[8], 2));
              value = value * multiplier;
              return true;
            }
            else if (expectedLength == 12)
            {
              value = x2i(&mReceivedString[8], 2);
              value = value * multiplier;
              return true;
            }
            else if (expectedLength == 10)
            {
              value = x2i(&mReceivedString[6], 2);
              value = value * multiplier;
              return true;
            }
            else if (expectedLength == 18)
            {
              // the response to enableOnOff is unique
              value = 0;
              return true;
            }
            else
            {
              // response doesn't match request
              if (printRead)
              {
                Serial.println("invalid");
              }
              return false;
            }
          }
        }
      }
      else
      {
        // wrong length
        if (printRead)
        {
          Serial.println(" wrong length");
          Serial.print(" expected, ");
          Serial.print(expectedLength);
          Serial.print(" ");
          Serial.print(mCharNumber);
          Serial.print("\n");

        }
        clearReceivedString();
        return false;
      }
      return true;
    }

};

clsVictronMPPT gVictronMPPT;

unsigned char gNoSoladinAvailable;

// standard arduino functions
void setup()
{
  Serial.begin(19200);
  while (!Serial)
  {
    // wait for the serial port to initialise
  }

  pinMode(DO_PV_OR_CHARGER, OUTPUT);
  pinMode(DO_LIGHT_ON, OUTPUT);
  pinMode(IN_LOAD, INPUT);
  pinMode(IN_AUX2, INPUT);
  pinMode(IN_RAIN, INPUT);
  digitalWrite(DO_PV_OR_CHARGER, LOW);
  digitalWrite(DO_LIGHT_ON, LOW);

  gProcessSoladin.Setup();

  gTime = millis();
  eState = eSwitchToCharger;
  gPanelVoltage = 0;
  gLightOnTime = 0;

  gVictronMPPT.setup();

  eLightState = eLightOFF;
  prevLightState = eLightON;
#define NO_DATA_AVAILABLE 25
  gNoSoladinAvailable = NO_DATA_AVAILABLE;
}

bool inLoad()
{
  // Invert the input
  return !digitalRead(IN_LOAD);
}

void outputState(String& aString)
{

}

void ChargerOn(boolean aON)
{
  gChargerON = aON;
  if (aON)
  {
    digitalWrite(DO_PV_OR_CHARGER, LOW);
  }
  else
  {
    digitalWrite(DO_PV_OR_CHARGER, HIGH);
  }
}

void handleInverter()
{

  /*
     Switch to charger
     Read PVvoltage
     If PVVoltage is < 12Volt then SwitchLights ON , Switch to charger
     If PVVoltage is > 12,5Volt then SwitchLights OFF
     If lightON time > PVVoltage is > 12,5Volt then SwitchLights OFF

     If Charger state is absorbtion switch to solar inverter
     GoTo ReadPVvoltage
   */

  switch (eState)
  {
    case eSwitchToCharger:
      {
        Log("chrg", "Charger ON");
        // Charger ON
        ChargerOn(true);
        gTime = millis() + 1000;
        eState = eWaitCharger;
      }
      break;
    case eWaitCharger:
      {
        if (millis() > gTime)
        {
          gTime = millis() + 1000;
          if (gChargerON)
          {
            if (gVictronMPPT.isDataValid())
            {
              eState = eReadPVvoltage;
            }
          }
          else
          {
            if (gProcessSoladin.isDataValid())
            {
              eState = eReadPVvoltage;
              gNoSoladinAvailable = NO_DATA_AVAILABLE;
            }
            else
            {
              if (gNoSoladinAvailable)
              {
                gNoSoladinAvailable--;
              }
              else
              {
                eState = eNoSoladinAvailable;
              }
            }
          }
        }
        break;
      }

    case eNoSoladinAvailable:
      {
        // Charger is OFF and for a long time no communication with the Soladin. 
        // It looks like the Sun is down and no Power from the solarpanel.
        // Switch to Charger

        eState = eSwitchToCharger; 
        break;
      }

    case eReadPVvoltage:
      {
        Log("chrg", "ReadPVvoltage high");
        if (gChargerON)
        {
          gPanelVoltage = gVictronMPPT.getPanelVoltage();
        }
        else
        {
          gPanelVoltage = gProcessSoladin.getPanelVoltage();
        }

        eState = eCheckPVvoltage;

      }
      break;
    case eCheckPVvoltage:
      {
        Log("chrg", "checkpvvoltage");

        if (eLightState == eSunDown)
        {
          if (millis() > gLightOnTime)
          {
            // Lights OFF
            eLightState = eLightOFF;
          }
          else
          {
            Serial.print((gLightOnTime - millis()) / 1000);
            Serial.println(" sec. time left");
          }
        }

        if (gPanelVoltage < 1200)
        {
          // SunDown
          eLightState = eSunDown;
        }
        if (gPanelVoltage > 1250)
        {
          // SunRise
          eLightState = eSunRise;
        }

        eState = eCheckChargerState;
        break;
      }
    case eCheckChargerState:
      {
        Log("chrg", "eCheckChargerState");
        if (gVictronMPPT.isFloat())
        {
          ChargerOn(false);
        }

        gTime = millis() + 1000;

        eState = eWaitCharger;
        break;
      }

    default:
      {

      }
  }

  if (eLightState != prevLightState)
  {
    prevLightState = eLightState;

    switch (eLightState)
    {

      case eSunDown:
        {
          Log("chrg", "SunDown");
          // Calculate the time to switch off 360 minutes
          gLightOnTime = millis() + (360l * (1000l * 60l));
          digitalWrite(DO_LIGHT_ON, HIGH);
          break;
        }
      case eSunRise:
      case eLightOFF:
        {
          Log("chrg", "SunRise/LightOFF");
          digitalWrite(DO_LIGHT_ON, LOW);
          break;
        }

      default:
        {

        }
    }
  }

}

void loop()
{
  //  Log("loop","inverter");
  handleInverter();
  //  Log("loop","temp TK1");
  //  gReadTemperatureTK1.readTemperature();
  //  Log("loop","Soladin");
  gProcessSoladin.Loop();
  //  Log("loop","MPPT");
  gVictronMPPT.loop();
  //  Log("loop","time");
  //gReadTime.readTime();
}


