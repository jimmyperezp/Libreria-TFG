#ifndef _DW1000Ranging_H_INCLUDED
#define _DW1000Ranging_H_INCLUDED
#endif

#include "DW1000.h"
#include "DW1000Time.h"
#include "DW1000Device.h" 
#include "DW1000Mac.h"

#ifndef DEBUG
	#define DEBUG false
#endif


/*  Definition of Message types:  */

// 1: Messages used in the TWR protocol

#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
#define BLINK 4
#define RANGING_INIT 5

// 2: Added messages --> Control & data flow:

#define MODE_SWITCH 6     			// To request a switch from initiator to responder (or viceversa)
#define MODE_SWITCH_ACK 7 			// Informs the requester that the petition was received

#define REQUEST_DATA 8    			// Requests a data report. These reports include the measurements made by the requested device
#define DATA_REPORT_LOCAL 9 		// Only sends its own measurements. Used in hub & spoke (star) topology. Each device answers to the coordinator directly
#define DATA_REPORT_AGGREGATED 10   // Receives previous reports, adds its measurements, and sends them to the requester. 
#define DATA_REPORT_ACK 11  		// The requester sends this to the sender once the report is received

#define STOP_RANGING 12				// Requests a device to stop its ranging, without caring if it's an initiator or responder 
#define STOP_RANGING_ACK 13			// Informs the requester that the petition was received

#define TOKEN_HANDOFF 14			// Used in token-passing topologies. Makes the receiver act as an initiator and manage the next token pass.
#define TOKEN_HANDOFF_ACK 15		// Informs the requester that the token was received.
#define TOKEN_HANDOFF_NACK 16		// Informs the requester that the token was NOT accepted. Used in MESH topologies. Nodes may have received the token from other requester before. 

/* Board types (see TWR diagram in repository's readme to understand) */

#define INITIATOR 0   // Starts the TWR transmissions
#define RESPONDER 1	  // Answers to polls to start the TWR process



#define LEN_DATA 90 	// Máx amount of bytes that can be sent inside a message's payload (the data slots available to fill up)
#define MAX_DEVICES 10  // Máx amount of devices saved into the networkDevices array. (each DW1000Device takes up around 74 bytes in SRAM memory)


//Default timing values: 
#define DEFAULT_RESET_PERIOD 500 		// [mS]
#define DEFAULT_REPLY_DELAY_TIME 8000 	// [uS]
#define DEFAULT_TIMER_DELAY 80			// [mS]



// Struct to handle the known measurements among the system's devices. Each device locally creates an array of this struct to save its measurements.
struct Measurement {
    uint8_t short_addr_origin;  // Origin's short address header. Used to know the devices involved in the measurement
    uint8_t short_addr_dest;	// "Destiny's" short address header.
    float distance;     		// Last measured distance (in meters)
    float rxPower;      		// Last RX power measured with the destiny (in dBm)
    bool active;        		// Controls if the measurement is active (made in current cycle or previous ones) 
};


// Each device locally (in the ".ino") creates an array of this struct to handle its known Devices from there.
struct ExistingDevice{
	
	uint8_t short_addr; 		// Short address header (the known Device's name)
	byte byte_short_addr[2];	// Short address saved in byte mode. This is used to 
	
	bool active;				// Saves if the device is active or not. If not, the device is skipped in the control stage of the system
	bool is_node;				// Informs whether the device is a node instead of tag. (For example, nodes can be sent the token, but tags won't receive it)
	
	// "Pending" flags. They allow to filter received transmissions by checking if they come from the device that is expected (whoever is pending to respond)
	bool range_pending;			
	bool token_handoff_pending;
	bool mode_switch_pending;
	bool data_report_pending;

	uint8_t cycle_id;
	
};

class DW1000RangingClass {
	
	public:

		// Ranging modes. They affect the way the polls and blinks are sent. (see in timerTick function)
		enum RangingMode{
			BROADCAST,
			UNICAST,
			DISCOVERY
		};
		
		static byte data[LEN_DATA]; // Data buffer for transmission & reception. In here, I'll save the received messages and the outgoing messages before they are sent

		/* Initialization */

		//static void initCommunication(uint8_t myRST = DEFAULT_RST_PIN, uint8_t mySS = DEFAULT_SPI_SS_PIN, uint8_t myIRQ = 2);
		static void  initCommunication(uint8_t myRST , uint8_t mySS , uint8_t myIRQ );
		static void  configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]);
		static void  generalStart();
		
		static void  startAsResponder(const char address[], const byte mode[], const uint8_t device_type = 0);
		static void  startAsInitiator(const char address[], const byte mode[], const uint8_t device_type = 0);
		
		static bool  addNetworkDevices(DW1000Device* device, bool shortAddress);
		static bool  addNetworkDevices(DW1000Device* device);
		static void  removeNetworkDevices(int16_t index);
		



		/* Setters */
		static void setReplyTime(uint16_t replyDelayTimeUs) { _replyDelayTimeUS = replyDelayTimeUs;}
		static void setResetPeriod(uint32_t resetPeriod)    { _resetPeriod = resetPeriod; }
		static void setRangingMode(RangingMode mode)        { _ranging_mode = mode; }
		static void setEnableRanging(bool ranging_enabled)	{ _ranging_enabled = ranging_enabled; }
		



		/* Getters */
		static uint8_t getNetworkDevicesNumber() { return _networkDevicesNumber;}
		static uint8_t getOwnCycleId()			 { return _own_cycle_id;		}
		static byte*   getCurrentAddress()       { return _currentAddress;      }
		static byte*   getCurrentShortAddress()  { return _currentShortAddress; }
		static bool    getIsTransmitting()       { return _is_transmitting;     }
		


		//ranging functions
		static int16_t detectMessageType(byte datas[]); // TODO check return type
		static void    loop();

		/* Noise & Measurement Filtering */
			// The range filter is currently not used. If used, it smoothes the distances received using an EMA (exponential moving average).
			// The filter calculations (see function filterValue) are private, and defined later on.
		static void    useRangeFilter(bool enabled);
		static void    setRangeFilterValue(uint16_t newValue); 
		
		
		/* CALLBACKS*/
		static void attachNewRange				(void (* handleNewRange)(void)) 										{ _handleNewRange = handleNewRange; 						}
		static void attachBlinkDevice			(void (* handleBlinkDevice)(DW1000Device*)) 							{ _handleBlinkDevice = handleBlinkDevice; 					}
		static void atttachDiscoveredDevice		(void (* handleDiscoveredDevice)(DW1000Device*))						{ _handleDiscoveredDevice = handleDiscoveredDevice;			}
		static void attachNewDevice				(void (* handleNewDevice)(DW1000Device*)) 								{ _handleNewDevice = handleNewDevice; 						}
		static void attachInactiveDevice		(void (* handleInactiveDevice)(DW1000Device*)) 							{ _handleInactiveDevice = handleInactiveDevice; 			}
		static void attachModeSwitchRequested	(void (* handleModeSwitch)(bool toInitiator,bool _broadcast_ranging))	{ _handleModeSwitchRequest = handleModeSwitch;				}
		static void attachModeSwitchAck			(void (* handleModeSwitchAck)(bool isInitiator))						{ _handleModeSwitchAck = handleModeSwitchAck;				}
		static void attachDataRequested			(void (* handleDataRequest)(void))										{ _handleDataRequest = handleDataRequest; 					}
		static void attachLocalDataReport		(void (* handleLocalDataReport)(byte* dataReport))						{ _handleLocalDataReport = handleLocalDataReport;			}
		static void attachAggregatedDataReport	(void (* handleAggregatedDataReport)(byte* dataReport))					{ _handleAggregatedDataReport = handleAggregatedDataReport;	}
		static void attachDataReportAck			(void (* handleDataReportAck)(void))									{ _handleDataReportAck = handleDataReportAck;				}
		static void attachStopRangingRequested	(void (* handleStopRanging)(void))										{ _handleStopRanging = handleStopRanging;					}
		static void attachStopRangingAck		(void (* handleStopRangingAck)(void))									{ _handleStopRangingAck = handleStopRangingAck;				}
		static void attachTokenHandoff			(void (* handleTokenHandoff)(uint8_t cycle_id))							{ _handleTokenHandoff = handleTokenHandoff;					}
		static void attachTokenHandoffAck		(void (* handleTokenHandoffAck)(void))									{ _handleTokenHandoffAck = handleTokenHandoffAck;			} 		
		static void attachTokenHandoffNack		(void (* handleTokenHandoffNack)(void))									{ _handleTokenHandoffNack = handleTokenHandoffNack;			}

		/* DEVICE SEARCHING (using the "inner" newtorkDevices array) */
		static DW1000Device* getDistantDevice(); 										// Returns the "full" device that last "spoke" to me.
		static DW1000Device* searchDistantDevice(byte shortAddress[]);	 				// Searches (& returns) the device indicated by a specific shortAddress (in byte "mode")
		static DW1000Device* searchDeviceByShortAddHeader(uint8_t short_addr_header); 	// Searches & returns the device indicated by its address header
		

	
		/* UWB MESSAGE TRANSMISSIONS*/
		// See brief explanation on each of their uses inside their functions (in DW1000Ranging.cpp)

		void transmitModeSwitch				(bool toInitiator, DW1000Device* device = nullptr, bool _is_ranging_done_via_broadcast = true); 
		void transmitModeSwitchAck			(DW1000Device* device = nullptr, bool isInitiator = false); 
		void transmitStopRanging			(DW1000Device* device = nullptr);
		void transmitStopRangingAck			(DW1000Device* device = nullptr);
		void transmitDataRequest			(DW1000Device* device = nullptr);
		void transmitDataReportAck			(DW1000Device* device = nullptr); 
		void transmitTokenHandoff			(DW1000Device* device = nullptr);
		void transmitTokenHandoffAck		(DW1000Device* device = nullptr); 
		void transmitTokenHandoffNack		(DW1000Device* device = nullptr);
		
		void transmitLocalDataReport		(Measurement* measurements, int num_measures, DW1000Device* device = nullptr); 
		void transmitAggregatedDataReport	(Measurement* measurements, int num_measures, DW1000Device* device = nullptr);
 

		//	For ranging protocol: transmitPoll has to be public so that the devices can locally call it to target their known devices via unicast
		// (The rest of ranging protocol functions are private)
		static void transmitPoll(DW1000Device* myDistantDevice);


		
	private:

		//other devices in the network
		static DW1000Device _networkDevices[MAX_DEVICES];
		static volatile uint8_t _networkDevicesNumber;
		static bool 		_lastFrameWasLong;
		static int8_t 		check_inactive_devices_count;
		static int32_t      timer;
		static int16_t      counterForBlink;
		static int16_t      _lastDistantDevice;
		static byte         _currentAddress[8];
		static byte         _currentShortAddress[2];
		static byte         _lastSentToShortAddress[2];
		static DW1000Mac    _globalMac;
		
		
		/*CALLBACK HANDLERS*/
		static void (* _handleNewRange)				(void);
		static void (* _handleDataReportAck)		(void);
		static void (* _handleStopRangingAck)		(void);
		static void (* _handleTokenHandoff)			(uint8_t _cycle_id);
		static void (* _handleTokenHandoffAck)		(void);
		static void (* _handleTokenHandoffNack)		(void);
		static void (* _handleStopRanging)			(void);
		static void (* _handleDataRequest)			(void);
		static void (* _handleBlinkDevice)			(DW1000Device*);
		static void (* _handleDiscoveredDevice)		(DW1000Device*);
		static void (* _handleNewDevice)			(DW1000Device*);
		static void (* _handleInactiveDevice)		(DW1000Device*);
		static void (* _handleModeSwitchRequest)	(bool toInitiator, bool _broadcast_ranging);
		static void (* _handleModeSwitchAck)		(bool isInitiator);		
		static void (* _handleLocalDataReport)		(byte* dataReport);
		static void (* _handleAggregatedDataReport)	(byte* dataReport);
		

		
		static RangingMode _ranging_mode; 	// Ranging mode can be: broadcast (0), unicast(1) or discovery(2)
		
		/*Board's type and TWR role*/
		static uint8_t _twr_role; 			// TWR role --> Initiator (0) or responder(1). 
		static uint8_t  _device_type;		// Device type --> can be coordinator, node or tag		
		
		
		/*Cycle ID*/
		static uint8_t _own_cycle_id; 		// Used in MESH topologies. Used to make sure all of the devices in the system have received the token in the current cycle.

		// message flow state
		static volatile byte    _expectedMsgId;  // Saves next expected message. If I get a different one, then protocolFailed = true
		static bool _protocolFailed; 			
		
		// message sent/received state
		static volatile bool _sentAck;
		static volatile bool _receivedAck;

		static volatile bool _is_transmitting;
		static uint32_t 		_tx_start_time; //To avoid leaving _is_transmitting stuck

		
		// reset line to the chip
		static uint8_t     _RST;
		static uint8_t     _SS;

		// watchdog and reset period
		static uint32_t    _lastActivity;
		static uint32_t    _resetPeriod;

		/*Reply times*/
		//They must be the same in all devices for a symmetric TWR.
		static uint16_t     _replyDelayTimeUS;
		
		/*Timer tick delay*/
		static uint16_t     _timerDelay;
		
		// ranging counter (per second)
		static uint16_t     _successRangingCount;
		static uint32_t    _rangingCountPeriod;
		
		/*Ranging filtering*/
		static volatile bool _useRangeFilter;
		static uint16_t      _rangeFilterValue;
		
		/*Ranging enabler*/
		// Decides if the device responds/initiates ranging or not.
		static bool _ranging_enabled; 
		

		//methods
		static void handleSent();
		static void handleReceived();
		static void noteActivity();
		static void resetInactive();
		
		//global functions:
		static void checkForReset();
		static void checkForInactiveDevices();
		static void copyShortAddress(byte address1[], byte address2[]);
		
		//Ranging protocole (responder)
		static void transmitInit();
		static void transmit(byte datas[]);
		static void transmit(byte datas[], DW1000Time time);
		static void transmitBlink();
		static void transmitRangingInit(DW1000Device* myDistantDevice);
		static void transmitPollAck(DW1000Device* myDistantDevice);
		static void transmitRangeReport(DW1000Device* myDistantDevice);
		static void transmitRangeFailed(DW1000Device* myDistantDevice);
		static void receiver();
		
		//Ranging protocole (Initiator)
		static void transmitRange(DW1000Device* myDistantDevice);
		
		//methods for range computation
		static void computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF);
		static void timerTick();
		
		//Utils
		static float filterValue(float value, float previousValue, uint16_t numberOfElements);
};

extern DW1000RangingClass DW1000Ranging;