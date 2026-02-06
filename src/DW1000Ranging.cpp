#include "DW1000Ranging.h"
#include "DW1000Device.h"

DW1000RangingClass DW1000Ranging;


//other devices we are going to communicate with which are on our network:
DW1000Device DW1000RangingClass::_networkDevices[MAX_DEVICES];
byte         DW1000RangingClass::_currentAddress[8];
byte         DW1000RangingClass::_currentShortAddress[2];
byte         DW1000RangingClass::_lastSentToShortAddress[2];
volatile uint8_t DW1000RangingClass::_networkDevicesNumber = 0; // TODO short, 8bit?
int16_t      DW1000RangingClass::_lastDistantDevice    = 0; // TODO short, 8bit?
DW1000Mac    DW1000RangingClass::_globalMac;

//module type (responder or initiator)
int16_t      DW1000RangingClass::_type; 

//board type (master, anchor or tag)
uint8_t DW1000RangingClass::_myBoardType = 99;

//Ranging Mode (broadcast or unicast)
DW1000RangingClass::RangingMode DW1000RangingClass::_ranging_mode = DW1000RangingClass::BROADCAST;

//To enable/disable ranging. Starts enabled.
bool DW1000RangingClass:: ranging_enabled = true;
bool DW1000RangingClass:: stop_ranging = false;

// message flow state
volatile byte    DW1000RangingClass::_expectedMsgId;

// range filter
volatile bool DW1000RangingClass::_useRangeFilter = false;
uint16_t DW1000RangingClass::_rangeFilterValue = 15;

// message sent/received state
volatile bool DW1000RangingClass::_sentAck     = false;
volatile bool DW1000RangingClass::_receivedAck = false;

// protocol error state
bool DW1000RangingClass::_protocolFailed = false;

// Check if last frame was long: 
bool DW1000RangingClass::_lastFrameWasLong = false;

// timestamps to remember
int32_t DW1000RangingClass::timer           = 0;
int16_t DW1000RangingClass::counterForBlink = 0; // TODO 8 bit?

// data buffer
byte DW1000RangingClass::data[LEN_DATA];
// reset line to the chip
uint8_t   DW1000RangingClass::_RST;
uint8_t   DW1000RangingClass::_SS;
// watchdog and reset period
uint32_t  DW1000RangingClass::_lastActivity;
uint32_t  DW1000RangingClass::_resetPeriod;
// reply times (same on both sides for symm. ranging)
uint16_t  DW1000RangingClass::_replyDelayTimeUS;
//timer delay
uint16_t  DW1000RangingClass::_timerDelay;
// ranging counter (per second)
uint16_t  DW1000RangingClass::_successRangingCount = 0;
uint32_t  DW1000RangingClass::_rangingCountPeriod  = 0;
//Here our handlers
void (* DW1000RangingClass::_handleNewRange)(void) = 0;
void (* DW1000RangingClass::_handleBlinkDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleNewDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleInactiveDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleModeSwitchRequest)(byte*, bool toInitiator,bool _broadcast_ranging) = 0;
void (* DW1000RangingClass::_handleModeSwitchAck)(bool isInitiator) = 0;
void (* DW1000RangingClass::_handleDataRequest)(byte*) = 0;
void (* DW1000RangingClass::_handleDataReport)(byte*) = 0;
void (* DW1000RangingClass::_handleStopRanging)(byte*) = 0;
void (* DW1000RangingClass::_handleStopRangingAck)(void) = 0;


/* ###########################################################################
 * #### Init and end #######################################################
 * ######################################################################### */

void DW1000RangingClass::initCommunication(uint8_t myRST, uint8_t mySS, uint8_t myIRQ) {
	// reset line to the chip
	_RST              = myRST;
	_SS               = mySS;
	_resetPeriod      = DEFAULT_RESET_PERIOD;
	// reply times (same on both sides for symm. ranging)
	_replyDelayTimeUS = DEFAULT_REPLY_DELAY_TIME;
	//we set our timer delay
	_timerDelay       = DEFAULT_TIMER_DELAY;
	
	
	DW1000.begin(myIRQ, myRST);
	DW1000.select(mySS);
}

void DW1000RangingClass::configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]) {
	// general configuration
	DW1000.newConfiguration();
	DW1000.setDefaults();
	DW1000.setDeviceAddress(deviceAddress);
	DW1000.setNetworkId(networkId);
	DW1000.enableMode(mode);
	DW1000.commitConfiguration();
	
}

void DW1000RangingClass::generalStart() {
	// attach callback for (successfully) sent and received messages
	DW1000.attachSentHandler(handleSent);
	DW1000.attachReceivedHandler(handleReceived);
	
	
	
	if(DEBUG) {
		// DEBUG monitoring
		Serial.println("DW1000-arduino");
		// initialize the driver
		
		
		Serial.println("configuration..");
		// DEBUG chip info and registers pretty printed
		char msg[90];
		DW1000.getPrintableDeviceIdentifier(msg);
		Serial.print("Device ID: ");
		Serial.println(msg);
		DW1000.getPrintableExtendedUniqueIdentifier(msg);
		Serial.print("Unique ID: ");
		Serial.print(msg);
		char string[6];
		sprintf(string, "%02X:%02X", _currentShortAddress[0], _currentShortAddress[1]);
		Serial.print(" short: ");
		Serial.println(string);
		
		DW1000.getPrintableNetworkIdAndShortAddress(msg);
		Serial.print("Network ID & Device Address: ");
		Serial.println(msg);
		DW1000.getPrintableDeviceMode(msg);
		Serial.print("Device mode: ");
		Serial.println(msg);
	}
	
	
	// responder starts in receiving mode, awaiting a ranging poll message
	receiver();
	// for first time ranging frequency computation
	_rangingCountPeriod = millis();
}

void DW1000RangingClass::startAsResponder(const char address[], const byte mode[], const bool randomShortAddress, const uint8_t boardType) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	
	
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);
	
	//general start:
	generalStart();
	
	//defined type as responder
	_type = RESPONDER;
	_myBoardType = boardType;
	
	
}

void DW1000RangingClass::startAsInitiator(const char address[], const byte mode[], const bool randomShortAddress, const uint8_t boardType) {
	
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	
	
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);
	
	generalStart();
	//defined type as initiator

	_type = INITIATOR;
	_myBoardType = boardType;
	
}

bool DW1000RangingClass::addNetworkDevices(DW1000Device* device, bool shortAddress) {
	bool   addDevice = true;

	//we test our network devices array to check
	//we don't already have it
	
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && !shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		else if(_networkDevices[i].isShortAddressEqual(device) && shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		device->setRange(0);
		memcpy((uint8_t *)&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device)); //3_16_24 add pointer cast sjr
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

bool DW1000RangingClass::addNetworkDevices(DW1000Device* device) {
	bool addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && _networkDevices[i].isShortAddressEqual(device)) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		
		memcpy((uint8_t *)&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));  //3_16_24 pointer cast sjr
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

void DW1000RangingClass::removeNetworkDevices(int16_t index) {
	//if we have just 1 element
	if(_networkDevicesNumber == 1) {
		_networkDevicesNumber = 0;
	}
	else if(index == _networkDevicesNumber-1) //if we delete the last element
	{
		_networkDevicesNumber--;
	}
	else {
		//we translate all the element wich are after the one we want to delete.
		for(int16_t i = index; i < _networkDevicesNumber-1; i++) { // TODO 8bit?
			memcpy((uint8_t *)&_networkDevices[i], &_networkDevices[i+1], sizeof(DW1000Device));  //3_16_24 pointer cast sjr
			_networkDevices[i].setIndex(i);
		}
		_networkDevicesNumber--;
	}
}

/* ###########################################################################
 * #### Setters and Getters ##################################################
 * ######################################################################### */

void DW1000RangingClass::setReplyTime(uint16_t replyDelayTimeUs) { 
	_replyDelayTimeUS = replyDelayTimeUs; 
}

void DW1000RangingClass::setResetPeriod(uint32_t resetPeriod) { 
	_resetPeriod = resetPeriod; 
}

void DW1000RangingClass::setStopRanging(bool stop_ranging_input){

	stop_ranging = stop_ranging_input;

	if(!stop_ranging && !ranging_enabled ){ //when asked to continue
		ranging_enabled = true;
	}
	
}

DW1000Device* DW1000RangingClass::searchDistantDevice(byte shortAddress[]) {
	
	//we compare the 2 bytes address with the others
	for(uint16_t i = 0; i < _networkDevicesNumber; i++) { // TODO 8bit?
		if(memcmp(shortAddress, _networkDevices[i].getByteShortAddress(), 2) == 0) {
			//we have found our device !
			return &_networkDevices[i];
		}
	}
	
	return nullptr;
}

DW1000Device* DW1000RangingClass::searchDeviceByShortAddHeader(uint8_t short_addr_header){



	for(uint8_t i = 0; i < _networkDevicesNumber; i++) { 
		
		if(short_addr_header == _networkDevices[i].getShortAddressHeader()) {
			// Found!
			return &_networkDevices[i];
		}
	}

	return nullptr; // Not found.
}


DW1000Device* DW1000RangingClass::getDistantDevice() {
	//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
	
	return &_networkDevices[_lastDistantDevice];
	
}

/* ###########################################################################
 * #### Public methods #######################################################
 * ######################################################################### */

void DW1000RangingClass::checkForReset() {
	uint32_t curMillis = millis();
	if(!_sentAck && !_receivedAck) {
		// check if inactive
		if(curMillis-_lastActivity > _resetPeriod) {
			resetInactive();
		}
		return; // TODO cc
	}
}

void DW1000RangingClass::checkForInactiveDevices() {
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isInactive()) {
			if(_handleInactiveDevice != 0) {
				(*_handleInactiveDevice)(&_networkDevices[i]);
			}
			//we need to delete the device from the array:
			removeNetworkDevices(i);
			
		}
	}
}

int16_t DW1000RangingClass::detectMessageType(byte datas[]) {
	if(datas[0] == FC_1_BLINK) {
		return BLINK;
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2) {
		//we have a long MAC frame message (ranging init)
		_lastFrameWasLong = true;
		return datas[LONG_MAC_LEN];
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2_SHORT) {
		//we have a short mac frame message (poll, range, range report, etc..)
		_lastFrameWasLong = false;
		return datas[SHORT_MAC_LEN];
	}
	return -1; // Default return value to prevent compilation error
}

void DW1000RangingClass::loop() {
	
	checkForReset();
	uint32_t current_time = millis();
	if(current_time-timer > _timerDelay) {
		timer = current_time;
		timerTick();
	}
	
	if(_sentAck) {
		_sentAck = false;

		int messageType = detectMessageType(data);
		
		if(messageType == MODE_SWITCH || messageType == REQUEST_DATA || messageType == STOP_RANGING) {
             if(_type == RESPONDER || _type == INITIATOR){
                 receiver(); //To wait for the ack just after sending a message.
             }
        }
		if(messageType != POLL_ACK && messageType != POLL && messageType != RANGE)
			return;
		
		//A msg was sent. We launch the ranging protocole when a message was sent
		if(_type == RESPONDER) {
			if(messageType == POLL_ACK) {
				DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
				
				if (myDistantDevice) {
					DW1000.getTransmitTimestamp(myDistantDevice->timePollAckSent);
				}
			}
		}
		else if(_type == INITIATOR) {
			if(messageType == POLL) {
				DW1000Time timePollSent;
				DW1000.getTransmitTimestamp(timePollSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timePollSent = timePollSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timePollSent = timePollSent;
					}
				}
			}
			else if(messageType == RANGE) {
				DW1000Time timeRangeSent;
				DW1000.getTransmitTimestamp(timeRangeSent);
				//if the last device we send the POLL is broadcast:
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					//we save the value for all the devices !
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timeRangeSent = timeRangeSent;
					}
				}
				else {
					//we search the device associated with the last send address
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					//we save the value just for one device
					if (myDistantDevice) {
						myDistantDevice->timeRangeSent = timeRangeSent;
					}
				}
				
			}
		}
		
	}
	
	//Checks for received messages:
	if(_receivedAck) {

		_receivedAck = false;

		DW1000.getData(data, LEN_DATA); //getData returns the valuable info + its length
		
		int messageType = detectMessageType(data); //Extracts message type from the data buffer.
		
		if (messageType == MODE_SWITCH || messageType == REQUEST_DATA ||messageType == DATA_REPORT || messageType == STOP_RANGING) {

			bool is_broadcast = (data[5] == 0xFF && data[6] == 0xFF);
            bool is_for_me = (data[6] == _currentShortAddress[0] && data[5] == _currentShortAddress[1]);

			if (!is_broadcast && !is_for_me) {
				
				if(DEBUG){
					Serial.print("Slaves Filter: -> ");
					Serial.print("Requested to: [");
					Serial.print(data[5],HEX);Serial.print(":");Serial.print(data[6],HEX);
					Serial.print("] And I am: [");
					Serial.print(_currentShortAddress[0], HEX); Serial.print(":"); Serial.print(_currentShortAddress[1], HEX);
					Serial.println("]");

					if(is_for_me) Serial.println("Message is for me --> I continue the loop");
					else Serial.println("Message isn't for me --> I quit the loop.");

				}
				
				//If not broadcast (unicast) and not for me -> ignore the message
				return;
			}
		}
		
		if(messageType == MODE_SWITCH){

			byte shortAddress[2]; //Creates 2 bytes to save 'shortAddress' from the requester.
			_globalMac.decodeShortMACFrame(data, shortAddress); //To extract the shortAddress from the frame data[]

			DW1000Device* requester = searchDistantDevice(shortAddress);
            if (requester) {
                requester->noteActivity();
                _lastDistantDevice = requester->getIndex();
            }

			int headerLen = _lastFrameWasLong ? LONG_MAC_LEN : SHORT_MAC_LEN;
			bool toInitiator =  (data[headerLen + 1] == 1);
			bool _range_via_broadcast = (data[headerLen + 2] == 1 );
			if (_handleModeSwitchRequest) {
				
				(*_handleModeSwitchRequest)(shortAddress,toInitiator,_range_via_broadcast);
			}

			return;

		}
        else if(messageType == MODE_SWITCH_ACK){

            // Identify the ACK sender so getDistantDevice() points to it
            byte shortAddress[2];
            _globalMac.decodeShortMACFrame(data, shortAddress);
            DW1000Device* ackDevice = searchDistantDevice(shortAddress);
            if (ackDevice) {
                _lastDistantDevice = ackDevice->getIndex();
				ackDevice ->noteActivity();
            }

            bool isInitiator = data[SHORT_MAC_LEN +1];
            if(_handleModeSwitchAck){
                (*_handleModeSwitchAck)(isInitiator);
            }
			return;

        }
		else if(messageType == STOP_RANGING){

			byte shortAddress[2]; //Creates 2 bytes to save 'shortAddress' from the requester.
			_globalMac.decodeShortMACFrame(data, shortAddress);

			DW1000Device* ackDevice = searchDistantDevice(shortAddress);
            if (ackDevice) {
                _lastDistantDevice = ackDevice->getIndex();
				ackDevice ->noteActivity();
            }

			if(_handleStopRanging){
                (*_handleStopRanging)(shortAddress);
            }
			return;
		}
		else if(messageType == STOP_RANGING_ACK){

			byte shortAddress[2];
            _globalMac.decodeShortMACFrame(data, shortAddress);
            DW1000Device* ackDevice = searchDistantDevice(shortAddress);
            if (ackDevice) {
                _lastDistantDevice = ackDevice->getIndex();
				ackDevice ->noteActivity();
            }

            bool isInitiator = data[SHORT_MAC_LEN +1];
            if(_handleStopRangingAck){
                (*_handleStopRangingAck)();
            }
		}
		else if(messageType == REQUEST_DATA){

			byte shortAddress[2]; //Creates 2 bytes to save 'shortAddress'
			_globalMac.decodeShortMACFrame(data, shortAddress); //To extract the shortAddress from the frame data[]
			DW1000Device* req = searchDistantDevice(shortAddress);
    		if (req){ req->noteActivity(); _lastDistantDevice = req->getIndex();}

			if(_handleDataRequest){
				(* _handleDataRequest)(shortAddress);
			}
			return;

		}
		else if(messageType == DATA_REPORT){
			
			byte shortAddress[2]; //Creates 2 bytes to save 'shortAddress'
			_globalMac.decodeShortMACFrame(data, shortAddress); //To extract the shortAddress from the frame data[]
			DW1000Device* req = searchDistantDevice(shortAddress);
    		if (req){ req->noteActivity(); _lastDistantDevice = req->getIndex();}
			// The master anchor requests the slaves for a data report.
			// Slaves will have to send their measurements struct
			
			if(_handleDataReport){
				(* _handleDataReport)(data);
			}
			return;
		}

		if(ranging_enabled){

			if(messageType == BLINK && _type == RESPONDER) { //If I'm a responder and I receive a BLINK (discovery) message:
				
				byte address[8];
				byte shortAddress[2];
				_globalMac.decodeBlinkFrame(data, address, shortAddress);
				//we create a new device with the initiator
				DW1000Device myInitiator(address, shortAddress);
				bool isNewDevice = addNetworkDevices(&myInitiator);
			
				if(isNewDevice && _handleBlinkDevice != 0) {
					(*_handleBlinkDevice)(&myInitiator);
				}
				
				transmitRangingInit(&myInitiator); //Respond to the BLINK with a RANGING INIT message.
				noteActivity();
			
				_expectedMsgId = POLL;	//Next expected message is POLL from the initiator.
				
			}

			else if(messageType == RANGING_INIT && _type == INITIATOR) { //If I'm an initiator and receive a RANGING INIT (response to blink) message:
				
				byte address[2];
				_globalMac.decodeLongMACFrame(data, address);
				
				DW1000Device myResponder(address, true); //Parameter "true" indicates it's a short address.
				uint8_t responderboardType = data[LONG_MAC_LEN+1];
            	
				if(addNetworkDevices(&myResponder, true)) {
            	    
            	    _networkDevices[_networkDevicesNumber-1].setBoardType(responderboardType); //Sets the responder's board type. 
            	    
            	    if(_handleNewDevice != 0) {
            	        (*_handleNewDevice)(&_networkDevices[_networkDevicesNumber-1]);
            	    }
            	}
				noteActivity();
			}
			
			else{
				
				//When reaching this point, both the responder and initiator have been logged in the networkDevices array

				byte address[2];
				_globalMac.decodeShortMACFrame(data, address);
				DW1000Device* myDistantDevice = searchDistantDevice(address); //Searches for the device that sent the message

				if((_networkDevicesNumber == 0) || (myDistantDevice == nullptr)) { //Sender not found
					
					if (DEBUG) {
						Serial.println("404 - Sender not found");
					}
					return;
				}


				//(Code reaches here only if the sender was found)
				if(_type == RESPONDER) {
					if (messageType == POLL_ACK || messageType == RANGE_REPORT || messageType == RANGE_FAILED) {
						//Filters messages that are not for responders.
						return; 
            		}
					if(messageType != _expectedMsgId) {
						
						_protocolFailed = true;
						if(DEBUG){
							Serial.println("PROTOCOL FAILED --> Received a non expected message");
						}
					}
					if(messageType == POLL) {
						
						
						int16_t numberDevices = 0;
						memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);

						for(uint16_t i = 0; i < numberDevices; i++) {

							//Poll message can be broadcast or unicast. Responders need to check if the poll is for them. 
							// If it is for them, they need to get the replyTime, update the timestamps, and respond with POLL_ACK.

							byte shortAddress[2];
							memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*4, 2); //Extracts each short address from the POLL message.
						
							if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) { //If the short address matches mine:
								
								uint16_t replyTime;
								memcpy(&replyTime, data+SHORT_MAC_LEN+2+i*4+2, 2);
								_replyDelayTimeUS = replyTime; //Extracts the replyTime and sets it.

								_protocolFailed = false;
								DW1000.getReceiveTimestamp	(myDistantDevice->timePollReceived);
								
								myDistantDevice->noteActivity();
								
								uint8_t initiatorType = data[SHORT_MAC_LEN + 2 + 4*numberDevices];
    							myDistantDevice->setBoardType(initiatorType); //Sets the initiator's board type.

								_expectedMsgId = RANGE; //Next message expected is RANGE from the initiator.
								transmitPollAck(myDistantDevice); //But first, respond with POLL_ACK.
								noteActivity();
								return;
							}
						}
					}
					else if(messageType == RANGE) { //Last TWR message received from the initiator
						
						//Same as the POLL message reception. Need to check if the RANGE message is for me (responder). If so, extract the needed values.

						uint8_t numberDevices = 0;
						memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);

						for(uint8_t i = 0; i < numberDevices; i++) {
							
							byte shortAddress[2];
							memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*17, 2);
							
							if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) { //If the short address matches mine:
								
								
								DW1000.getReceiveTimestamp(myDistantDevice->timeRangeReceived); //Saves received time in timeRangeReceived
								noteActivity();
								_expectedMsgId = POLL; //TWR is done. Next expected message is a new POLL from the initiator.

								if(!_protocolFailed) {

									//Sets all the timestamps to calculate the distance between devices
									myDistantDevice->timePollSent.setTimestamp(data	+SHORT_MAC_LEN+4+17*i);
									myDistantDevice->timePollAckReceived.setTimestamp(data+SHORT_MAC_LEN+9+17*i);
									myDistantDevice->timeRangeSent.setTimestamp(data+SHORT_MAC_LEN+14+17*i);
									
									DW1000Time myTOF;
									computeRangeAsymmetric(myDistantDevice, &myTOF); // Calculations done here. 
									float distance = myTOF.getAsMeters();
									if (_useRangeFilter) {
										//Skip first range
										if (myDistantDevice->getRange() != 0.0f) {
											distance = filterValue(distance, myDistantDevice->getRange(),_rangeFilterValue);
										}
									}
									
									//After calculations, saves all results in the initiator's device instance:
									myDistantDevice->setRXPower(DW1000.getReceivePower());
									myDistantDevice->setRange(distance);
									myDistantDevice->setFPPower(DW1000.getFirstPathPower());
									myDistantDevice->setQuality(DW1000.getReceiveQuality());
									
									
									transmitRangeReport(myDistantDevice); //And "sends" the results back to the initiator.

									
									_lastDistantDevice = myDistantDevice->getIndex();
									if(_handleNewRange != 0) {
										(*_handleNewRange)(); //Range is finished. Calls the newRange Handler.
									}
								}
								else {
									transmitRangeFailed(myDistantDevice);
								}
								return;
							}
						}
					}
				}
				else if(_type == INITIATOR) {
					
					if(messageType != _expectedMsgId) {
						// unexpected message, start over again
						//not needed ?
						return;
						_expectedMsgId = POLL_ACK;
						return;
					}
					if(messageType == POLL_ACK) {

						DW1000.getReceiveTimestamp(myDistantDevice->timePollAckReceived); //Saves the timestamp of the received POLL_ACK
						
						myDistantDevice->noteActivity(); //notes the responder's activity (last seen moment).

						//If the poll was sent via unicast:

						if(_ranging_mode ==  UNICAST){

							//Poll was only sent once. Only 1 poll_ack expected.

							transmitRange(myDistantDevice); //Directly send range to the responder.
							_expectedMsgId = RANGE_REPORT; //Next expected message is RANGE_REPORT from the responder.
							
							return;
							
						}
						
						//If the poll was sent bia broadcast: Initiator needs to "wait" for all the poll_acks from all responders. (waits until poll_ack from the device placed last in the networkDevices array)

						else if(myDistantDevice->getIndex() == _networkDevicesNumber-1) {
							_expectedMsgId = RANGE_REPORT;
							
							transmitRange(nullptr); //If the poll was broadcast, the range message will be broadcast too.
						}
					}
					else if(messageType == RANGE_REPORT) {
						float curRange;
						memcpy(&curRange, data+1+SHORT_MAC_LEN, 4);
						float curRXPower;
						memcpy(&curRXPower, data+5+SHORT_MAC_LEN, 4);
						if (_useRangeFilter) {
							//Skip first range
							if (myDistantDevice->getRange() != 0.0f) {
								curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
								myDistantDevice->noteActivity();
							}
						}
						//we have a new range to save !
						myDistantDevice->setRange(curRange);
						myDistantDevice->setRXPower(curRXPower);
						
						//Ranging protocol finished and calculations received by the initiator. Now, it can launch the newRange callback:
						_lastDistantDevice = myDistantDevice->getIndex();
						if(_handleNewRange != 0) {
							(*_handleNewRange)();
						}
						if(stop_ranging){
							ranging_enabled = false;
						}

					}
					else if(messageType == RANGE_FAILED) {

						return;
						_expectedMsgId = POLL_ACK;
					}
				}
			}
		}
	}
}

void DW1000RangingClass::useRangeFilter(bool enabled) {
	_useRangeFilter = enabled;
}

void DW1000RangingClass::setRangeFilterValue(uint16_t newValue) {
	if (newValue < 2) {
		_rangeFilterValue = 2;
	}else{
		_rangeFilterValue = newValue;
	}
}

/* ###########################################################################
 * #### Private methods and Handlers for transmit & Receive reply ############
 * ######################################################################### */

void DW1000RangingClass::handleSent() {
	// status change on sent success
	_sentAck = true;
}

void DW1000RangingClass::handleReceived() {
	// status change on received success
	_receivedAck = true;
}

void DW1000RangingClass::noteActivity() {
	// update activity timestamp, so that we do not reach "resetPeriod"
	_lastActivity = millis();
}

void DW1000RangingClass::resetInactive() {
	//if inactive
	if(_type == RESPONDER) {
		_expectedMsgId = POLL;
		receiver();
	}
	noteActivity();
}

void DW1000RangingClass::timerTick() {

	if(ranging_enabled && !stop_ranging){

		if(_ranging_mode == BROADCAST){ //Only ticks "automatically" in broadcast mode.

			if(_networkDevicesNumber > 0 && counterForBlink != 0) {
				if(_type == INITIATOR) {
					_expectedMsgId = POLL_ACK;
					transmitPoll(nullptr);  //broadcast poll
				}
			}
			else if(counterForBlink == 0) {
				if(_type == INITIATOR) {
					transmitBlink();	
				}	
    	   	 	checkForInactiveDevices(); //check for inactive devices if we are a INITIATOR or RESPONDER
			}

			counterForBlink++;
			if(counterForBlink > 6) {
				counterForBlink = 0;
			}
		}
	}
}

void DW1000RangingClass::copyShortAddress(byte address1[], byte address2[]) {
	*address1     = *address2;
	*(address1+1) = *(address2+1);
}

/*  ###########################################################################
 * #### Methods for ranging protocole   ######################################
 * ######################################################################### */

void DW1000RangingClass::transmitInit() {
	DW1000.newTransmit();
	DW1000.setDefaults();
}

void DW1000RangingClass::transmit(byte datas[]) {
	DW1000.setData(data, LEN_DATA);
	//DW1000.setData(datas, LEN_DATA);
	DW1000.startTransmit();
}

void DW1000RangingClass::transmit(byte datas[], DW1000Time time) {
	DW1000.setDelay(time);
	DW1000.setData(data, LEN_DATA);
	//DW1000.setData(datas, LEN_DATA);
	DW1000.startTransmit();
}

void DW1000RangingClass::transmitBlink() {
	transmitInit();
	_globalMac.generateBlinkFrame(data, _currentAddress, _currentShortAddress);
	transmit(data);
	
}

void DW1000RangingClass::transmitRangingInit(DW1000Device* myDistantDevice) {
	transmitInit();
	//we generate the mac frame for a ranging init message
	_globalMac.generateLongMACFrame(data, _currentShortAddress, myDistantDevice->getByteAddress());
	//we define the function code
	data[LONG_MAC_LEN] = RANGING_INIT;
	data[LONG_MAC_LEN + 1] = _myBoardType;
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	delay(random(5, 25)); //This delay prevents colissions in responding to the blinks from the master. This way, library auto re-enables after master's reset.
	transmit(data);
}

void DW1000RangingClass::transmitPoll(DW1000Device* myDistantDevice) {
	
	transmitInit();
	
	if(myDistantDevice == nullptr) { //Polling via broadcast.
		

		//we need to set our timerDelay:
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);
		
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = POLL;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;
		
		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {

			/*In this "for", we set up a different reply delay time for each targeted device. 
			We do so by multiplying the default reply_delay_time by a different numer each time, 
			giving it enough time to send the messages in each slot.*/

			
			_networkDevices[i].setReplyTime((2*i+1)*DEFAULT_REPLY_DELAY_TIME);
			
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+4*i, _networkDevices[i].getByteShortAddress(), 2);
			//Clears 4 bytes per device. The first 2 are for the shortAddress
			
			//we add the replyTime
			uint16_t replyTime = _networkDevices[i].getReplyTime();
			memcpy(data+SHORT_MAC_LEN+2+2+4*i, &replyTime, 2);
			//These go in the pending freed up 2 bytes from before
			
		}
		
		data[SHORT_MAC_LEN+2+4*_networkDevicesNumber] = _myBoardType;
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
	}
	
	else { //Polling via unicast.
		
	
		_timerDelay = DEFAULT_TIMER_DELAY; //Only 1 device --> timer delay is OK as default.
		
		//1 Generate MAC frame
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		
		//2 Add message type and number of devices (1 here)
		data[SHORT_MAC_LEN] = POLL;
		data[SHORT_MAC_LEN+1] = 1; //number of devices = 1

		//3 Add the receiver's short address
		memcpy(data+SHORT_MAC_LEN+2, myDistantDevice->getByteShortAddress(), 2);
		
		//4 --> Add the reply time for the targeted device
		uint16_t replyTime = DEFAULT_REPLY_DELAY_TIME;
		myDistantDevice->setReplyTime(replyTime);
		memcpy(data+SHORT_MAC_LEN+4, &replyTime, sizeof(uint16_t));

		//5 --> Add the boardType of the initiator
		data[SHORT_MAC_LEN+6] = _myBoardType;

		//6 --> Save last sent to address for future reference
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	}
	
	transmit(data);
}

void DW1000RangingClass::transmitPollAck(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = POLL_ACK;
	// delay the same amount as ranging initiator
	DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data, deltaTime);
}

void DW1000RangingClass::transmitRange(DW1000Device* myDistantDevice) {

	transmitInit();

	if(myDistantDevice == nullptr) { //Range via broadcast.

		//1st --> Set the timer delay:
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);
		
		//2 --> Generate MAC frame (current address + broadcast (FF:FF))
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		
		//3 --> Message Type & number of devices
		data[SHORT_MAC_LEN]= RANGE;
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;
		
		//4 --> delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime = DW1000Time(DEFAULT_REPLY_DELAY_TIME, DW1000Time::MICROSECONDS);
		DW1000Time timeRangeSent = DW1000.setDelay(deltaTime);
		
		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
			
			//5 --> ShortAddress of each device
			memcpy(data+SHORT_MAC_LEN+2+17*i, _networkDevices[i].getByteShortAddress(), 2);
			
			//6 --> Saves all the timestamps into the message for each device:
			_networkDevices[i].timeRangeSent = timeRangeSent;
			_networkDevices[i].timePollSent.getTimestamp(data+SHORT_MAC_LEN+4+17*i);
			_networkDevices[i].timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+9+17*i);
			_networkDevices[i].timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+14+17*i);
			
		}
		
		//7 --> Save last sent to address for future reference
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
	}
	else { //Unicast Range 

		//Same steps as broadcast, but only for one device:

		//1st --> Set the timer delay:
		_timerDelay = DEFAULT_TIMER_DELAY;

		//2 --> Generate MAC frame (current address + targeted device)

		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());

		//3 --> Message Type & number of devices
		data[SHORT_MAC_LEN] = RANGE;
		data[SHORT_MAC_LEN+1] = 1; //number of devices = 1


		//4 --> delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
		myDistantDevice->timeRangeSent = DW1000.setDelay(deltaTime);

		//5 --> ShortAddress of the device
		memcpy(data+SHORT_MAC_LEN+2, myDistantDevice->getByteShortAddress(), 2);

		//6 --> Saves all the timestamps into the message for the device:
		myDistantDevice->timePollSent.getTimestamp(data+SHORT_MAC_LEN+4);
		myDistantDevice->timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+9);
		myDistantDevice->timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+14);

		//7 --> Save last sent to address for future reference
		
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	}
	
	
	transmit(data);
}

void DW1000RangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_REPORT;
	// write final ranging result
	float curRange   = myDistantDevice->getRange();
	float curRXPower = myDistantDevice->getRXPower();
	//We add the Range and then the RXPower
	memcpy(data+1+SHORT_MAC_LEN, &curRange, 4);
	memcpy(data+5+SHORT_MAC_LEN, &curRXPower, 4);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data, DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));
}

void DW1000RangingClass::transmitRangeFailed(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_FAILED;
	
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data);
}

void DW1000RangingClass::receiver() {
	DW1000.newReceive();
	DW1000.setDefaults();
	// so we don't need to restart the receiver manually
	DW1000.receivePermanently(true);
	DW1000.startReceive();
}

/* ###################################################
* -------------------------------------------------
* Methods: Mode Switch, Data Request & Data Report
* -------------------------------------------------
###################################################### */

void DW1000RangingClass::transmitStopRanging(DW1000Device* device){

	transmitInit();
	byte dest[2];
	bool sent_by_broadcast = false;

	if(device==nullptr){
		dest[0] = 0xFF;
		dest[1] = 0xFF;

		sent_by_broadcast = true;
	}

	else{

		memcpy(dest,device->getByteShortAddress(),2); 
		sent_by_broadcast = false;

	}
	_globalMac.generateShortMACFrame(data, _currentShortAddress, dest);
	
	data[SHORT_MAC_LEN ] = STOP_RANGING;
	transmit(data);
	
}

void DW1000RangingClass::transmitStopRangingAck(DW1000Device* device){

	transmitInit();
	byte dest[2];

	if(device == nullptr){
		dest[0] = 0xFF;
		dest[1] = 0xFF;
	}
	else{

		memcpy(dest,device->getByteShortAddress(),2);
	}

	_globalMac.generateShortMACFrame(data, _currentShortAddress, dest);
	data[SHORT_MAC_LEN] = STOP_RANGING_ACK;
	transmit(data);
}


void DW1000RangingClass::transmitModeSwitch(bool toInitiator, DW1000Device* device, bool _is_ranging_done_via_broadcast){

	//1: Prepare for new transmission:
	transmitInit(); //Resets ack flag and sets default parameters (power, bit rate, preamble)
	
	bool sent_by_broadcast = false;

	byte dest[2]; //Here, I'll code the message's destination. 
	
	//2: Select destination: unicast or broadcast:
	if (device == nullptr){
		//If nullptr --> Broadcasting. 
		// The message will be sent to all devices that are listening. 
		dest[0] = 0xFF;
		dest[1] = 0xFF;
		//According to the IEEE standard used, shortAddress 0xFF 0xFF is reserved as a broadcast, so that all nodes receive the message.

		sent_by_broadcast = true;
	}
	else{
		//If not -> Unicast to the device's address
		//memcpy function parameters: destiny, origin, number of bytes

		memcpy(dest,device->getByteShortAddress(),2); // This function copies n bytes from the origin to the destiny.
		sent_by_broadcast = false;
	}

	//3: Generate shortMacFrame:
	_globalMac.generateShortMACFrame(data, _currentShortAddress, dest);

	//4: Insert the payload (message to send)
	/* Byte #0 = MODE_SWITCH code
	   Byte #1 -> 0 = switch to responder. 1 = to initiator.
	*/
	uint16_t index = SHORT_MAC_LEN;
	data[index++] = MODE_SWITCH;
	data[index++] = toInitiator ? 1:0;
	data[index++] = _is_ranging_done_via_broadcast ? 1:0; // The receiver must do the ranging the same way as the sender.
	data[index++] = sent_by_broadcast ? 1:0;
	

	if(index>LEN_DATA){
		//TODO - Clip the exceeding length, instead of not sending it
		if(DEBUG) Serial.println("Payload del mode_switch demasiado largo. Ha habido truncamiento");
		
	}

	transmit(data); //the data is sent via UWB
}

void DW1000RangingClass::transmitModeSwitchAck(DW1000Device* device,bool isInitiator){

	transmitInit();
	byte dest[2];

	if(device == nullptr){
		dest[0] = 0xFF;
		dest[1] = 0xFF;
	}
	else{

		memcpy(dest,device->getByteShortAddress(),2);
	}

	_globalMac.generateShortMACFrame(data, _currentShortAddress, dest);

	data[SHORT_MAC_LEN] = MODE_SWITCH_ACK;
	data[SHORT_MAC_LEN+1] = (isInitiator ? 1:0);

	transmit(data);
}


void DW1000RangingClass::transmitDataRequest(DW1000Device* device){

	//This method works just as the "transmitModeSwitch". See explanations and commentaries there.
	transmitInit(); 

	byte dest[2]; 
	
	if (device == nullptr){
		
		dest[0] = 0xFF;
		dest[1] = 0xFF;
		
	}
	else{
		memcpy(dest,device->getByteShortAddress(),2);
	}

	_globalMac.generateShortMACFrame(data, _currentShortAddress, dest);
	data[SHORT_MAC_LEN] = REQUEST_DATA;
	
	transmit(data); //the data is sent via UWB
}


void DW1000RangingClass::transmitDataReport(Measurement* measurements, int numMeasures, DW1000Device* device) {

	uint8_t active_measures = 0;
    byte dest[2];

    // Destiny selection: broadcast or unicast
    if (device == nullptr) {

        dest[0] = 0xFF;
        dest[1] = 0xFF;
    } 
	else {
        memcpy(dest, device->getByteShortAddress(), 2);
    }

    transmitInit(); //Start a new transmit to clean up the data buffer

	//First, generate MACFrame in short Mode.
    _globalMac.generateShortMACFrame(data, _currentShortAddress, dest);
	
	//The MAC address is saved from byte 0 to the length of short_mac_len -1. 

    // Then, first byte is reserved to the type of message.
	// It is stored in the index with value short_mac_len
    data[SHORT_MAC_LEN] = DATA_REPORT;

    // Variable "index" is used to fill up the data buffer.
    uint8_t index = SHORT_MAC_LEN + 1;

	
    // 1 byte for number of measurements that are going to be sent:
	// I only send the active ones.

	for (int i = 0; i<numMeasures;i++){
		//From all the measures known to the device, only sends the active ones
		if(measurements[i].active == true){
			active_measures++;
		}
	}
    data[index++] = active_measures;

    // Before sending, I check if there's enough space for the full message:
    size_t totalPayloadSize = 1 + active_measures * 5;  // 3 "constant" bytes + 10 for each measure sent.
    size_t totalMessageSize = SHORT_MAC_LEN + 1 + totalPayloadSize; //+1 because of the message type.

    if (totalMessageSize > LEN_DATA) {
        if (DEBUG) {
            Serial.println("Error: DATA_REPORT exceeds the size of the data[] buffer");
        }
        return;  // If there isn't enough space, I return without sending it.
    }

	for (uint8_t i = 0; i < numMeasures; i++) {
    	if(measurements[i].active == true){

			//1 byte for the destiny's short Address
    		data[index++] = (uint8_t)measurements[i].short_addr_dest;
			
    		// Distante measured (sent as cm to reduce message length)
    		uint16_t distance_cm = (uint16_t)(measurements[i].distance * 100.0f);
    		memcpy(data + index, &distance_cm, 2); 
    		index += 2;
			
    		// 2 bytes for the rx power. Sent as 2 bytes.
    		int16_t rxPower_tx = (int16_t)(measurements[i].rxPower * 100.0f); // Using a signed integer (int instead o uint), the negative sign is saved correctly.
    		memcpy(data + index, &rxPower_tx, 2); 
    		index += 2;
		}
		
	}
	

    transmit(data); //Finally, sends the message
}


/* ###########################################################################
 * #### Methods for range computation and corrections  #######################
 * ######################################################################### */


void DW1000RangingClass::computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF) {
	// asymmetric two-way ranging (more computation intense, less error prone)
	DW1000Time round1 = (myDistantDevice->timePollAckReceived-myDistantDevice->timePollSent).wrap();
	DW1000Time reply1 = (myDistantDevice->timePollAckSent-myDistantDevice->timePollReceived).wrap();
	DW1000Time round2 = (myDistantDevice->timeRangeReceived-myDistantDevice->timePollAckSent).wrap();
	DW1000Time reply2 = (myDistantDevice->timeRangeSent-myDistantDevice->timePollAckReceived).wrap();
	
	myTOF->setTimestamp((round1*round2-reply1*reply2)/(round1+round2+reply1+reply2));
	
}


/* FOR DEBUGGING*/
void DW1000RangingClass::visualizeDatas(byte datas[]) {
	char string[60];
	sprintf(string, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
					datas[0], datas[1], datas[2], datas[3], datas[4], datas[5], datas[6], datas[7], datas[8], datas[9], datas[10], datas[11], datas[12], datas[13], datas[14], datas[15]);
	Serial.println(string);
}



/* ###########################################################################
 * #### Utils  ###############################################################
 * ######################################################################### */

float DW1000RangingClass::filterValue(float value, float previousValue, uint16_t numberOfElements) {
	
	float k = 2.0f / ((float)numberOfElements + 1.0f);
	return (value * k) + previousValue * (1.0f - k);
}
