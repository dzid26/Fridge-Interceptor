#include "ReturnZeroSignalHandler.h"
#include <FunctionalInterrupt.h>


ICACHE_RAM_ATTR RZ_Signal::RZ_Signal(int select_pinIN, int select_pinOUT, int baudRate, HardwareSerial *debugSerial) : _ticker()
{
  _pinIn = select_pinIN;
  _pinOut = select_pinOUT;
  _baudRate = baudRate;
  pinMode(_pinIn, INPUT);
  pinMode(_pinOut, OUTPUT);
  _debugSerial = debugSerial;
}

void ICACHE_RAM_ATTR RZ_Signal::startMirroring()
{
  _mirroringActive = true;
}

void ICACHE_RAM_ATTR RZ_Signal::stopMirroring()
{
  digitalWrite(_pinOut, LOW);
  _mirroringActive = false;
}

void RZ_Signal::disable(){
  detachInterrupt(_pinIn);
}

void RZ_Signal::begin(){
  attachInterrupt(_pinIn, [this]() { this->handleCyclicInputSignal_ISR(); }, CHANGE);
}

boolean ICACHE_RAM_ATTR RZ_Signal::isMirroringActive()
{
  return _mirroringActive;
}

void ICACHE_RAM_ATTR RZ_Signal::writeSequence(const boolean *sequence, int seqLength) //doesn't care about sync
{
  if(!_sequenceWriteOnce){
    if(!_ticker.active()) { //activate sequencer ticker if it never got synced to input signal 
    debugPrint("startedTicker \n");
    _ticker.attach_ms(1000/_baudRate, [this]() { this->sequencer_ISR(); }); //not synced ticker
    }
    debugPrint("writing\n");
    _sequenceWriteOnce=true;
    _sequence=sequence; //set the member for ISR to access
    _sequenceLength=seqLength;
  }
}

void ICACHE_RAM_ATTR RZ_Signal::writeSequenceWhenIdle(boolean *sequence, int seqLength)
{
  checkIfIdle();
  writeSequence(sequence, seqLength);
}

void ICACHE_RAM_ATTR RZ_Signal::writeSequenceWhenFalling(boolean *sequence, int seqLength)
{
  checkIfIdle();
  writeSequence(sequence,seqLength);
}

void ICACHE_RAM_ATTR RZ_Signal::debugPrint(const char * text){
#ifdef GEN_DEBUG 
  _debugSerial->print(text); 
#endif
}

boolean ICACHE_RAM_ATTR RZ_Signal::checkIfIdle()
{
  return true;
}

boolean ICACHE_RAM_ATTR RZ_Signal::readBit()
{
  return digitalRead(_pinIn); // read the input pin
};


void ICACHE_RAM_ATTR RZ_Signal::writeBit(boolean a)
{
  a ? digitalWrite(_pinOut, HIGH) : digitalWrite(_pinOut, LOW); // read the input pin
  a ? debugPrint("1"):debugPrint("0");
}

void ICACHE_RAM_ATTR RZ_Signal::mirrorBit(){
  writeBit(readBit());
}

void ICACHE_RAM_ATTR RZ_Signal::handleCyclicInputSignal_ISR()
{
  if (_mirroringActive)
    mirrorBit();
  if (!_tickerSynced && readBit()==false && !_sequanceStarted){ //initialize ssequencer on falling edge
    if (_ticker.active()){
    _ticker.detach(); //detach and sync the timer to the falling edge
    }
    _ticker.attach_ms(1000/_baudRate, [this]() { this->sequencer_ISR(); });
    debugPrint("startTickerSyncd_");
    _tickerSynced=true;
  }
}

void ICACHE_RAM_ATTR RZ_Signal::sequencer_ISR()
{ 
  if (_sequenceWriteOnce || _sequenceWriteLoop){
    if (_sequanceStarted || (!_tickerSynced || readBit()==false)){ //start sequence on falling edge if believe ticker is synced (sequence array should start with 0)
    boolean _mirroringWasActive; //before the sequence
    if (isMirroringActive()){
      _mirroringWasActive=true;
      stopMirroring(); //pause mirroring
      debugPrint("pause mirroring\n");
    }
    _sequanceStarted=true;
    if( cycleThruSequence()){ //when sequence full cycle finished 
      debugPrint("cycle done\n");
      if (!_sequenceWriteLoop){ //if not looped, or loop was stopped
      _sequanceStarted=false; //clear "Started flag"
      debugPrint("clear sequencer start flag\n");
      if (_mirroringWasActive){ //resume mirroring, if it was active prior to the sequence writing
        startMirroring(); //resume mirroring
        debugPrint("resuming mirroring\n");
        }
      }
      _sequenceWriteOnce=false;
    }
    }
  }
}

boolean ICACHE_RAM_ATTR RZ_Signal::cycleThruSequence(){
  writeBit(_sequence[_sequence_caret]);
  //debugPrint((char*) _sequence[_sequence_caret]);
  _sequence_caret++;
  if (_sequence_caret < _sequenceLength)
    return false; //indicates "not done yet"
  else //end of the sequence reached
  {
    _sequence_caret=0;
    debugPrint("\n");
    return true; //sequence done
  }
}