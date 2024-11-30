// Timer Service Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// Timer 4

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef TIMER_H_
#define TIMER_H_

typedef void (*_tim_callback_t)(void*);

#define NUM_TIMERS 25
#define INVALID_TIMER 255

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initTimer();
uint8_t startOneshotTimer(_tim_callback_t callback, uint32_t seconds, void* context);
uint8_t startPeriodicTimer(_tim_callback_t callback, uint32_t seconds, void* context);
bool stopTimer(uint8_t id);
//bool restartTimer(_tim_callback_t callback);
void tickIsr();
uint32_t random32();

#endif
