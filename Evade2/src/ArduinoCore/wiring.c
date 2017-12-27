/*
  wiring.c - Partial implementation of the Wiring API for the ATmega8.
  Part of Arduino - http://www.arduino.cc/

  Copyright (c) 2005-2006 David A. Mellis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/

#include "wiring_private.h"

// the prescaler is set so that timer0 ticks every 64 clock cycles, and the
// the overflow handler is called every 256 ticks.
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64 * 256))

// the whole number of milliseconds per timer0 overflow
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW / 1000)

// the fractional number of milliseconds per timer0 overflow. we shift right
// by three to fit these numbers into a byte. (for the clock speeds we care
// about - 8 and 16 MHz - this doesn't lose precision.)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW % 1000) >> 3)
#define FRACT_MAX (1000 >> 3)

volatile unsigned long timer0_overflow_count = 0;
volatile unsigned long timer0_millis = 0;
static unsigned char timer0_fract = 0;

#if defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
ISR(TIM0_OVF_vect)  
#else
ISR(TIMER0_OVF_vect) //assembly optimized by 36 bytes
#endif
{
    // copy these to local variables so they can be stored in registers
    // (volatile variables must be read from memory on every access)
/*
    unsigned long m = timer0_millis;
    unsigned char f = timer0_fract;

    m += MILLIS_INC;
    f += FRACT_INC;
    if (f >= FRACT_MAX) {
        f -= FRACT_MAX;
        m += 1;
    }

    timer0_fract = f;
    timer0_millis = m;
    timer0_overflow_count++;
*/  
    // assembly optimalisation of above code saving 36 bytes
    asm volatile(
      "    ld   r24, z                  \n\t" // m = timer0_millis; 
      "    ldd  r25, z+1                \n\t"  
      "    subi %[fract], %[fract_inc]  \n\t" // f += FRACT_INC;
      "    cpi  %[fract], %[fract_max]  \n\t" // if (f >= FRACT_MAX)
      "    brcc .l1                     \n\t" 
      
      "    adiw r24, %[millis_inc]      \n\t" // m += MILLIS_INC
      "    rjmp .l2                     \n\t" 
      ".l1:                             \n\t"
      "    subi %[fract], %[fract_max]  \n\t" // f -= FRACT_MAX;
      "    adiw r24, %[millis_ip1]      \n\t" // m += MILLIS_INC + 1
      ".l2:                             \n\t"
      // timer0_millis = m; 
      "    st   z, r24                  \n\t" 
      "    std  z+1, r25                \n\t" // r25 used by arduboy code below too
      "    ldd  r0, z+2                 \n\t"  
      "    adc  r0, r1                  \n\t"  
      "    std  z+2, r0                 \n\t"  
      "    ldd  r0, z+3                 \n\t"  
      "    adc  r0, r1                  \n\t"  
      "    std  z+3, r0                 \n\t"
      : [fract]      "+a" (timer0_fract)  //f = timer0_fract / timer0_fract = f;
      :              "z"  (&timer0_millis),
        [millis_inc] "M"  (MILLIS_INC),
        [millis_ip1] "M"  (MILLIS_INC + 1),
        [fract_inc]  "M"  (256 - FRACT_INC), // negated for subi instruction
        [fract_max]  "M"  (FRACT_MAX)
      : "r24", "r25"
    );
    //Arduboy bootloader and reset button feature (Arduboy: 64 bytes, DevKit: 68 bytes)
    asm volatile (
#ifdef     AB_DEVKIT
      "    in	r24, %[pinb]            \n\t" // down, left, up buttons
      "    andi r24, 0x70               \n\t" 
      "    sbis %[pinc], 6	            \n\t" // right button
      "    ori	r24, 0x04	            \n\t" 
      "    sbis %[pinf], 7	            \n\t" // A button
      "    ori	r24, 0x02	            \n\t" 
      "    sbis %[pinf], 6	            \n\t" // B button
      "    ori	r24, 0x01	            \n\t" 
      "    cpi	r24, 0x43	            \n\t" // test LEFT+UP+A+B for bootloader
      "    breq .l3                     \n\t" 
      "    cpi  r24, 0x37               \n\t" // test RIGHT+DOWN+A+B for reset sketch
      "    brne .l5                     \n\t" 
#else
      "    in	r24, %[pinf]            \n\t" // directional buttons
      "    andi r24, 0xF0               \n\t" 
      "    sbis %[pine], 6	            \n\t" // A button
      "    ori	r24, 0x08	            \n\t" 
      "    sbis %[pinb], 4	            \n\t" // B button
      "    ori	r24, 0x04	            \n\t" 
      "    cpi	r24, 0x5C	            \n\t" // test LEFT+UP+A+B for bootloader
      "    breq .l3                     \n\t" 
      "    cpi  r24, 0xAC               \n\t" // test RIGHT+DOWN+A+B for reset sketch
      "    brne .l5                     \n\t" 
#endif      
      ".l3:                             \n\t" 
      "    lds  r0, %[hold]             \n\t" 
      "    sub  r25, r0                 \n\t" // r25 = (uint8_t)(timer0_millis >> 8)
      "    brcc .l4                     \n\t" 
      "    neg  r25                     \n\t" 
      ".l4:                             \n\t" 
      "    cpi  r25, 8                  \n\t" 
      "    brcs .l6                     \n\t" // if ((millis - hold) < 8)
      "                                 \n\t" 
#ifdef     AB_DEVKIT  
      "    subi r24, 0x43 - 0x77        \n\t" //get bootloader key or reset key value
#else      
      "    subi r24, 0x5C - 0x77        \n\t" //get bootloader key or reset key value
#endif  
      "    sts	0x800, r24              \n\t" 
      "    sts	0x801, r24              \n\t" 
      "    ldi	r24, %[value1]          \n\t" 
      "    ldi	r25, %[value2]          \n\t" 
      "    wdr                          \n\t" 
      "    sts   %[wdtcsr], r24         \n\t" 
      "    sts   %[wdtcsr], r25         \n\t" 
      "    rjmp .-2                     \n\t"
      ".l5:                             \n\t"
      "    sts  %[hold], r25            \n\t"
      ".l6:                             \n\t"
      :
      : [pinf]      "I" (_SFR_IO_ADDR(PINF)),
        [pine]      "I" (_SFR_IO_ADDR(PINE)),
        [pinc]      "I" (_SFR_IO_ADDR(PINC)),
        [pinb]      "I" (_SFR_IO_ADDR(PINB)),
        [hold]      ""  (RAMEND),
        [value1]    "M" ((uint8_t)(_BV(WDCE) | _BV(WDE))),
        [value2]    "M" ((uint8_t)(_BV(WDE))),                         
        [wdtcsr]    "n" (_SFR_MEM_ADDR(WDTCSR))
      : "r0", "r24", "r25" ,"r30", "r31"
    );
    //timer0_overflow_count++;
    asm volatile (
      "    ld   r24, z                  \n\t"  
      "    ldd  r25, z+1                \n\t"  
      "    adiw r24, 1                  \n\t"
      "    st   z, r24                  \n\t"  
      "    std  z+1, r25                \n\t"  
      "    ldd  r24, z+2                \n\t"  
      "    ldd  r25, z+3                \n\t"  
      "    adc  r24, r1                 \n\t"  
      "    adc  r25, r1                 \n\t"  
      "    std  z+2, r24                \n\t"  
      "    std  z+3, r25                \n\t"  
      :
      : [ptr]   "z" (&timer0_overflow_count)
      : "r24", "r25"
    );
}

unsigned long millis()
{
    unsigned long m;
    uint8_t oldSREG = SREG;

    // disable interrupts while we read timer0_millis or we might get an
    // inconsistent value (e.g. in the middle of a write to timer0_millis)
    cli();
    m = timer0_millis;
    SREG = oldSREG;

    return m;
}

unsigned long micros() {
    unsigned long m;
    uint8_t oldSREG = SREG, t;
    
    cli();
    m = timer0_overflow_count;
//    asm volatile(
//      "    ld   r24, z+                 \n\t"          
//      "    ld   r25, z+                 \n\t"          
//      "    ld   r26, z+                 \n\t"          
//      "    ld   r27, z                  \n\t"          
//      "                                 \n\t"          
//      "                                 \n\t"          
//      "                                 \n\t"          
//      :
//      : [count] "z" (&timer0_overflow_count),
//        [tcnt]   "M" (_BV(CS11))
//      : "r24", "r30", "r31"
//    );
    
#if defined(TCNT0)
    t = TCNT0;
#elif defined(TCNT0L)
    t = TCNT0L;
#else
    #error TIMER 0 not defined
#endif

#ifdef TIFR0
    if ((TIFR0 & _BV(TOV0)) && (t < 255))
        m++;
#else
    if ((TIFR & _BV(TOV0)) && (t < 255))
        m++;
#endif

    SREG = oldSREG;
    
    return ((m << 8) + t) * (64 / clockCyclesPerMicrosecond());
}

void delay(unsigned long ms)
{
    uint32_t start = micros();

    while (ms > 0) {
        yield();
        while ( ms > 0 && (micros() - start) >= 1000) {
            ms--;
            start += 1000;
        }
    }
}

/* Delay for the given number of microseconds.  Assumes a 1, 8, 12, 16, 20 or 24 MHz clock. */
void delayMicroseconds(unsigned int us)
{
    // call = 4 cycles + 2 to 4 cycles to init us(2 for constant delay, 4 for variable)

    // calling avrlib's delay_us() function with low values (e.g. 1 or
    // 2 microseconds) gives delays longer than desired.
    //delay_us(us);
#if F_CPU >= 24000000L
    // for the 24 MHz clock for the aventurous ones, trying to overclock

    // zero delay fix
    if (!us) return; //  = 3 cycles, (4 when true)

    // the following loop takes a 1/6 of a microsecond (4 cycles)
    // per iteration, so execute it six times for each microsecond of
    // delay requested.
    us *= 6; // x6 us, = 7 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 22 (24) cycles above, remove 5, (5*4=20)
    // us is at least 6 so we can substract 5
    us -= 5; //=2 cycles

#elif F_CPU >= 20000000L
    // for the 20 MHz clock on rare Arduino boards

    // for a one-microsecond delay, simply return.  the overhead
    // of the function call takes 18 (20) cycles, which is 1us
    __asm__ __volatile__ (
        "nop" "\n\t"
        "nop" "\n\t"
        "nop" "\n\t"
        "nop"); //just waiting 4 cycles
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes a 1/5 of a microsecond (4 cycles)
    // per iteration, so execute it five times for each microsecond of
    // delay requested.
    us = (us << 2) + us; // x5 us, = 7 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 26 (28) cycles above, remove 7, (7*4=28)
    // us is at least 10 so we can substract 7
    us -= 7; // 2 cycles

#elif F_CPU >= 16000000L
    // for the 16 MHz clock on most Arduino boards

    // for a one-microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 1us
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/4 of a microsecond (4 cycles)
    // per iteration, so execute it four times for each microsecond of
    // delay requested.
    us <<= 2; // x4 us, = 4 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 19 (21) cycles above, remove 5, (5*4=20)
    // us is at least 8 so we can substract 5
    us -= 5; // = 2 cycles,

#elif F_CPU >= 12000000L
    // for the 12 MHz clock if somebody is working with USB

    // for a 1 microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 1.5us
    if (us <= 1) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/3 of a microsecond (4 cycles)
    // per iteration, so execute it three times for each microsecond of
    // delay requested.
    us = (us << 1) + us; // x3 us, = 5 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 20 (22) cycles above, remove 5, (5*4=20)
    // us is at least 6 so we can substract 5
    us -= 5; //2 cycles

#elif F_CPU >= 8000000L
    // for the 8 MHz internal clock

    // for a 1 and 2 microsecond delay, simply return.  the overhead
    // of the function call takes 14 (16) cycles, which is 2us
    if (us <= 2) return; //  = 3 cycles, (4 when true)

    // the following loop takes 1/2 of a microsecond (4 cycles)
    // per iteration, so execute it twice for each microsecond of
    // delay requested.
    us <<= 1; //x2 us, = 2 cycles

    // account for the time taken in the preceeding commands.
    // we just burned 17 (19) cycles above, remove 4, (4*4=16)
    // us is at least 6 so we can substract 4
    us -= 4; // = 2 cycles

#else
    // for the 1 MHz internal clock (default settings for common Atmega microcontrollers)

    // the overhead of the function calls is 14 (16) cycles
    if (us <= 16) return; //= 3 cycles, (4 when true)
    if (us <= 25) return; //= 3 cycles, (4 when true), (must be at least 25 if we want to substract 22)

    // compensate for the time taken by the preceeding and next commands (about 22 cycles)
    us -= 22; // = 2 cycles
    // the following loop takes 4 microseconds (4 cycles)
    // per iteration, so execute it us/4 times
    // us is at least 4, divided by 4 gives us 1 (no zero delay bug)
    us >>= 2; // us div 4, = 4 cycles
    

#endif

    // busy wait
    __asm__ __volatile__ (
        "1: sbiw %0,1" "\n\t" // 2 cycles
        "brne 1b" : "=w" (us) : "0" (us) // 2 cycles
    );
    // return = 4 cycles
}

void init() //assembly optimized by 68 bytes
{
    // this needs to be called before setup() or some functions won't
    // work there
    sei();
    
    // on the ATmega168, timer 0 is also used for fast hardware pwm
    // (using phase-correct PWM would mean that timer 0 overflowed half as often
    // resulting in different millis() behavior on the ATmega8 and ATmega168)
#if defined(TCCR0A) && defined(WGM01)
    //sbi(TCCR0A, WGM01);
    //sbi(TCCR0A, WGM00);
  asm volatile(
      "    in   r24, %[tccr0a]          \n\t"          
      "    ori  r24, %[wgm01]           \n\t"          
      "    out  %[tccr0a], r24          \n\t"          
      "    ori  r24, %[wgm00]           \n\t"          
      "    out  %[tccr0a], r24          \n\t"          
      :
      : [tccr0a] "I" (_SFR_IO_ADDR(TCCR0A)),
        [wgm01]  "M" (_BV(WGM01)),
        [wgm00]  "M" (_BV(WGM00))
      : "r24"
    );
#endif

    // set timer 0 prescale factor to 64
#if defined(__AVR_ATmega128__)
    // CPU specific: different values for the ATmega128
    sbi(TCCR0, CS02);
#elif defined(TCCR0) && defined(CS01) && defined(CS00)
    // this combination is for the standard atmega8
    sbi(TCCR0, CS01);
    sbi(TCCR0, CS00);
#elif defined(TCCR0B) && defined(CS01) && defined(CS00)
    // this combination is for the standard 168/328/1280/2560
    //sbi(TCCR0B, CS01);
    //sbi(TCCR0B, CS00);
    asm volatile(
      "    in   r24, %[tccr0b]          \n\t"          
      "    ori  r24, %[cs01]            \n\t"          
      "    out  %[tccr0b], r24          \n\t"          
      "    ori  r24, %[cs00]            \n\t"          
      "    out  %[tccr0b], r24          \n\t"          
      :
      : [tccr0b] "I" (_SFR_IO_ADDR(TCCR0B)),
        [cs01]   "M" (_BV(CS01)),
        [cs00]   "M" (_BV(CS00))
      : "r24"
    );
#elif defined(TCCR0A) && defined(CS01) && defined(CS00)
    // this combination is for the __AVR_ATmega645__ series
    sbi(TCCR0A, CS01);
    sbi(TCCR0A, CS00);
#else
    #error Timer 0 prescale factor 64 not set correctly
#endif

    // enable timer 0 overflow interrupt
#if defined(TIMSK) && defined(TOIE0)
    sbi(TIMSK, TOIE0);
#elif defined(TIMSK0) && defined(TOIE0)
    //sbi(TIMSK0, TOIE0);
    asm volatile(
      "    ldi  r30, %[timsk0]          \n\t"          
      "    ldi  r31, 0x00               \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[toie0]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [timsk0] "M" (_SFR_MEM_ADDR(TIMSK0)),
        [toie0]  "M" (_BV(TOIE0))
      : "r24", "r30", "r31"
    );
#else
    #error  Timer 0 overflow interrupt not set correctly
#endif

    // timers 1 and 2 are used for phase-correct hardware pwm
    // this is better for motors as it ensures an even waveform
    // note, however, that fast pwm mode can achieve a frequency of up
    // 8 MHz (with a 16 MHz clock) at 50% duty cycle

#if defined(TCCR1B) && defined(CS11) && defined(CS10)
    //TCCR1B = 0;

    // set timer 1 prescale factor to 64
    //sbi(TCCR1B, CS11);
    asm volatile(
      "    ldi  r30, %[tccr1b]          \n\t"          
      "    st   z, r1                   \n\t"          
      "    ldi  r24, %[cs11]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr1b] "M" (_SFR_MEM_ADDR(TCCR1B)),
        [cs11]   "M" (_BV(CS11))
      : "r24", "r30", "r31"
    );
    
#if F_CPU >= 8000000L
    //sbi(TCCR1B, CS10);
    asm volatile(
      "    ori  r24, %[cs10]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [cs10] "M" (_BV(CS10))
      : "r24", "r30", "r31"
    );
#endif
#elif defined(TCCR1) && defined(CS11) && defined(CS10)
    sbi(TCCR1, CS11);
#if F_CPU >= 8000000L
    sbi(TCCR1, CS10);
#endif
#endif
    // put timer 1 in 8-bit phase correct pwm mode
#if defined(TCCR1A) && defined(WGM10)
    //sbi(TCCR1A, WGM10);
    asm volatile(
      "    ldi  r30, %[tccr1a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm10]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr1a] "M" (_SFR_MEM_ADDR(TCCR1A)),
        [wgm10]  "M" (_BV(WGM10))
      : "r24", "r30", "r31"
    );
#endif

    // set timer 2 prescale factor to 64
#if defined(TCCR2) && defined(CS22)
    sbi(TCCR2, CS22);
#elif defined(TCCR2B) && defined(CS22)
    sbi(TCCR2B, CS22);
//#else
    // Timer 2 not finished (may not be present on this CPU)
#endif

    // configure timer 2 for phase correct pwm (8-bit)
#if defined(TCCR2) && defined(WGM20)
    sbi(TCCR2, WGM20);
#elif defined(TCCR2A) && defined(WGM20)
    sbi(TCCR2A, WGM20);
//#else
    // Timer 2 not finished (may not be present on this CPU)
#endif

#if defined(TCCR3B) && defined(CS31) && defined(WGM30)
    //sbi(TCCR3B, CS31);      // set timer 3 prescale factor to 64
    //sbi(TCCR3B, CS30);
    asm volatile(
      "    ldi  r30, %[tccr3b]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[cs31]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs30]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr3b] "M" (_SFR_MEM_ADDR(TCCR3B)),
        [cs31]   "M" (_BV(CS31)),
        [cs30]   "M" (_BV(CS30))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR3A, WGM30);     // put timer 3 in 8-bit phase correct pwm mode
    asm volatile(
      "    ldi  r30, %[tccr3a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm30]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr3a] "M" (_SFR_MEM_ADDR(TCCR3A)),
        [wgm30]  "M" (_BV(WGM30))
      : "r24", "r30", "r31"
    );
#endif

#if defined(TCCR4A) && defined(TCCR4B) && defined(TCCR4D) /* beginning of timer4 block for 32U4 and similar */
    //sbi(TCCR4B, CS42);      // set timer4 prescale factor to 64
    //sbi(TCCR4B, CS41);
    //sbi(TCCR4B, CS40);
    asm volatile(
      "    ldi  r30, %[tccr4b]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[cs42]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs41]            \n\t"          
      "    st   z, r24                  \n\t"          
      "    ori  r24, %[cs40]            \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4b] "M" (_SFR_MEM_ADDR(TCCR4B)),
        [cs42]   "M" (_BV(CS42)),
        [cs41]   "M" (_BV(CS41)),
        [cs40]   "M" (_BV(CS40))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4D, WGM40);     // put timer 4 in phase- and frequency-correct PWM mode 
    asm volatile(
      "    ldi  r30, %[tccr4d]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[wgm40]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4d] "M" (_SFR_MEM_ADDR(TCCR4D)),
        [wgm40]  "M" (_BV(WGM40))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4A, PWM4A);     // enable PWM mode for comparator OCR4A
    asm volatile(
      "    ldi  r30, %[tccr4a]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[pwm4a]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4a] "M" (_SFR_MEM_ADDR(TCCR4A)),
        [pwm4a]  "M" (_BV(PWM4A))
      : "r24", "r30", "r31"
    );
    //sbi(TCCR4C, PWM4D);     // enable PWM mode for comparator OCR4D
    asm volatile(
      "    ldi  r30, %[tccr4c]          \n\t"          
      "    ld   r24, z                  \n\t"          
      "    ori  r24, %[pwm4d]           \n\t"          
      "    st   z, r24                  \n\t"          
      :
      : [tccr4c] "M" (_SFR_MEM_ADDR(TCCR4C)),
        [pwm4d]  "M" (_BV(PWM4D))
      : "r24", "r30", "r31"
    );
#else /* beginning of timer4 block for ATMEGA1280 and ATMEGA2560 */
#if defined(TCCR4B) && defined(CS41) && defined(WGM40)
    sbi(TCCR4B, CS41);      // set timer 4 prescale factor to 64
    sbi(TCCR4B, CS40);
    sbi(TCCR4A, WGM40);     // put timer 4 in 8-bit phase correct pwm mode
#endif
#endif /* end timer4 block for ATMEGA1280/2560 and similar */   

#if defined(TCCR5B) && defined(CS51) && defined(WGM50)
    sbi(TCCR5B, CS51);      // set timer 5 prescale factor to 64
    sbi(TCCR5B, CS50);
    sbi(TCCR5A, WGM50);     // put timer 5 in 8-bit phase correct pwm mode
#endif

#if defined(ADCSRA)
    // set a2d prescaler so we are inside the desired 50-200 KHz range.
    #if F_CPU >= 16000000 // 16 MHz / 128 = 125 KHz
        //sbi(ADCSRA, ADPS2);
        //sbi(ADCSRA, ADPS1);
        //sbi(ADCSRA, ADPS0);
        asm volatile(
          "    ldi  r30, %[adcsra]          \n\t"          
          "    ld   r24, z                  \n\t"          
          "    ori  r24, %[adps2]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps1]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps0]           \n\t"          
          "    st   z, r24                  \n\t"          
          :
          : [adcsra] "M" (_SFR_MEM_ADDR(ADCSRA)),
            [adps2]  "M" (_BV(ADPS2)),
            [adps1]  "M" (_BV(ADPS1)),
            [adps0]  "M" (_BV(ADPS0))
          : "r24", "r30", "r31"
        );
    #elif F_CPU >= 8000000 // 8 MHz / 64 = 125 KHz
        //sbi(ADCSRA, ADPS2);
        //sbi(ADCSRA, ADPS1);
        //cbi(ADCSRA, ADPS0);
        asm volatile(
          "    ldi  r30, %[adcsra]          \n\t"          
          "    ld   r24, z                  \n\t"          
          "    ori  r24, %[adps2]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    ori  r24, %[adps1]           \n\t"          
          "    st   z, r24                  \n\t"          
          "    andi r24, %[adps0]           \n\t"          
          "    st   z, r24                  \n\t"          
          :
          : [adcsra] "M" (_SFR_MEM_ADDR(ADCSRA)),
            [adps2]  "M" (_BV(ADPS2)),
            [adps1]  "M" (_BV(ADPS1)),
            [adps0]  "M" (~_BV(ADPS0))
          : "r24", "r30", "r31"
        );
    #elif F_CPU >= 4000000 // 4 MHz / 32 = 125 KHz
        sbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #elif F_CPU >= 2000000 // 2 MHz / 16 = 125 KHz
        sbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        cbi(ADCSRA, ADPS0);
    #elif F_CPU >= 1000000 // 1 MHz / 8 = 125 KHz
        cbi(ADCSRA, ADPS2);
        sbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #else // 128 kHz / 2 = 64 KHz -> This is the closest you can get, the prescaler is 2
        cbi(ADCSRA, ADPS2);
        cbi(ADCSRA, ADPS1);
        sbi(ADCSRA, ADPS0);
    #endif
    // enable a2d conversions
    //sbi(ADCSRA, ADEN);
        asm volatile(
          "    ori  r24, %[aden]            \n\t" 
          "    st   z, r24                  \n\t"          
          :
          : [aden] "M" (_BV(ADEN))
          : "r24", "r30", "r31"
        );
#endif

    // the bootloader connects pins 0 and 1 to the USART; disconnect them
    // here so they can be used as normal digital i/o; they will be
    // reconnected in Serial.begin()
#if defined(UCSRB)
    UCSRB = 0;
#elif defined(UCSR0B)
    UCSR0B = 0;
#endif
}
