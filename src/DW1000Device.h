/*DW1000Device.h*/

#define INACTIVITY_TIME 1500

#define COORDINATOR 1
#define NODE 2
#define TAG 3

#ifndef _DW1000Device_H_INCLUDED
#define _DW1000Device_H_INCLUDED

#include "DW1000Time.h"
#include "DW1000Mac.h"

class DW1000Mac;

class DW1000Device;

class DW1000Device {
public:
	//Constructor and destructor
	DW1000Device();
	DW1000Device(byte address[], byte shortAddress[]);
	DW1000Device(byte address[], bool shortOne = false);
	~DW1000Device();
	
	//Setters:
	void setReplyTime(uint16_t replyDelayTimeUs);
	void setAddress(char address[]);
	void setAddress(byte* address);
	void setShortAddress(byte address[]);
	
	void setRange(float range);
	void setRXPower(float power);
	void setFPPower(float power);
	void setQuality(float quality);
	
	void setReplyDelayTime(uint16_t time) { _replyDelayTimeUS = time; }
	void setBoardType(uint8_t boardType) {_boardType = boardType;}
	void setIndex(int8_t index) { _index = index; }

	void setCycleId(uint8_t cycle_id){_cycle_id = cycle_id;} // Used in mesh token passing. Used to make sure every node receives the token in each cycle
	
	/*Getters*/

	uint8_t getBoardType(){return _boardType;}
	uint16_t getReplyTime() { return _replyDelayTimeUS; }
	
	int8_t getIndex() { return _index; }
	
	uint8_t getCycleId(){return _cycle_id;}

	//To get the short address in all forms (as byte, as uint16_t, or only the header)
	uint16_t getShortAddress();
	uint8_t  getShortAddressHeader();
	byte*    getByteShortAddress();
	byte* 	 getByteAddress();
	
	float getRange();
	float getRXPower();
	float getFPPower();
	float getQuality();
	
	bool isAddressEqual(DW1000Device* device);
	bool isShortAddressEqual(DW1000Device* device);
	
	//Timestamps used in the TWR ranging protocol
	DW1000Time timePollSent;
	DW1000Time timePollReceived;
	DW1000Time timePollAckSent;
	DW1000Time timePollAckReceived;
	DW1000Time timeRangeSent;
	DW1000Time timeRangeReceived;
	
	void noteActivity();
	bool isInactive();	// Checks last activity time. If greater than the limit, then sets device as inactive.


private:
	
	
	int32_t   _activity;
	uint16_t  _replyDelayTimeUS;
	uint8_t	  _cycle_id; // Used to ensure all of the devices receive the token in each coordinator's cycle.
	int8_t    _index; 	 // Used to "control" the spot it ocuppies in the networkDevices array
	byte      _ownAddress[8];
	byte      _shortAddress[2];

	uint16_t _boardType;
	int16_t _range;
	int16_t _RXPower;
	int16_t _FPPower;
	int16_t _quality;
	
	void randomShortAddress();
	
};


#endif
