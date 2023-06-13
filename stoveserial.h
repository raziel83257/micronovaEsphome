#include "esphome.h"
using namespace esphome;

#define stoveStateAddr 0x21
#define ambTempAddr 0x01
#define fumesTempAddr 0x5A
#define flamePowerAddr 0x34
#define speedFanFumesAddr 0x37
#define cocleaSpeedAddr 0x0D
//#define intakeAirTempAddr 0x73


class StoveSensor : public Component, public UARTDevice, public CustomMQTTDevice {
 public:
	String topic;
	StoveSensor(UARTComponent *parent,String l_topic) : UARTDevice(parent) {topic=l_topic;} //constructor
	
	String mqtt_topic;
	String pong_topic;
	char char_pong_topic[50];
	String dump_topic;
	char char_dump_topic[50];
	String in_topic;
	char char_in_topic[50];

	const char stoveOn[4] = {0x80, 0x21, 0x01, 0xA2};
	const char stoveOff[4] = {0x80, 0x21, 0x06, 0xA7};
	const char forceOff[4] = {0x80, 0x21, 0x00, 0xA1};
	
    long previousMillis;
	String stoveOnOff;
    int stoveState, fumesTemp, flamePower,speedFanFumes,cocleaSpeed;
    float ambTemp;
    char stoveRxData[2];	//When the heater is sending data, it sends two bytes: a checksum and the value
	
	bool g_isSerialWorking = false;	//check is serial to the stove is in use
    bool g_read_error=false;	//used to check if there is an error in reading
	int g_retry =0;	//retry for getstate
	int g_maxretry=3;	//num retry max for getstate
	unsigned long m_dly=100;	//delay for publish mqtt message
	
	void IRAM_ATTR fullReset()	//Reset all the settings but without erasing the program
	{
		Serial.println("Resetting…");
		ESP.restart();
	}

    void f_dump(const std::string payload)
    {
		dump_topic = mqtt_topic;
		dump_topic += "/dump";
		byte readByte = 0x00; //read RAM
		int readlimit = 151;
		if ("DUMP_EEPROM" == payload){	// check the payload , otherwise read RAM
			readByte = 0x20;readlimit=128; //read EEPROM
			dump_topic += "_eeprom";
		}
		dump_topic.toCharArray(char_dump_topic, 50);
		
		uint8_t retry=0;
		uint8_t maxretry = 5;
		while (true == g_isSerialWorking && retry < maxretry)
		{
			delay (500);
			retry++;
		}
		if (retry < maxretry)
		{
			g_isSerialWorking=true;
			
			ESP_LOGV("VERB", "f_dump cicle...");
			for (int shift = 0; shift < readlimit; shift++)
			{
				write(readByte);
				delay(10);
				write((byte)shift);
				delay(50);
				byte val = f_checkStoveReply((byte)shift,readByte);
				if (false == g_read_error)
				{   
					String value = String(shift);
					value += "-";
					value += String(val);
					publish(char_dump_topic, value.c_str(), true);
				}
			}
			g_isSerialWorking=false;
		}
	}
	
    void f_getStates() //Calls all the get…() functions
    {
		int retry=0;
		int maxretry = 5;
		while (true == g_isSerialWorking && retry < maxretry)
		{
			delay (500);
			retry++;
		}
		if (retry < maxretry)
		{
			g_isSerialWorking=true;
			ESP_LOGV("INFO", "Getting States...");
			f_getState(stoveStateAddr);
			f_getState(ambTempAddr);
			f_getState(fumesTempAddr);
			f_getState(flamePowerAddr);
			f_getState(speedFanFumesAddr);
			f_getState(cocleaSpeedAddr);
			//f_getState(intakeAirTempAddr);
			g_isSerialWorking=false;
			// publish JSON using lambda syntax
			publish_json("micronova/json", [=](JsonObject root2) {
				root2["stoveOnOff"] = stoveOnOff;
				root2["stoveState"] = stoveState;
				root2["ambTemp"] = ambTemp;
				root2["fumesTemp"] = fumesTemp;
				root2["speedFanFumes"] = speedFanFumes;
				root2["flamePower"] = flamePower;
				root2["cocleaSpeed"] = cocleaSpeed;
			});
		}
    }
	
    void f_getState(byte msgAddr) //Get detailed stove state RAM
    {
        const byte readByte = 0x00;
        write(readByte);
        delay(10);
        write(msgAddr);
        delay(50);
        byte val = f_checkStoveReply(msgAddr,readByte);
		if (false == g_read_error)
		{
            switch (msgAddr)
            {
				case stoveStateAddr:
					stoveState = val;
					if ((stoveState>0) & (stoveState<6)){stoveOnOff = "ON";}
					else {stoveOnOff = "OFF";}
					break;
				case ambTempAddr:
					ambTemp = (float)val / 2;
					break;
				case fumesTempAddr:
					fumesTemp = val;
					break;
				case flamePowerAddr:
					if ((stoveState < 6) && (stoveState > 0)){flamePower = val;}
					else{flamePower = 0;}
					break;
				case speedFanFumesAddr:
					if (val > 0){ speedFanFumes = (val *10) +250;}
					else {speedFanFumes =0;}		
					break;
				case cocleaSpeedAddr:
					cocleaSpeed = val;
					break;
            }
		}
		else
		{
			if (g_retry < g_maxretry)
			{
				g_retry++;
				f_getState(msgAddr);
			}
		}
		g_retry=0;		
    }
	
    byte f_checkStoveReply(byte msgAddr,byte location) // msgAddr fro RAM or EEPROM
    {
		g_read_error=false;
        uint8_t rxCount = 0;
        stoveRxData[0] = 0x00;
        stoveRxData[1] = 0x00;
        while (available()) //It has to be exactly 2 bytes, otherwise it's an error
        {
            stoveRxData[rxCount] = read();
            rxCount++;
        }
        if (2 == rxCount)
        {
            byte val = stoveRxData[1];
            byte checksum = stoveRxData[0];
            byte param = checksum - val;
			if (param != msgAddr+location)
			{
				g_read_error=true;
				return 255;
			}
			else
			{
				g_read_error=false;
				return val;
			}		
        }
		else {g_read_error=true;}
		return 255;
    }

	void f_on_message(const std::string &payload) {
		if ("ON" == payload)
		{
			for (int i = 0; i < 4; i++)
			{
				if (0 == stoveState || stoveState > 5)
				{
					write(stoveOn[i]);
					delay(5);
				}
			}
			delay(1000);
			f_getStates();
		}
		else if ("OFF" == payload)
		{
			for (int i = 0; i < 4; i++)
			{
				if (stoveState < 6 && stoveState > 0)
				{
					write(stoveOff[i]);
					delay(5);
				}
			}
			delay(1000);
			f_getStates();
		}
		else if ("DUMP_RAM" == payload || "DUMP_EEPROM" == payload)
		{			
			f_dump(payload);
			publish(char_dump_topic, "DUMP_END", true);
		}
		else if ( std::string::npos != payload.find("WRITE_",0))
		{
			int n = payload.length();
			// declaring character array
			char char_array[n];
			// copying the contents of the string to char array
			strcpy(char_array, payload.c_str());
			char * pch;
			pch = strtok(char_array, "_");
			pch = strtok (NULL,"_");
			char location[sizeof pch];
			strcpy(location, pch); //get location byte, 0=RAM 1=EEPROM
			pch = strtok (NULL,"-");
			char address[sizeof pch];
			strcpy(address, pch); //get address byte
			pch = strtok (NULL,"-"); 
			char value[sizeof pch];
			strcpy(value, pch);//get value byte
			uint8_t addr = atoi(address);
			uint8_t val = atoi(value);
			byte writeByte=0x80; //default write in RAM
			dump_topic = mqtt_topic;
			dump_topic += "/dump";
			if ('1' == location[0])
			{ dump_topic += "_eeprom";writeByte = 0xA0; }//write in EEPROM
			dump_topic.toCharArray(char_dump_topic, 50);		
			byte sum = writeByte + (byte)addr + (byte)val;
			char writeData[4];
			writeData[0]=writeByte;
			writeData[1]=addr;
			writeData[2]=val;
			writeData[3]=sum;
			for (int i = 0; i < 4; i++)
			{
				write(writeData[i]);
				delay(5);
			}
			delay(50);
			byte ret = f_checkStoveReply((byte)addr,writeByte);
			if (false == g_read_error)
			{
				String msg = String(addr);
				msg += "-";
				msg += String(ret);
				publish(char_dump_topic, msg.c_str(), true);
			}
			ESP_LOGV("INTOPIC", "END_WRITE");		
		}
		else if ("RESET" == payload)
		{
			fullReset();
		}
	}
	
    void setup() override {
		mqtt_topic = topic;
        mqtt_topic.trim();
        pong_topic += mqtt_topic;
        in_topic += mqtt_topic;
        pong_topic += "/pong";
        in_topic += "/intopic";		
		pong_topic.toCharArray(char_pong_topic, 50);       
        in_topic.toCharArray(char_in_topic, 50);
		subscribe(char_in_topic, &StoveSensor::f_on_message);
    }
    void loop() override {
        unsigned long currentMillis = millis();
        if (previousMillis > currentMillis)
        {
            previousMillis = 0;
        }
        if (currentMillis - previousMillis >= 5000)
        {
            previousMillis = currentMillis;
            f_getStates();
			publish(char_pong_topic, "Connected");
        }
    }
};
