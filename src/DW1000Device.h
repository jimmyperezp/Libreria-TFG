#define INACTIVITY_TIME 5000

#define MASTER_ANCHOR 1
#define SLAVE_ANCHOR 2
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

		//setters:
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

		void setRangingComplete(bool ranging_complete){_ranging_complete = ranging_complete;}

		//getters
		bool getRangingComplete(){return _ranging_complete;}
		uint8_t getBoardType(){return _boardType;}
		uint16_t getReplyTime() { return _replyDelayTimeUS; }
		int8_t getIndex() { return _index; }
		
		byte* getByteAddress();
		byte* getByteShortAddress();
		
		uint16_t getShortAddress();
		uint8_t getShortAddressHeader();

		float getRange();
		float getRXPower();
		float getFPPower();
		float getQuality();

		bool isAddressEqual(DW1000Device* device);
		bool isShortAddressEqual(DW1000Device* device);

		//functions which contains the date: (easier to put as public)
		// timestamps to remember
		DW1000Time timePollSent;
		DW1000Time timePollReceived;
		DW1000Time timePollAckSent;
		DW1000Time timePollAckReceived;
		DW1000Time timeRangeSent;
		DW1000Time timeRangeReceived;

		void noteActivity();
		bool isInactive();

		
	private:

		//device ID
		byte         _ownAddress[8];
		byte         _shortAddress[2];
		int32_t      _activity;
		uint16_t     _replyDelayTimeUS;
		int8_t       _index; // not used

		uint16_t _boardType;
		int16_t _range;
		int16_t _RXPower;
		int16_t _FPPower;
		int16_t _quality;

		bool _ranging_complete;

		void randomShortAddress();

};

#endif