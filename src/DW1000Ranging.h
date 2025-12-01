//Decawave DW1000 library for arduino. 

#ifndef _DW1000Ranging_H_INCLUDED
#define _DW1000Ranging_H_INCLUDED
#endif

#include "DW1000.h"
#include "DW1000Time.h"
#include "DW1000Device.h" 
#include "DW1000Mac.h"

// messages used in the ranging protocol
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
#define BLINK 4
#define RANGING_INIT 5

//Messages used to control the data flow: 
#define MODE_SWITCH 6 // To request a switch in mode. From initiator to responder (or viceversa)
#define MODE_SWITCH_ACK 7

#define REQUEST_DATA 8 // The master anchor sends this message to request the slave anchors the data they've collected (this data includes the measurements from the slave to the rest of devices)
#define DATA_REPORT 9 // The slave anchors respond with this message. In it, the requested data is codified.

#define STOP_RANGING 10
#define STOP_RANGING_ACK 11


//Length of tha payload in the sent messages.
#define LEN_DATA 90

//Max devices we put in the networkDevices array ! Each DW1000Device is 74 Bytes in SRAM memory for now.
#define MAX_DEVICES 10

//Default Pin for module:
#define DEFAULT_RST_PIN 9
#define DEFAULT_SPI_SS_PIN 10

//Default value
//in ms
#define DEFAULT_RESET_PERIOD 500
//in us
#define DEFAULT_REPLY_DELAY_TIME 7000

//sketch type (responder or Initiator)
#define INITIATOR 0
#define RESPONDER 1

//default timer delay
#define DEFAULT_TIMER_DELAY 80

//debug mode
#ifndef DEBUG
#define DEBUG false
#endif

// Struct to handle the known measurements among the system's devices:
struct Measurement {
    uint8_t short_addr_origin;   
    uint8_t short_addr_dest;
    float distance;     // Last measured distance (in meters)
    float rxPower;      // Last RX power measured with the destiny (in dBm)
    bool active;        // Checks if the destiny device is active. 
};

// Struct to know the existing devices of the system. Used to send messages via unicast.
struct ExistingDevice{
	
	uint8_t short_addr;
	byte byte_short_addr[2];
	bool is_slave_anchor;
	bool is_responder;
	bool active;
	bool mode_switch_pending;
	bool data_report_pending;
	
};



class DW1000RangingClass {
public:
	//variables
	// data buffer
	static byte data[LEN_DATA];
	
	//initialisation
	static void    initCommunication(uint8_t myRST = DEFAULT_RST_PIN, uint8_t mySS = DEFAULT_SPI_SS_PIN, uint8_t myIRQ = 2);
	static void    configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]);
	static void    generalStart();
	static void    startAsResponder(const char address[], const byte mode[], const bool randomShortAddress = true,const uint8_t boardType = 0);
	static void    startAsInitiator(const char address[], const byte mode[], const bool randomShortAddress = true, const uint8_t boardType = 0);
	static bool addNetworkDevices(DW1000Device* device, bool shortAddress);
	static bool addNetworkDevices(DW1000Device* device);
	static void    removeNetworkDevices(int16_t index);
	
	//setters
	static void setReplyTime(uint16_t replyDelayTimeUs);
	static void setResetPeriod(uint32_t resetPeriod);
	static void setStopRanging(bool stop_ranging_input);
	//getters
	static byte* getCurrentAddress() { return _currentAddress; };
	
	static byte* getCurrentShortAddress() { return _currentShortAddress; };
	
	static uint8_t getNetworkDevicesNumber() { return _networkDevicesNumber; };
	

	//ranging functions
	static int16_t detectMessageType(byte datas[]); // TODO check return type
	static void loop();
	static void useRangeFilter(bool enabled);
	// Used for the smoothing algorithm (Exponential Moving Average). newValue must be >= 2. Default 15.
	static void setRangeFilterValue(uint16_t newValue);
	
	//Handlers:
	static void attachNewRange(void (* handleNewRange)(void)) { _handleNewRange = handleNewRange; };
	
	static void attachBlinkDevice(void (* handleBlinkDevice)(DW1000Device*)) { _handleBlinkDevice = handleBlinkDevice; };
	
	static void attachNewDevice(void (* handleNewDevice)(DW1000Device*)) { _handleNewDevice = handleNewDevice; };
	
	static void attachInactiveDevice(void (* handleInactiveDevice)(DW1000Device*)) { _handleInactiveDevice = handleInactiveDevice; };
	
	//Callback for the change request. It aims to a function with a bool parameter (toInitiator)
	static void attachModeSwitchRequested(void (* handleModeSwitch)(byte* shortAddress, bool toInitiator)){ _handleModeSwitchRequest = handleModeSwitch;}

	// Callback for when the Mode Switch is made. Sent from the slave back to the master.
	static void attachModeSwitchAck(void (* handleModeSwitchAck)(bool isInitiator)){ _handleModeSwitchAck = handleModeSwitchAck;}
	// Callback for when data is requested (slave anchors have access to this)
	static void attachDataRequested(void (*handleDataRequest)(byte* shortAddress)){ _handleDataRequest = handleDataRequest; }

	//Callback for when the master receives a data_report message (only the master anchor has access to this)
	static void attachDataReport(void (*handleDataReport)(byte* dataReport)){ _handleDataReport = handleDataReport;}

	static void attachStopRangingRequested( void(*handleStopRanging)(byte* shortAddress)){
		_handleStopRanging = handleStopRanging;	}

	static void attachStopRangingAck(void(*handleStopRangingAck)(void)){_handleStopRangingAck = handleStopRangingAck;}

	static DW1000Device* getDistantDevice();
	static DW1000Device* searchDistantDevice(byte shortAddress[]);
	static DW1000Device* searchDeviceByShortAddHeader(uint8_t short_addr_header);
	
	//FOR DEBUGGING
	static void visualizeDatas(byte datas[]);

	//Mode Swith: 
	//Request: sent by the master to the slaves.
	void transmitModeSwitch(bool toInitiator, DW1000Device* device = nullptr);
	//Acknowledgement: sent by the master back to the master.
	void transmitModeSwitchAck(DW1000Device* device,bool isInitiator);

	void transmitStopRanging(DW1000Device* device);

	void transmitStopRangingAck(DW1000Device* device);

	// To request a data request --> Sent by the master. Receives as parameter, the target device 
	void transmitDataRequest(DW1000Device* device = nullptr);

	// To send the data report. Sent by the slaves to the master.
	void transmitDataReport(Measurement* measurements, int numMedidas, DW1000Device* device = nullptr);

	
private:
	//other devices in the network
	static DW1000Device _networkDevices[MAX_DEVICES];
	static volatile uint8_t _networkDevicesNumber;
	static int16_t      _lastDistantDevice;
	static byte         _currentAddress[8];
	static byte         _currentShortAddress[2];
	static byte         _lastSentToShortAddress[2];
	static DW1000Mac    _globalMac;
	static int32_t      timer;
	static int16_t      counterForBlink;
	static bool 		_lastFrameWasLong;
	//Handlers:
	static void (* _handleNewRange)(void);
	static void (* _handleBlinkDevice)(DW1000Device*);
	static void (* _handleNewDevice)(DW1000Device*);
	static void (* _handleInactiveDevice)(DW1000Device*);

	static void (* _handleModeSwitchRequest)(byte* shortAddress, bool toInitiator);
	static void (* _handleModeSwitchAck)(bool isInitiator);

	static void (* _handleDataRequest)(byte* shortAddress);
	static void (* _handleDataReport)(byte* dataReport);

	static void (* _handleStopRanging)(byte* shortAddress);
	static void (* _handleStopRangingAck)(void);
	
	
	//sketch type (Initiator or responder)
	static int16_t          _type; //0 for Initiator and 1 for responder
	// TODO check type, maybe enum?
	// message flow state
	static volatile byte    _expectedMsgId;
	// message sent/received state
	static volatile bool _sentAck;
	static volatile bool _receivedAck;
	// protocol error state
	static bool          _protocolFailed;
	// reset line to the chip
	static uint8_t     _RST;
	static uint8_t     _SS;
	// watchdog and reset period
	static uint32_t    _lastActivity;
	static uint32_t    _resetPeriod;
	// reply times (same on both sides for symm. ranging)
	static uint16_t     _replyDelayTimeUS;
	//timer Tick delay
	static uint16_t     _timerDelay;
	// ranging counter (per second)
	static uint16_t     _successRangingCount;
	static uint32_t    _rangingCountPeriod;
	//ranging filter
	static volatile bool _useRangeFilter;
	static uint16_t         _rangeFilterValue;
	
	static uint8_t  _myBoardType;
	//_bias correction
	static char  _bias_RSL[17]; // TODO remove or use
	//17*2=34 bytes in SRAM
	static int16_t _bias_PRF_16[17]; // TODO remove or use
	//17 bytes in SRAM
	static char  _bias_PRF_64[17]; // TODO remove or use
	
	
	static bool stop_ranging;
	static bool ranging_enabled; 
	
	//methods
	static void handleSent();
	static void handleReceived();
	static void noteActivity();
	static void resetInactive();
	
	//global functions:
	static void checkForReset();
	static void checkForInactiveDevices();
	static void copyShortAddress(byte address1[], byte address2[]);
	
	//for ranging protocole (responder)
	static void transmitInit();
	static void transmit(byte datas[]);
	static void transmit(byte datas[], DW1000Time time);
	static void transmitBlink();
	static void transmitRangingInit(DW1000Device* myDistantDevice);
	static void transmitPollAck(DW1000Device* myDistantDevice);
	static void transmitRangeReport(DW1000Device* myDistantDevice);
	static void transmitRangeFailed(DW1000Device* myDistantDevice);
	static void receiver();
	
	//for ranging protocole (Initiator)
	static void transmitPoll(DW1000Device* myDistantDevice);
	static void transmitRange(DW1000Device* myDistantDevice);
	
	//methods for range computation
	static void computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF);
	
	static void timerTick();
	
	//Utils
	static float filterValue(float value, float previousValue, uint16_t numberOfElements);
};

extern DW1000RangingClass DW1000Ranging;