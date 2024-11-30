// Clock Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// 16 MHz external crystal oscillator

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include "clock.h"
#include "tm4c123gh6pm.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

uint32_t systime = 0;

// Initialize system clock to 40 MHz using PLL and 16 MHz crystal oscillator
void initSystemClockTo40Mhz(void) {
    // Configure HW to work with 16 MHz XTAL, PLL enabled, sysdivider of 5, creating system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);
}

void initSysTimer1ms() {
    //init sysTick (1ms)
    NVIC_ST_RELOAD_R = (40e3)-1; //set timer to 1ms
    NVIC_ST_CTRL_R |= NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_ENABLE; //set clk src to sysclk, enable systick
}

uint32_t millis() {
    return systime;
}

void sysTickIsr() {
    systime++;
}
