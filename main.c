/*
 * Description: Full game, version one
 * Game: 
 * Display: 
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#include <util/delay.h>

#include "nokia5110.c"

// TIMING BEGIN
volatile unsigned char TimerFlag = 0; // TimerISR() sets this to 1. C programmer should clear to 0.

// Internal variables for mapping AVR's ISR to our cleaner TimerISR model.
unsigned long _avr_timer_M = 1; // Start count from here, down to 0. Default 1 ms.
unsigned long _avr_timer_cntcurr = 0; // Current internal count of 1ms ticks

void TimerOn() {
	// AVR timer/counter controller register TCCR1
	TCCR1B = 0x0B;// bit3 = 0: CTC mode (clear timer on compare)
	// bit2bit1bit0=011: pre-scaler /64
	// 00001011: 0x0B
	// SO, 8 MHz clock or 8,000,000 /64 = 125,000 ticks/s
	// Thus, TCNT1 register will count at 125,000 ticks/s

	// AVR output compare register OCR1A.
	OCR1A = 125;	// Timer interrupt will be generated when TCNT1==OCR1A
	// We want a 1 ms tick. 0.001 s * 125,000 ticks/s = 125
	// So when TCNT1 register equals 125,
	// 1 ms has passed. Thus, we compare to 125.
	// AVR timer interrupt mask register
	TIMSK1 = 0x02; // bit1: OCIE1A -- enables compare match interrupt

	//Initialize avr counter
	TCNT1=0;

	_avr_timer_cntcurr = _avr_timer_M;
	// TimerISR will be called every _avr_timer_cntcurr milliseconds

	//Enable global interrupts
	SREG |= 0x80; // 0x80: 1000000
}

void TimerOff() {
	TCCR1B = 0x00; // bit3bit1bit0=000: timer off
}

void TimerISR() {
	TimerFlag = 1;
}

// In our approach, the C programmer does not touch this ISR, but rather TimerISR()
ISR(TIMER1_COMPA_vect) {
	// CPU automatically calls when TCNT1 == OCR1 (every 1 ms per TimerOn settings)
	_avr_timer_cntcurr--; // Count down to 0 rather than up to TOP
	if (_avr_timer_cntcurr == 0) { // results in a more efficient compare
		TimerISR(); // Call the ISR that the user uses
		_avr_timer_cntcurr = _avr_timer_M;
	}
}

// Set TimerISR() to tick every M ms
void TimerSet(unsigned long M) {
	_avr_timer_M = M;
	_avr_timer_cntcurr = _avr_timer_M;
}
// TIMING END


// JOYSTICK BEGIN
void InitADC(void)
{
	ADMUX|=(1<<REFS0);
	ADCSRA|=(1<<ADEN)|(1<<ADPS0)|(1<<ADPS1)|(1<<ADPS2); //ENABLE ADC, PRESCALER 128
}

uint16_t readadc(uint8_t ch)
{
	ch&=0b00000111;         //ANDing to limit input to 7
	ADMUX = (ADMUX & 0xf8)|ch;  //Clear last 3 bits of ADMUX, OR with ch
	ADCSRA|=(1<<ADSC);        //START CONVERSION
	while((ADCSRA)&(1<<ADSC));    //WAIT UNTIL CONVERSION IS COMPLETE
	return(ADC);        //RETURN ADC VALUE
}
#define buttonUp (readadc(0) > 600)
#define buttonDown (readadc(0) < 300)
#define buttonRight (readadc(1) > 600)
#define buttonLeft (readadc(1) < 300)
#define buttonShoot (~PINB & 0x01)
#define buttonShoot2 (~PINB & 0x02)
#define buttonRight2 (readadc(4) > 600)
#define buttonLeft2 (readadc(4) < 300)
#define buttonReset (~PINB & 0x04)
// JOYSTICK END

// game state
unsigned char playingGame;
unsigned char winLose = 0;
unsigned char cnt = 0;
const unsigned char displayTime = 10; // .050 sec period - 5 seconds
unsigned doReset;

// ship
const unsigned char maxX = 81; // 84x48
const unsigned char minX = 3;
const unsigned char initX = 41;
unsigned char xPosition; // input/output

// bullet
signed char bulletXPos; // output/input
signed char bulletYPos; // output/input
const unsigned char bulletMaxY = 46; // 84x48
const unsigned char bulletInitY = 4;
unsigned char bulletLife;

// enemy states
const unsigned char enemyNumber = 10;
unsigned char enemyAlive[10]; // initialize all to one
unsigned char enemyRL[10]; // initialize all to one
unsigned char enemyLeft; //initialize to enemy number - win condition if == 0

// enemy positions
unsigned char enemyXPos[10]; // input <- size of enemyNumber
unsigned char enemyYPos[10]; // input <- size of enemyNumber
const unsigned char maxXEnemy = 80;
const unsigned char minXEnemy = 3;
const unsigned char minYEnemy = 4;

// player 2
unsigned const char maxX2 = 81; // 84x48
unsigned const char minX2 = 3;
unsigned const char initX2 = 40;
unsigned char xPosition2; // input/output

const unsigned char bulletInitY2 = 43;
const unsigned char maxY2 = 1; // 84x48

unsigned char bulletXPos2; // output/input
unsigned char bulletYPos2; // output/input

unsigned playerWin;
unsigned player2Win;


char playerHit2(unsigned char xCoor, unsigned char yCoor) // hitbox/hurtbox setup
{
	if(xCoor == bulletXPos) // middle column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "top" row
		{
			return 1;
		}
	}
	else if((xCoor - 1) == bulletXPos) // left column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor + 1) == bulletXPos) // right column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor + 2) == bulletXPos) // right column
	{
		if((yCoor + 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor - 2) == bulletXPos) // right column
	{
		if((yCoor + 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
	}
	
	return 0;
}

char playerHit(unsigned char xCoor, unsigned char yCoor) // hitbox/hurtbox setup
{
	if(xCoor == bulletXPos2) // middle column
	{
		if(yCoor == bulletYPos2) // middle row
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos2) // "bottom row"
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos2) // "top" row
		{
			return 1;
		}
	}
	else if((xCoor - 1) == bulletXPos2) // left column
	{
		if(yCoor == bulletYPos2) // middle row
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos2) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor + 1) == bulletXPos2) // right column
	{
		if(yCoor == bulletYPos2) // middle row
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos2) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor + 2) == bulletXPos2) // right column
	{
		if((yCoor - 1) == bulletYPos2) // "bottom row"
		{
			return 1;
		}
	}
	else if((xCoor - 2) == bulletXPos2) // right column
	{
		if((yCoor - 1) == bulletYPos2) // "bottom row"
		{
			return 1;
		}
	}
	
	return 0;
}

void displayShipInit() // call only when playing game is started
{
	// draw ship
	nokia_lcd_set_pixel(initX - 2, 0, 1); // bottom
	nokia_lcd_set_pixel(initX - 1, 0, 1);
	nokia_lcd_set_pixel(initX, 0, 1);
	nokia_lcd_set_pixel(initX + 1, 0, 1);
	nokia_lcd_set_pixel(initX + 2, 0, 1);
	nokia_lcd_set_pixel(initX, 1, 1); // center
	nokia_lcd_set_pixel(initX - 1, 1, 1); // left
	nokia_lcd_set_pixel(initX + 1, 1, 1); // right
	nokia_lcd_set_pixel(initX, 2, 1); // top
	
	/*		00000 
	 *		 000
	 *		  0	 */
}

void displayMoveLeft(char xPosition) // call every time ship moves left
{
	// erase
	nokia_lcd_set_pixel(xPosition - 2, 0, 0); // bottom
	nokia_lcd_set_pixel(xPosition - 1, 0, 0);
	nokia_lcd_set_pixel(xPosition, 0, 0);
	nokia_lcd_set_pixel(xPosition + 1, 0, 0);
	nokia_lcd_set_pixel(xPosition + 2, 0, 0);
	nokia_lcd_set_pixel(xPosition, 1, 0); // center
	nokia_lcd_set_pixel(xPosition - 1, 1, 0); // left
	nokia_lcd_set_pixel(xPosition + 1, 1, 0); // right
	nokia_lcd_set_pixel(xPosition, 2, 0); // top
	
	// move left
	nokia_lcd_set_pixel((xPosition - 2) - 1, 0, 1); // bottom
	nokia_lcd_set_pixel((xPosition - 1) - 1, 0, 1);
	nokia_lcd_set_pixel((xPosition) - 1, 0, 1);
	nokia_lcd_set_pixel((xPosition + 1) - 1, 0, 1);
	nokia_lcd_set_pixel((xPosition + 2) - 1, 0, 1);
	nokia_lcd_set_pixel((xPosition) - 1, 1, 1); // center
	nokia_lcd_set_pixel((xPosition - 1) - 1, 1, 1); // left
	nokia_lcd_set_pixel((xPosition + 1) - 1, 1, 1); // right
	nokia_lcd_set_pixel((xPosition) - 1, 2, 1); // top
}

void displayMoveRight(char xPosition) // call every time ship moves left
{
	// erase
	nokia_lcd_set_pixel(xPosition - 2, 0, 0); // bottom
	nokia_lcd_set_pixel(xPosition - 1, 0, 0);
	nokia_lcd_set_pixel(xPosition, 0, 0);
	nokia_lcd_set_pixel(xPosition + 1, 0, 0);
	nokia_lcd_set_pixel(xPosition + 2, 0, 0);
	nokia_lcd_set_pixel(xPosition, 1, 0); // center
	nokia_lcd_set_pixel(xPosition - 1, 1, 0); // left
	nokia_lcd_set_pixel(xPosition + 1, 1, 0); // right
	nokia_lcd_set_pixel(xPosition, 2, 0); // top
	
	// move left
	nokia_lcd_set_pixel((xPosition - 2) + 1, 0, 1); // bottom
	nokia_lcd_set_pixel((xPosition - 1) + 1, 0, 1);
	nokia_lcd_set_pixel((xPosition) + 1, 0, 1);
	nokia_lcd_set_pixel((xPosition + 1) + 1, 0, 1);
	nokia_lcd_set_pixel((xPosition + 2) + 1, 0, 1);
	nokia_lcd_set_pixel((xPosition) + 1, 1, 1); // center
	nokia_lcd_set_pixel((xPosition + 1) - 1, 1, 1); // left
	nokia_lcd_set_pixel((xPosition + 1) + 1, 1, 1); // right
	nokia_lcd_set_pixel((xPosition) + 1, 2, 1); // top
}

// player 2
void displayShipInit2() // call only when playing game is started
{
	// draw ship
	nokia_lcd_set_pixel(initX2 - 2, 47, 1); // bottom
	nokia_lcd_set_pixel(initX2 - 1, 47, 1);
	nokia_lcd_set_pixel(initX2, 47, 1);
	nokia_lcd_set_pixel(initX2 + 1, 47, 1);
	nokia_lcd_set_pixel(initX2 + 2, 47, 1);
	nokia_lcd_set_pixel(initX2, 46, 1); // center
	nokia_lcd_set_pixel(initX2 - 1, 46, 1); // left
	nokia_lcd_set_pixel(initX2 + 1, 46, 1); // right
	nokia_lcd_set_pixel(initX2, 45, 1); // top
	
	/*		00000 
	 *		 000
	 *		  0	 */
}

void displayMoveLeft2(char xPosition) // call every time ship moves left
{
	// erase
	nokia_lcd_set_pixel(xPosition2 - 2, 47, 0); // bottom
	nokia_lcd_set_pixel(xPosition2 - 1, 47, 0);
	nokia_lcd_set_pixel(xPosition2, 47, 0);
	nokia_lcd_set_pixel(xPosition2 + 1, 47, 0);
	nokia_lcd_set_pixel(xPosition2 + 2, 47, 0);
	nokia_lcd_set_pixel(xPosition2, 46, 0); // center
	nokia_lcd_set_pixel(xPosition2 - 1, 46, 0); // left
	nokia_lcd_set_pixel(xPosition2 + 1, 46, 0); // right
	nokia_lcd_set_pixel(xPosition2, 45, 0); // top
	
	// move left
	nokia_lcd_set_pixel((xPosition2 - 2) - 1, 47, 1); // bottom
	nokia_lcd_set_pixel((xPosition2 - 1) - 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2) - 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2 + 1) - 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2 + 2) - 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2) - 1, 46, 1); // center
	nokia_lcd_set_pixel((xPosition2 - 1) - 1, 46, 1); // left
	nokia_lcd_set_pixel((xPosition2 + 1) - 1, 46, 1); // right
	nokia_lcd_set_pixel((xPosition2) - 1, 45, 1); // top
}

void displayMoveRight2(char xPosition) // call every time ship moves left
{
	// erase
	nokia_lcd_set_pixel(xPosition2 - 2, 47, 0); // bottom
	nokia_lcd_set_pixel(xPosition2 - 1, 47, 0);
	nokia_lcd_set_pixel(xPosition2, 47, 0);
	nokia_lcd_set_pixel(xPosition2 + 1, 47, 0);
	nokia_lcd_set_pixel(xPosition2 + 2, 47, 0);
	nokia_lcd_set_pixel(xPosition2, 46, 0); // center
	nokia_lcd_set_pixel(xPosition2 - 1, 46, 0); // left
	nokia_lcd_set_pixel(xPosition2 + 1, 46, 0); // right
	nokia_lcd_set_pixel(xPosition2, 45, 0); // top
	
	// move left
	nokia_lcd_set_pixel((xPosition2 - 2) + 1, 47, 1); // bottom
	nokia_lcd_set_pixel((xPosition2 - 1) + 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2) + 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2 + 1) + 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2 + 2) + 1, 47, 1);
	nokia_lcd_set_pixel((xPosition2) + 1, 46, 1); // center
	nokia_lcd_set_pixel((xPosition2 - 1) + 1, 46, 1); // left
	nokia_lcd_set_pixel((xPosition2 + 1) + 1, 46, 1); // right
	nokia_lcd_set_pixel((xPosition2) + 1, 45, 1); // top
}


void eraseBullet(char bulletXPos, char bulletYPos)
{
	nokia_lcd_set_pixel(bulletXPos, bulletYPos + 1, 0); // top		|
	nokia_lcd_set_pixel(bulletXPos, bulletYPos, 0); // center	|
	nokia_lcd_set_pixel(bulletXPos, bulletYPos - 1, 0); // bottom	|
}

void displayBullet(char bulletXPos, char bulletYPos)
{
	nokia_lcd_set_pixel(bulletXPos, bulletYPos + 1, 1); // top		|
	nokia_lcd_set_pixel(bulletXPos, bulletYPos, 1); // center	|
	nokia_lcd_set_pixel(bulletXPos, bulletYPos - 1, 1); // bottom	|
}

void enemyInit()
{
	enemyXPos[0] = 3;
	enemyXPos[1] = 10;
	enemyXPos[2] = 17;
	enemyXPos[3] = 24;
	enemyXPos[4] = 31;
	enemyXPos[5] = 38;
	enemyXPos[6] = 45;
	enemyXPos[7] = 52;
	enemyXPos[8] = 59;
	enemyXPos[9] = 66;
	// enemyXPos[10] = 73;
	// enemyXPos[11] = 80; // 12 enemies
	
	for(int i = 0; i < enemyNumber; i++)
	{
		enemyYPos[i] = 45;
		enemyRL[i] = 1; // 1 - left, 0 - right
		enemyAlive[i] = 1;
		
		nokia_lcd_set_pixel(enemyXPos[i] - 1, 46, 1);	// top row
		nokia_lcd_set_pixel(enemyXPos[i], 46, 1);
		nokia_lcd_set_pixel(enemyXPos[i] + 1, 46, 1);
		nokia_lcd_set_pixel(enemyXPos[i] - 1, 45, 1);		// mid row
		nokia_lcd_set_pixel(enemyXPos[i], 45, 1);
		nokia_lcd_set_pixel(enemyXPos[i] + 1, 45, 1);
		nokia_lcd_set_pixel(enemyXPos[i] - 1, 44, 1);	// bottom row
		//nokia_lcd_set_pixel(enemyXPos[i], 44, 1);
		nokia_lcd_set_pixel(enemyXPos[i] + 1, 44, 1);
	}
}

void enemyEraseAll()
{
	for(int i = 0; i < enemyNumber; i++)
	{
		if(enemyAlive[i])
		{
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i] + 1, 0);	// top row
			nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i] + 1, 0);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i] + 1, 0);
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i], 0);		// mid row
			nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i], 0);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i], 0);
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i] - 1, 0);	// bottom row
			//nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i] - 1, 0);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i] - 1, 0);
		}
	}
}

void enemyEraseIndv(char xCoor, char yCoor)
{
	nokia_lcd_set_pixel(xCoor - 1, yCoor + 1, 0);	// top row
	nokia_lcd_set_pixel(xCoor, yCoor + 1, 0);
	nokia_lcd_set_pixel(xCoor + 1, yCoor + 1, 0);
	nokia_lcd_set_pixel(xCoor - 1, yCoor, 0);		// mid row
	nokia_lcd_set_pixel(xCoor, yCoor, 0);
	nokia_lcd_set_pixel(xCoor + 1, yCoor, 0);
	nokia_lcd_set_pixel(xCoor - 1, yCoor - 1, 0);	// bottom row
	//nokia_lcd_set_pixel(xCoor, yCoor - 1, 0);
	nokia_lcd_set_pixel(xCoor + 1, yCoor - 1, 0);
}

char enemyHit(unsigned char xCoor, unsigned char yCoor) // hitbox/hurtbox setup
{
	if(xCoor == bulletXPos) // middle column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if(yCoor == (bulletYPos + 1))
		{
			return 1;
		}
		else if(yCoor == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "top" row
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos - 1))
		{
			return 1;
		}
	}
	else if((xCoor - 1) == bulletXPos) // left column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if(yCoor == (bulletYPos + 1))
		{
			return 1;
		}
		else if(yCoor == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "top" row
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos - 1))
		{
			return 1;
		}
	}
	else if((xCoor + 1) == bulletXPos) // right column
	{
		if(yCoor == bulletYPos) // middle row
		{
			return 1;
		}
		else if(yCoor == (bulletYPos + 1))
		{
			return 1;
		}
		else if(yCoor == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor - 1) == bulletYPos) // "bottom row"
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor - 1) == (bulletYPos - 1))
		{
			return 1;
		}
		else if((yCoor + 1) == bulletYPos) // "top" row
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos + 1))
		{
			return 1;
		}
		else if((yCoor + 1) == (bulletYPos - 1))
		{
			return 1;
		}
	}
	
	return 0;
}


char bulletHit()
{
	for(int i = 0; i < enemyNumber; i++)
	{
		if(enemyAlive[i] == 1)
		{
			if(enemyHit(enemyXPos[i], enemyYPos[i]) == 1) // change to enemyHit
			{
				return 1;
			}
		}
	}
	return 0;
}

void enemyMoveAll()
{
	for(int i = 0; i < enemyNumber; i++)
	{
		if(enemyAlive[i])
		{
			if(enemyRL[i] == 1 && enemyXPos[i] > minXEnemy) // move left
			{
				enemyXPos[i]--;
			}
			else if(enemyRL[i] == 1 && enemyXPos[i] <= minXEnemy) // cant move left, move down then set RL to 0
			{
				enemyYPos[i] = enemyYPos[i] - 5;
				enemyRL[i] = 0;
			}
			else if(enemyRL[i] == 0 && enemyXPos[i] < maxXEnemy) // move right
			{
				enemyXPos[i]++;
			}
			else if(enemyRL[i] == 0 && enemyXPos[i] >= maxXEnemy) // // cant move right move down then set RL to 1
			{
				enemyYPos[i] = enemyYPos[i] - 5;
				enemyRL[i] = 1;
			}
			
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i] + 1, 1);	// top row
			nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i] + 1, 1);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i] + 1, 1);
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i], 1);		// mid row
			nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i], 1);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i], 1);
			nokia_lcd_set_pixel(enemyXPos[i] - 1, enemyYPos[i] - 1, 1);	// bottom row
			//nokia_lcd_set_pixel(enemyXPos[i], enemyYPos[i] - 1, 1);
			nokia_lcd_set_pixel(enemyXPos[i] + 1, enemyYPos[i] - 1, 1);
			
			if(enemyHit(enemyXPos[i], enemyYPos[i]))
			{
				enemyLeft--; // update win condition
				enemyAlive[i] = 0; // no longer char about this enemy
				enemyEraseIndv(enemyXPos[i], enemyYPos[i]); // erase from screen - not neccessary?
				bulletLife = 0;
				
				if(enemyLeft <= 0)
				{
					winLose = 1;
					playingGame = 0;
				}
			}
			else if(enemyYPos[i] <= minYEnemy)
			{
				winLose = 0; // lose condition fulfilled
				playingGame = 0;
			}
		}
	}
}

enum MenuStates {menuStart, menuTitle, menu1P, menu2P, menuCredits, menuCreditSelect, menuPlaying, menuPlaying2, menuGameOver, menuGameOver2} menuState;
enum MoveStates {moveStart, moveInactive, moveWait, moveLeft, moveRight} moveState;
enum ShootStates {shootStart, shootInactive, shootWait, shootFire, shootFired, shootHit} shootState;
enum EnemyStates {enemyStart, enemyInactive, enemyActive} enemyState;
enum MoveStates2 {move2Start, move2Inactive, move2Wait, move2Left, move2Right} move2State;
enum Shoot2States {shoot2Start, shoot2Inactive, shoot2Wait, shoot2Fire, shoot2Fired, shoot2Hit} shoot2State;

void menuTick()
{
	switch(menuState) // transitions
	{
		case menuStart:
			nokia_lcd_clear();
			playingGame = 0;
			doReset = 0;
			menuState = menuTitle;
			break;
		case menuTitle:
			if(buttonShoot)
			{
				menuState = menu1P;
				nokia_lcd_clear();
			}
			break;
		case menu1P:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(buttonShoot) // select
			{
				menuState = menuPlaying;
				playingGame = 1;
				nokia_lcd_clear();
			}
			else if(buttonDown) // move cursor down
			{
				menuState = menu2P;
				nokia_lcd_clear();
			}
			else if(buttonUp) // move cursor up
			{
				// do nothing
			}
			break;
		case menu2P:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(buttonShoot) // select
			{
				playingGame = 2;
				menuState = menuPlaying2;
				nokia_lcd_clear();
			}
			else if(buttonDown) // move cursor down
			{
				menuState = menuCredits;
				nokia_lcd_clear();
			}
			else if(buttonUp) // move cursor up
			{
				menuState = menu1P;
				nokia_lcd_clear();
			}
			break;
		case menuCredits:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(buttonShoot) // select
			{
				menuState = menuCreditSelect;
				nokia_lcd_clear();
			}
			else if(buttonDown) // move cursor down
			{
				// do nothing
			}
			else if(buttonUp) // move cursor up
			{
				menuState = menu2P;
				nokia_lcd_clear();
			}
			break;
		case menuCreditSelect:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(buttonShoot) // select
			{
				menuState = menu1P;
				nokia_lcd_clear();
			}
			break;
		case menuPlaying:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(playingGame == 0)
			{
				menuState = menuGameOver;
				nokia_lcd_clear();
			}
			break;
		case menuPlaying2:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(playingGame == 0)
			{
				menuState = menuGameOver2;
				nokia_lcd_clear();
			}
			break;
		case menuGameOver:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(cnt >= displayTime)
			{
				menuState = menu1P;
				cnt = 0;
				nokia_lcd_clear();
			}
			break;
		case menuGameOver2:
			if(buttonReset)
			{
				doReset = 1;
			}
			else if(cnt >= displayTime)
			{
				menuState = menu1P;
				cnt = 0;
				nokia_lcd_clear();
			}
			break;
	}
	
	switch(menuState) // actions
	{
		case menuStart:
			break;
		case menuTitle:
			// printToScreen: IMBEDDED INVADERS(centered)
			nokia_lcd_set_cursor(0, 4);
			nokia_lcd_write_string("IMBEDDED",1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("INVADER",2);
			nokia_lcd_render();
		break;
		case menu1P:
			nokia_lcd_set_cursor(0, 0);
			nokia_lcd_write_string("> 1 Player",1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("  2 Player VS",1);
			nokia_lcd_set_cursor(0, 40);
			nokia_lcd_write_string("  Credits",1);
			nokia_lcd_render();
			// printToScreen: > 1 Player
			break;
		case menu2P:
			nokia_lcd_set_cursor(0, 0);
			nokia_lcd_write_string("  1 Player",1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("> 2 Player VS",1);
			nokia_lcd_set_cursor(0, 40);
			nokia_lcd_write_string("  Credits",1);
			nokia_lcd_render();
			// printToScreen: > 2 Player
			break;
		case menuCredits:
			nokia_lcd_set_cursor(0, 0);
			nokia_lcd_write_string("  1 Player",1);
			nokia_lcd_set_cursor(0, 20);
			nokia_lcd_write_string("  2 Player VS",1);
			nokia_lcd_set_cursor(0, 40);
			nokia_lcd_write_string("> Credits",1);
			nokia_lcd_render();
			// printToScreen: > Credits
			break;
		case menuCreditSelect:
			nokia_lcd_set_cursor(0, 0);
			nokia_lcd_write_string("  Made by:",1);
			nokia_lcd_set_cursor(0, 10);
			nokia_lcd_write_string("  NRC", 1);
			nokia_lcd_set_cursor(0, 30);
			nokia_lcd_write_string("> Return",1);
			nokia_lcd_render();
			break;
		case menuPlaying:
			// do nothing - handled in other SMs
			break;
		case menuPlaying2:
			break;
		case menuGameOver:
			cnt++;
			if(winLose)
			{
				nokia_lcd_clear();
				nokia_lcd_set_cursor(0, 0);
				nokia_lcd_write_string("Enemy Destroy",1);
				nokia_lcd_set_cursor(0, 10);
				nokia_lcd_write_string("YOU WIN", 2);
				nokia_lcd_set_cursor(0, 40);
				nokia_lcd_write_string(":)", 1);
				nokia_lcd_render();
			}
			else
			{
				nokia_lcd_clear();
				nokia_lcd_set_cursor(0, 0);
				nokia_lcd_write_string("Enemy Invaded",1);
				nokia_lcd_set_cursor(0, 10);
				nokia_lcd_write_string("YOU LOSE", 2);
				nokia_lcd_set_cursor(0, 40);
				nokia_lcd_write_string(":(", 1);
				nokia_lcd_render();
			}
			break;
		case menuGameOver2:
			cnt++;
			if(player2Win == 1 && playerWin == 1)
			{
				nokia_lcd_clear();
				nokia_lcd_set_cursor(0, 0);
				nokia_lcd_write_string("DRAW",3);
				nokia_lcd_render();
			}
			else if(playerWin == 1)
			{
				nokia_lcd_clear();
				nokia_lcd_set_cursor(0, 0);
				nokia_lcd_write_string("TOP",2);
				nokia_lcd_set_cursor(0, 20);
				nokia_lcd_write_string("WINS", 2);
				//nokia_lcd_set_cursor(0, 40);
				//nokia_lcd_write_string(":)", 1);
				nokia_lcd_render();
			}
			else if(player2Win == 1)
			{
				nokia_lcd_clear();
				nokia_lcd_set_cursor(0, 0);
				nokia_lcd_write_string("BOTTOM",2);
				nokia_lcd_set_cursor(0, 20);
				nokia_lcd_write_string("WINS", 2);
				//nokia_lcd_set_cursor(0, 40);
				//nokia_lcd_write_string(":)", 1);
				nokia_lcd_render();
			}
			break;
	}
}

void moveShip()
{
	switch(moveState) // transitions
	{
		case moveStart:
			playerWin = 0;
			player2Win = 0;
			moveState = moveInactive;
			break;
		case moveInactive:
			if(playingGame == 1 || playingGame == 2)
			{
				moveState = moveWait;
				displayShipInit();
				xPosition = initX;
				bulletXPos = 0;
				bulletYPos = 0;
				bulletXPos2 = 0;
				bulletYPos2 = 0;
			}
			break;
		case moveWait:
			if(playerHit(xPosition, 1))
			{
				moveState = moveInactive;
				nokia_lcd_clear();
				player2Win = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				moveState = moveInactive;
			}
			else if(buttonLeft && !buttonRight && (xPosition > minX)) // move left button is pressed (not at left edge of screen)
			{
				moveState = moveLeft;
			}
			else if(buttonRight && !buttonLeft && (xPosition < maxX)) // move right button is pressed (not at right edge of screen)
			{
				moveState = moveRight;
			}
			break;
		case moveLeft:
			if(playerHit(xPosition, 1))
			{
				nokia_lcd_clear();
				moveState = moveInactive;
				player2Win = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				moveState = moveInactive;
			}
			else if(buttonLeft && !buttonRight && (xPosition > minX)) // move left button is pressed (not at left edge of screen)
			{
				// do nothing
			}
			else if(buttonRight && !buttonLeft && (xPosition < maxX)) // move right button is pressed (not at right edge of screen)
			{
				moveState = moveRight; // immediately move right (lessens delay)
			}
			else
			{
				moveState = moveWait;
			}
			break;
		case moveRight:
			if(playerHit(xPosition, 1))
			{
				nokia_lcd_clear();
				moveState = moveInactive;
				player2Win = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				moveState = moveInactive;
			}
			else if(buttonLeft && !buttonRight && (xPosition > minX)) // move left button is pressed (not at left edge of screen)
			{
				moveState = moveLeft; // immediately move left (lessens delay)
			}
			else if(buttonRight && !buttonLeft && (xPosition < maxX)) // move right button is pressed (not at right edge of screen)
			{
				// do nothing
			}
			else
			{
				moveState = moveWait;
			}
			break;
	}
	
	switch(moveState) // actions
	{
		case moveStart:
			break;
		case moveInactive:
			break;
		case moveWait:
			// display ship position - not moving
			break;
		case moveLeft:
			displayMoveLeft(xPosition); // change display (erase->add)
			xPosition--;
			break;
		case moveRight:
			displayMoveRight(xPosition); // change display (erase->add)
			xPosition++;
			break;
	}
}

void shipShoot()
{
	switch(shootState) // transitions
	{
		case shootStart:
			shootState = shootInactive;
			bulletXPos = 0;
			bulletYPos = 0;
			break;
		case shootInactive:
			if(playingGame == 1 || playingGame == 2)
			{
				shootState = shootWait;
			}
			break;
		case shootWait:
			if(playingGame == 0)
			{
				shootState = shootInactive;
			}
			else if(buttonShoot)
			{
				shootState = shootFire;
			}
			break;
		case shootFire:
			if(playingGame == 0)
			{
				shootState = shootInactive;
			}
			shootState = shootFired;
			break;
		case shootFired:
			if(playingGame == 0)
			{
				shootState = shootInactive;
			}
			else if(bulletLife == 0)
			{
				// hitPos[0] = bulletXPos;
				// hitPos[1] = bulletYPos;
				eraseBullet(bulletXPos, bulletYPos);
				bulletXPos = 0;
				bulletYPos = 0; 
				shootState = shootWait;
			}
			else if( bulletYPos >= bulletMaxY) // assumming 47 is edge of board
			{
				// hitPos[0] = -1;
				// hitPos[1] = -1;
				eraseBullet(bulletXPos, bulletYPos);
				shootState = shootWait;
				bulletLife = 0;
			}
			break;
		case shootHit: // only so that enemy state machine can read bullet positions before it is reset
			shootState = shootWait;
			break;
	}
	
	switch(shootState) // actions
	{
		case shootStart:
			break;
		case shootInactive:
			break;
		case shootWait:
			bulletLife = 0;
			bulletXPos = 0; // so that bullet doesnt stay active after enemies are hit
			bulletYPos = 0;
			break;
		case shootFire:
			bulletXPos = xPosition;
			bulletYPos = bulletInitY;
			bulletLife = 1;
			displayBullet(bulletXPos, bulletYPos);
			// erase, display shot
			break;
		case shootFired:
			eraseBullet(bulletXPos, bulletYPos);
			bulletYPos++;
			displayBullet(bulletXPos, bulletYPos);
			// erase, display shot
			break;
		case shootHit:
			break;
	}
}

void enemyTick()
{
	switch(enemyState) // transitions
	{
		case enemyStart:
			enemyState = enemyInactive;
			break;
		case enemyInactive:
			if(playingGame == 1)
			{
				enemyState = enemyActive;
				enemyLeft = 10;
				enemyInit();
			}
			break;
		case enemyActive:
			if(playingGame == 0)
			{
				enemyState = enemyInactive;
			}
			break;
	}
	
	switch(enemyState) // transitions
	{
		case enemyStart:
			break;
		case enemyInactive:
			break;
		case enemyActive:
			enemyEraseAll();
			enemyMoveAll();
			break;
	}
}

void shipShoot2()
{
	switch(shoot2State) // transitions
	{
		case shoot2Start:
			shoot2State = shoot2Inactive;
			break;
		case shoot2Inactive:
			if(playingGame == 2)
			{
				shoot2State = shoot2Wait;
				playerWin = 0;
				player2Win = 0;
			}
			break;
		case shoot2Wait:
			if(playingGame == 0)
			{
				shoot2State = shoot2Inactive;
			}
			else if(buttonShoot2)
			{
				shoot2State = shoot2Fire;
			}
			break;
		case shoot2Fire:
			shoot2State = shoot2Fired;
			if(playingGame == 0)
			{
				shoot2State = shoot2Inactive;
			}
			break;
		case shoot2Fired:
			if(playingGame == 0)
			{
				shoot2State = shoot2Inactive;
			}
			else if( bulletYPos2 <= maxY2) // assumming 47 is edge of board
			{
				// hitPos[0] = -1;
				// hitPos[1] = -1;
				eraseBullet(bulletXPos2, bulletYPos2);
				shoot2State = shoot2Wait;
			}
			break;
		case shoot2Hit: // only so that enemy state machine can read bullet positions before it is reset
			shoot2State = shoot2Wait;
			break;
	}
	
	switch(shoot2State) // actions
	{
		case shoot2Start:
		break;
		case shoot2Wait:
		bulletXPos2 = 0; // so that bullet doesnt stay active after enemies are hit
		bulletYPos2 = 0;
		break;
		case shoot2Fire:
		bulletXPos2 = xPosition2;
		bulletYPos2 = bulletInitY2;
		displayBullet(bulletXPos2, bulletYPos2);
		// erase, display shot
		break;
		case shoot2Fired:
		eraseBullet(bulletXPos2, bulletYPos2);
		bulletYPos2--;
		displayBullet(bulletXPos2, bulletYPos2);
		// erase, display shot
		break;
		case shoot2Hit:
		break;
		case shoot2Inactive:
		break;
	}
}

	void moveP2()
	{
		switch(move2State) // transitions
		{
			case move2Start:
				playerWin = 0;
				player2Win = 0;
				move2State = move2Inactive;
				break;
			case move2Inactive:
			if(playingGame == 2)
			{
				move2State = move2Wait;
				displayShipInit2();
				xPosition2 = initX2;
			}
			break;
			case move2Wait:
			if(playerHit2(xPosition2, 46))
			{
				move2State = move2Inactive;
				nokia_lcd_clear();
				playerWin = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				move2State = move2Inactive;
			}
			else if(buttonLeft2 && !buttonRight2 && (xPosition2 > minX2)) // move left button is pressed (not at left edge of screen)
			{
				move2State = move2Left;
			}
			else if(buttonRight2 && !buttonLeft2 && (xPosition2 < maxX2)) // move right button is pressed (not at right edge of screen)
			{
				move2State = move2Right;
			}
			break;
			case move2Left:
			if(playerHit2(xPosition2, 46))
			{
				move2State = move2Inactive;
				playerWin = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				move2State = move2Inactive;
			}
			else if(buttonLeft2 && !buttonRight2 && (xPosition2 > minX2)) // move left button is pressed (not at left edge of screen)
			{
				// do nothing
			}
			else if(buttonRight2 && !buttonLeft2 && (xPosition2 < maxX2)) // move right button is pressed (not at right edge of screen)
			{
				move2State = move2Right; // immediately move right (lessens delay)
			}
			else
			{
				move2State = move2Wait;
			}
			break;
			case move2Right:
			if(playerHit2(xPosition2, 46))
			{
				move2State = move2Inactive;
				nokia_lcd_clear();
				playerWin = 1;
				playingGame = 0;
			}
			else if(playingGame == 0)
			{
				move2State = move2Inactive;
			}
			else if(buttonLeft2 && !buttonRight2 && (xPosition2 > minX)) // move left button is pressed (not at left edge of screen)
			{
				move2State = move2Left; // immediately move left (lessens delay)
			}
			else if(buttonRight2 && !buttonLeft2 && (xPosition2 < maxX)) // move right button is pressed (not at right edge of screen)
			{
				// do nothing
			}
			else
			{
				move2State = move2Wait;
			}
			break;
		}
		
		switch(move2State) // actions
		{
			case move2Start:
			break;
			case move2Inactive:
			break;
			case move2Wait:
			// display ship position - not moving
			break;
			case move2Left:
			displayMoveLeft2(xPosition2); // change display (erase->add)
			xPosition2--;
			break;
			case move2Right:
			displayMoveRight2(xPosition2); // change display (erase->add)
			xPosition2++;
			break;
		}
	}

int main(void)
{
    DDRB = 0x00; PORTB = 0xFF; // Configure port B's 8 pins as inputs
    DDRD = 0xFF; PORTD = 0x00; // Configure port D's 8 pins as outputs
	
	TimerSet(1); // period 50 for now
	TimerOn();
	
	nokia_lcd_init(); // display
	nokia_lcd_clear();
	
	InitADC(); // controller
	
	xPosition = initX; // maybe 41 - mid screen on start up
	bulletXPos = 0;
	bulletYPos = 0;
	enemyLeft = 10;
	playingGame = 0; // should initialize to zero with menu added
	cnt = 0;
	xPosition2 = initX2;
	bulletXPos2 = 0;
	bulletYPos2 = 0;
	
	doReset = 0;
	
	moveState = moveStart;
	shootState = shootStart;
	enemyState = enemyStart;
	move2State = move2Start;
	shoot2State = shoot2Start;
	
	while(1)
	{
		menuTick();
		moveShip();
		moveP2();
		shipShoot();
		shipShoot2();
		enemyTick();
		
		
		while(!TimerFlag);
		TimerFlag = 0;
		
		nokia_lcd_render();
		
		if(doReset == 1)
		{
			xPosition = initX; // maybe 41 - mid screen on start up
			bulletXPos = 0;
			bulletYPos = 0;
			enemyLeft = 10;
			playingGame = 0; // should initialize to zero with menu added
			cnt = 0;
			xPosition2 = initX2;
			bulletXPos2 = 0;
			bulletYPos2 = 0;
			bulletLife = 0;
			
			menuState = menuStart;
			moveState = moveStart;
			shootState = shootStart;
			enemyState = enemyStart;
			move2State = move2Start;
			shoot2State = shoot2Start;
			nokia_lcd_clear();
		}
	}
}

