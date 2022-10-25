#include "bs_wiegand.h"

// whichISR_ is const read only, so cannot be set inside function. Hence weird whcihISR_ (whichIRS) syntax
// Also, we are initializing a new object, but the attach interrupts need to be on a static method, so... instance0_, instance1 pointers make this messy
BS_WIEGAND::BS_WIEGAND(const byte whichISR, byte D0, byte D1) : whichISR_ (whichISR){
  //whichISR_ = whichISR; <-- this cant work
  PIN_D0 = D0;
  PIN_D1 = D1;
  //counter_ = 0
}

// for use by ISR glue routines
BS_WIEGAND * BS_WIEGAND::instance0_;
BS_WIEGAND * BS_WIEGAND::instance1_;


void BS_WIEGAND::begin()
{
  
  _lastWiegand = 0;
  _cardTempHigh = 0;
  _cardTemp = 0;
  _code = 0;
  _wiegandType = 0;
  _bitCount = 0;  
  pinMode(PIN_D0, INPUT);          // Set D0 pin as input
  pinMode(PIN_D1, INPUT);          // Set D1 pin as input
    
  switch (whichISR_){
    // we should always just have 1 instance here for now... so just case0,
    // but we could have instance1, instance2, etc... but each one would need a way to call a static method for THAT instance
    //
    
    case 0: 
      //we are attaching to static method isr0 on BS_WIEGAND. Static method isr0 will resolve correct instance
      attachInterrupt(PIN_D0, isr0d0, FALLING);
      attachInterrupt(PIN_D1, isr0d1, FALLING);
      instance0_ = this;
      break;
   
  }  

}

void BS_WIEGAND::setResultHandler(WiegandResultCallback callback){
  weigandResultCallback = callback;
}


void BS_WIEGAND::loop(){  
  bool haveResults;
  noInterrupts();
  haveResults=DoWiegandConversion();
  interrupts();
  if(haveResults){
    //pack results into result struct, and send
      WiegandResult result;
      result.code = _code;
      result.type = _wiegandType;
      weigandResultCallback(result);
  }
    
}

void BS_WIEGAND::HandleData0Falling ()
{
  _bitCount++;        // Increament bit count for Interrupt connected to D0
  if (_bitCount>31)     // If bit count more than 31, process high bits
  {
    _cardTempHigh |= ((0x80000000 & _cardTemp)>>31);  //  shift value to high bits
    _cardTempHigh <<= 1;
    _cardTemp <<=1;
  }
  else
  {
    _cardTemp <<= 1;    // D0 represent binary 0, so just left shift card data
  }
  _lastWiegand = millis();  // Keep track of last wiegand bit received
}

void BS_WIEGAND::HandleData1Falling()
{
  _bitCount ++;       // Increment bit count for Interrupt connected to D1
  if (_bitCount>31)     // If bit count more than 31, process high bits
  {
    _cardTempHigh |= ((0x80000000 & _cardTemp)>>31);  // shift value to high bits
    _cardTempHigh <<= 1;
    _cardTemp |= 1;
    _cardTemp <<=1;
  }
  else
  {
    _cardTemp |= 1;     // D1 represent binary 1, so OR card data with 1 then
    _cardTemp <<= 1;    // left shift card data
  }
  _lastWiegand = millis();  // Keep track of last wiegand bit received
}

unsigned long BS_WIEGAND::GetCardId (volatile unsigned long *codehigh, volatile unsigned long *codelow, char bitlength)
{
  if (bitlength==26)                // EM tag
    return (*codelow & 0x1FFFFFE) >>1;

  if (bitlength==24)
    return (*codelow & 0x7FFFFE) >>1;

  if (bitlength==34)                // Mifare 
  {
    *codehigh = *codehigh & 0x03;       // only need the 2 LSB of the codehigh
    *codehigh <<= 30;             // shift 2 LSB to MSB   
    *codelow >>=1;
    return *codehigh | *codelow;
  }

  if (bitlength==32) {
    return (*codelow & 0x7FFFFFFE ) >>1;
  }

  return *codelow;                // EM tag or Mifare without parity bits
}

char translateEnterEscapeKeyPress(char originalKeyPress) {
  switch(originalKeyPress) {
  case 0x0b:        // 11 or * key
    return 0x0d;  // 13 or ASCII ENTER

  case 0x0a:        // 10 or # key
    return 0x1b;  // 27 or ASCII ESCAPE

  default:
    return originalKeyPress;
  }
}

bool BS_WIEGAND::DoWiegandConversion ()
{
  unsigned long cardID;
  unsigned long sysTick = millis();
  
  if ((sysTick - _lastWiegand) > 25)                // if no more signal coming through after 25ms
  {
    if ((_bitCount==24) || (_bitCount==26) || (_bitCount==32) || (_bitCount==34) || (_bitCount==8) || (_bitCount==4))   // bitCount for keypress=4 or 8, Wiegand 26=24 or 26, Wiegand 34=32 or 34
    {
      _cardTemp >>= 1;      // shift right 1 bit to get back the real value - interrupt done 1 left shift in advance
      if (_bitCount>32)     // bit count more than 32 bits, shift high bits right to make adjustment
        _cardTempHigh >>= 1;

      if (_bitCount==8)   // keypress wiegand with integrity
      {
        // 8-bit Wiegand keyboard data, high nibble is the "NOT" of low nibble
        // eg if key 1 pressed, data=E1 in binary 11100001 , high nibble=1110 , low nibble = 0001 
        char highNibble = (_cardTemp & 0xf0) >>4;
        char lowNibble = (_cardTemp & 0x0f);
        _wiegandType=_bitCount;         
        _bitCount=0;
        _cardTemp=0;
        _cardTempHigh=0;
        
        if (lowNibble == (~highNibble & 0x0f))    // check if low nibble matches the "NOT" of high nibble.
        {
          _code = (int)translateEnterEscapeKeyPress(lowNibble);
          return true;
        }
        else {
          _lastWiegand=sysTick;
          _bitCount=0;
          _cardTemp=0;
          _cardTempHigh=0;
          return false;
        }

        // TODO: Handle validation failure case!
      }
      else if (4 == _bitCount) {
        // 4-bit Wiegand codes have no data integrity check so we just
        // read the LOW nibble.
        _code = (int)translateEnterEscapeKeyPress(_cardTemp & 0x0000000F);

        _wiegandType = _bitCount;
        _bitCount = 0;
        _cardTemp = 0;
        _cardTempHigh = 0;

        return true;
      }
      else    // wiegand 26 or wiegand 34
      {
        cardID = GetCardId (&_cardTempHigh, &_cardTemp, _bitCount);
        _wiegandType=_bitCount;
        _bitCount=0;
        _cardTemp=0;
        _cardTempHigh=0;
        _code=cardID;
        return true;
      }
    }
    else
    {
      // well time over 25 ms and bitCount !=8 , !=26, !=34 , must be noise or nothing then.
      _lastWiegand=sysTick;
      _bitCount=0;      
      _cardTemp=0;
      _cardTempHigh=0;
      return false;
    } 
  }
  else
  return false;
}




//Static functions to Proxy to correct Instance... IRAM_ATTR because interrupt is calling is (Community Arduino ESP8266 SDK 3.x)

IRAM_ATTR void BS_WIEGAND::isr0d0 (){
  instance0_->HandleData0Falling ();  
}  

IRAM_ATTR void BS_WIEGAND::isr0d1 (){
  instance0_->HandleData1Falling ();  
}  
