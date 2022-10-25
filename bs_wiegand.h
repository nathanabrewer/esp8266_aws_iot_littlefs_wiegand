
#include "Arduino.h"

struct WiegandResult {
  unsigned long code;
  int type;
};

typedef void (*WiegandResultCallback) (WiegandResult result);

class BS_WIEGAND {
 
 //Pointer to instances of self (we only use instance0)
 static BS_WIEGAND * instance0_;
 static BS_WIEGAND * instance1_;

 //static interrupt calls for d0 and d1 Falling interrupts (both are for instance0)
 static void isr0d0();
 static void isr0d1();
 
 const byte whichISR_;

public:
  BS_WIEGAND(const byte whichISR, byte pinIntD0, byte pinIntD1);  
  void begin();
  void loop();
  void setResultHandler(WiegandResultCallback callback);
  void HandleData0Falling();
  void HandleData1Falling();
private:
  WiegandResultCallback weigandResultCallback;
  bool DoWiegandConversion ();
  unsigned long GetCardId (volatile unsigned long *codehigh, volatile unsigned long *codelow, char bitlength);
  volatile unsigned long   _cardTempHigh;
  volatile unsigned long   _cardTemp;
  volatile unsigned long   _lastWiegand;
  volatile int       _bitCount;  
  int        _wiegandType;
  unsigned long  _code;
  byte PIN_D0;
  byte PIN_D1;  
};
