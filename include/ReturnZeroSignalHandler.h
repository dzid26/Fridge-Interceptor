#ifndef RZ_SIGNAL_H
#define RZ_SIGNAL_H

#include <Arduino.h>
#include "Ticker.h"


class RZ_Signal
{
private:
  int _pinIn;
  int _pinOut;
  bool _buf;
  int _baudRate;
  boolean _idleSequence[2] = {0, 0};
  boolean _mirroringActive;
  Ticker _ticker;
  boolean _tickerSynced=false;
  HardwareSerial * _debugSerial;

  void ICACHE_RAM_ATTR debugPrint(const char * text);
  boolean ICACHE_RAM_ATTR checkIfIdle();
  boolean ICACHE_RAM_ATTR readBit();
  void ICACHE_RAM_ATTR writeBit(boolean a);
  void ICACHE_RAM_ATTR mirrorBit();
  void ICACHE_RAM_ATTR handleCyclicInputSignal_ISR();
  boolean _sequenceWriteOnce=false;
  boolean _sequenceWriteLoop=false;
  boolean _sequanceStarted=false;
  void ICACHE_RAM_ATTR sequencer_ISR();
  const boolean * _sequence;
  int _sequenceLength;
  int _sequence_caret=0;
  boolean ICACHE_RAM_ATTR cycleThruSequence();

public:
  RZ_Signal(int select_pinIN, int select_pinOUT, int baudRate, HardwareSerial *debugSerial);
  void ICACHE_RAM_ATTR startMirroring();
  void ICACHE_RAM_ATTR stopMirroring();
  void disable();
  void begin();
  boolean ICACHE_RAM_ATTR isMirroringActive();
  void ICACHE_RAM_ATTR writeSequence(const boolean *sequence, int seqLength);
  void ICACHE_RAM_ATTR writeSequenceWhenIdle(boolean *sequence, int seqLength);
  void ICACHE_RAM_ATTR writeSequenceWhenFalling(boolean *sequence, int seqLength);


};//end of class
#endif