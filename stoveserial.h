#include "esphome.h"

#define stoveStateAddr 0x21
#define ambTempAddr 0x01
#define fumesTempAddr 0x5A
#define flamePowerAddr 0x34
#define speedFanFumesAddr 0x37
#define intakeAirTempAddr 0x3C //0x73

/*
const char stoveOn[4] = {0x80, 0x21, 0x01, 0xA2};
const char stoveOff[4] = {0x80, 0x21, 0x06, 0xA7};
const char forceOff[4] = {0x80, 0x21, 0x00, 0xA1};
*/

class StoveSensor : public Component , public UARTDevice {
 public:
  StoveSensor(UARTComponent *parent) : UARTDevice(parent) {}

    long previousMillis;
    uint8_t stoveState, fumesTemp, flamePower,speedFanFumes;
    float ambTemp, intakeAirTemp;
    char stoveRxData[2]; //When the heater is sending data, it sends two bytes: a checksum and the value
    
    Sensor *stoveStatus_sensor = new Sensor();
    Sensor *ambTemp_sensor = new Sensor();
    Sensor *fumesTemp_sensor = new Sensor();
	Sensor *flamePower_sensor = new Sensor();
    Sensor *speedFanFumes_sensor = new Sensor();
	Sensor *intakeAirTemp_sensor = new Sensor();
	
	
	FS* fileSystem = &SPIFFS;
	File file;
	bool success= false;
	
	bool InitalizeFileSystem() {
	   bool initok = false;
	   initok = fileSystem->begin();
	   if (!(initok)) // format SPIFS, non formattato. - Prova 1   
	   {
		   ESP_LOGI("INFO","File system SPIFFS formattato.");
		   fileSystem->format();
		   initok = fileSystem->begin();
	   }   
	   if (!(initok)) // formatta SPIFS. - Prova 2   
	   {
		   fileSystem->format();
		   initok = fileSystem->begin();
	   }   
	   if (initok) {
			ESP_LOGI("INFO","SPIFFS è OK");
	   } 
	   else { 
			ESP_LOGI("INFO","SPIFFS non è OK");
	   }   
	   return initok;
	}
	
    void setup() override {
		success = InitalizeFileSystem();
    }
    
    void loop() override {
        unsigned long currentMillis = millis();
        if (previousMillis > currentMillis)
        {
            previousMillis = 0;
        }
        if (currentMillis - previousMillis >= 10000)
        {
            previousMillis = currentMillis;
			getdump();
			delay(3000);
            //getStates();
            //delay(3000);
        }
    }

    void getdump() 
    {
		if (success) {
			ESP_LOGI("OK", "File system mounted with success");
		} else {
			ESP_LOGI("ERROR", "Error mounting the file system");
		return;
		}
		file = fileSystem->open("/file.txt", "w");
		if (!file) {
			ESP_LOGI("ERROR", "Error opening file for writing");
			return;
		}
		for (int shift = 240; shift < 255; shift++) {
			readStoveRegister((byte)shift);
		}
		file.close();
	}

    void readStoveRegister(byte address) //Get detailed stove state EEPROM
	{
		const byte readByte = 0x20;
		write(readByte);
		delay(1);
		write(address);
		delay(80);
		uint8_t rxCount = 0;
        stoveRxData[0] = 0x00;
        stoveRxData[1] = 0x00;
        while (available()) //It has to be exactly 2 bytes, otherwise it's an error
        {
            stoveRxData[rxCount] = read();
            rxCount++;
        }
        if (rxCount == 2)
        {
            byte val = stoveRxData[1];
            byte checksum = stoveRxData[0];
            byte param = checksum - val;
			file.print(String(param).c_str());
			file.print("-");
			file.println(String(val).c_str());
		}
	}
	
    void getStates() //Calls all the get…() functions
    {
		ESP_LOGV("custom", "Getting States...");
		getState(stoveStateAddr);
        delay(10);
        getState(ambTempAddr);
        delay(10);
        getState(fumesTempAddr);
        delay(10);
        getState(flamePowerAddr);
        delay(10);
        getState(speedFanFumesAddr);
        delay(10);
        getState(intakeAirTempAddr);
    }
	
    void getState(byte msgAddr) //Get detailed stove state RAM
    {
        const byte readByte = 0x00;
        write(readByte);
        delay(1);
        write(msgAddr);
        delay(80);
        checkStoveReply();
    }

    void checkStoveReply() //Works only when request is RAM
    {
        uint8_t rxCount = 0;
        stoveRxData[0] = 0x00;
        stoveRxData[1] = 0x00;
        while (available()) //It has to be exactly 2 bytes, otherwise it's an error
        {
            stoveRxData[rxCount] = read();
            rxCount++;
        }
        if (rxCount == 2)
        {
			//ESP_LOGV("custom", "checkStoveReply...");
            byte val = stoveRxData[1];
            byte checksum = stoveRxData[0];
            byte param = checksum - val;
			ESP_LOGI("DUMP", "%s - %s" ,String(param).c_str(),String(val).c_str());
            switch (param)
            {
				case stoveStateAddr:
					stoveState = val;
					stoveStatus_sensor->publish_state(stoveState);
					break;
				case ambTempAddr:
					ambTemp = (float)val / 2;
					ambTemp_sensor->publish_state(ambTemp);
					//ESP_LOGV("ambtemp","%s" ,String(ambTemp).c_str());
					break;
				case fumesTempAddr:
					fumesTemp = val;
					fumesTemp_sensor->publish_state(fumesTemp);
					break;
				case flamePowerAddr:
					if ((stoveState < 6) && (stoveState > 0))
					{
						flamePower = val; //map(val, 0, 16, 10, 100);
						flamePower_sensor->publish_state(flamePower);
					}
					else
					{
						flamePower = 0;
						flamePower_sensor->publish_state(flamePower);
					}				
					break;
				case speedFanFumesAddr:
					speedFanFumes = val;
					speedFanFumes_sensor->publish_state(speedFanFumes);
					break;
				case intakeAirTempAddr:
					intakeAirTemp = (float)val;//(float)val / 2;
					intakeAirTemp_sensor->publish_state(intakeAirTemp);
					break;
            }
        }
    }
  
};