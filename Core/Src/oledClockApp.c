/*
 * oledClockApp.c
 *
 *  Created on: Apr 5, 2025
 *      Author: dwilk
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ssd1306.h"
#include "ssd1306_tests.h"
#include "ssd1306_fonts.h"
#include "stdbool.h"
#include "math.h"
#include "stm32l4xx_hal.h"
#include "app.h"

#define TEA5767_I2C_ADDR (0x60 << 1) // 7-bit address shifted for STM32 HAL
#define TEA5767_MUTE  0x80
#define TEA5767_SCAN  0x40
#define TEA5767_PLLREF 0x10
#define TEA5767_HIGHLO 0x08

void DisplayFM(void);
void DisplayTimeOled(void);
void TEA5767_SetFrequency(float freqMHz, bool mute, bool searchUp, bool searchMode);
void TEA5767_Status(void);
void DisplayAlarm(void);
void AlarmProc(void);
void TimeFace(void);
char* GetOrdinalSuffix(int number);

extern RTC_HandleTypeDef hrtc;
extern I2C_HandleTypeDef hi2c2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

static int32_t lastEncoderValue = 0;
static int lastEncoder = 0;

volatile int button = 0;
volatile int buttonCounter = 0;
volatile int buttonPressed = 0;
volatile int menuSelect = 0;
volatile int elementSelect = 0;
volatile int editElement = 0;
volatile int flagA = 0;
int elementInc = 0;
bool muteS = false;
bool wasA;

float readFreq = 0;
int freqI = 0;
int scanFlag = 0;
float lastScan = 0;
int adcLevel = 0;
float setFreq = 0;
int setFreqS = 0;

const char* weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
RTC_TimeTypeDef sTime = {0};
RTC_DateTypeDef sDate = {0};
RTC_AlarmTypeDef sAlarmA = {0};
RTC_AlarmTypeDef sAlarmB = {0};
HAL_StatusTypeDef result = {1};

void App_Init(void) {
	ssd1306_Init();
	HAL_RTC_Init(&hrtc);
	HAL_TIM_Base_Start_IT(&htim3);

	sTime.Hours = 21;   // 12-hour format
	sTime.Minutes = 24;
	sTime.Seconds = 0;
	sTime.TimeFormat = RTC_HOURFORMAT_24; // 24-hour format

	sDate.WeekDay = RTC_WEEKDAY_MONDAY;
	sDate.Month = RTC_MONTH_APRIL;
	sDate.Date = 5;
	sDate.Year = 25;    // Year 2025

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);


    TEA5767_SetFrequency(88.1, true, false, false);
    TEA5767_Status();
}

void App_MainLoop(void) {

	int32_t currentEncoderValue = __HAL_TIM_GET_COUNTER(&htim2);
	int32_t diff = (int16_t)(currentEncoderValue - lastEncoderValue);
	if (diff > 1) {
	    // Clockwise rotation
		__HAL_TIM_SET_COUNTER(&htim3, __HAL_TIM_GET_AUTORELOAD(&htim3));
	    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);  // Turn LED on
	    if (editElement == 0){
	    	elementSelect--;
	    }
	    else if (editElement != 0){
	    	elementInc = -1;
	    }
	}
	else if (diff < -1) {
	    // Counter-clockwise rotation
		__HAL_TIM_SET_COUNTER(&htim3, __HAL_TIM_GET_AUTORELOAD(&htim3));
	    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);  // Turn LED off
	    if (editElement == 0){
	    	elementSelect++;
	    }
	    else if (editElement != 0){
	       	elementInc = 1;
	    }
	}
	lastEncoderValue = currentEncoderValue;
	lastEncoder = lastEncoderValue;
	switch(menuSelect){
		case 0:{
			DisplayTimeOled();
			break;
		}
		case 1:{
			DisplayFM();
			break;
		}
		case 2:{
			DisplayAlarm();
			break;
		}
		case 3:{
			menuSelect = 0;
			break;
		}
		case 4:{
			AlarmProc();
			break;
		}
		case 5:{
			if (wasA){
				TEA5767_SetFrequency(readFreq, true, false, false);
			}
			else {
				TEA5767_SetFrequency(readFreq, false, false, false);
			}
			wasA = false;
			menuSelect = 6;
			break;
		}
		case 6:{
			TimeFace();
			break;
		}
		case 7:{
			HAL_TIM_Base_Start(&htim3);
			menuSelect =  0;
		}
	}
}

void TEA5767_SetFrequency(float freqMHz, bool mute, bool searchUp, bool searchMode)
{
    uint8_t txbuf[5] = {0};
    uint16_t pll;
    float IF = 0.225;
    muteS = mute;
    pll = (uint16_t)(((freqMHz + IF) * 1000000) / 8192);
    // Byte 1: High 6 bits of PLL, bits 7-6 = 00
    txbuf[0] = ((pll >> 8) & 0x3F); // Ensure 01xxxxxx pattern
    if (mute) {
        txbuf[0] |= 0x80;
    }
    if(searchMode){
    	txbuf[0] |= 0x40;
    }
    // Byte 2: Low 8 bits of PLL
    txbuf[1] = pll & 0xFF;
    // Byte 3: 10111000 — PLLREF (0x80) | HIGHLO (0x10) | SUD (0x20) | MUTE (0x80 if mute in byte 0 only)
    txbuf[2] = 0x38;
    if (searchUp){
    	txbuf[2] |= 0x80;
    }
    // Byte 4: 00010000 — HCC = 1
    txbuf[3] = 0x14;

    // Byte 5: 01000000 — DTC = 1 (75us)
    txbuf[4] = 0x40;
    result = HAL_I2C_Master_Transmit(&hi2c2, TEA5767_I2C_ADDR, txbuf, 5, HAL_MAX_DELAY);
}

void TEA5767_Status(void){
	uint8_t rxbuf[5];

	result = HAL_I2C_Master_Receive(&hi2c2, TEA5767_I2C_ADDR | 0x01, rxbuf, 5, HAL_MAX_DELAY);
	uint16_t pll = ((rxbuf[0] & 0x3F) << 8) | rxbuf[1];
	float IF = 0.225; // Intermediate Frequency in MHz
	readFreq = roundf((((float)pll * 8192.0f) / 1000000.0f - IF)*10.0f)/10.0f;
	freqI = (int)(readFreq * 100);
	uint8_t signalLevel = (rxbuf[3] & 0xF0) >> 4;
	adcLevel = signalLevel;
}

void TimeFace(void){
	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	editElement = 0;
	elementInc = 0;
	elementSelect = 0;
	char alarmMenu[20];
	ssd1306_Reset();
	ssd1306_Fill(Black);
	sprintf(alarmMenu, "20%d", sDate.Year);
	ssd1306_SetCursor(95, 0);
	ssd1306_WriteString(alarmMenu, Font_7x10, White);
	sprintf(alarmMenu, "%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
	ssd1306_SetCursor(24, 16);
	ssd1306_WriteString(alarmMenu, Font_16x26, White);
	const char* weekdayStr = weekdays[sDate.WeekDay - 1];
    const char* monthStr = months[sDate.Month -1];
	sprintf(alarmMenu, "%s, %s %s", weekdayStr, monthStr, GetOrdinalSuffix(sDate.Date));
	ssd1306_SetCursor(0, 50);
	ssd1306_WriteString(alarmMenu, Font_7x10, White);
	ssd1306_UpdateScreen();
	HAL_SuspendTick();
	HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

char* GetOrdinalSuffix(int number) {
    static char ordinal[10];
    if (number % 100 >= 11 && number % 100 <= 13) {
        sprintf(ordinal, "%dth", number);
    }
    else {
        switch (number % 10) {
            case 1:
                sprintf(ordinal, "%dst", number);
                break;
            case 2:
                sprintf(ordinal, "%dnd", number);
                break;
            case 3:
                sprintf(ordinal, "%drd", number);
                break;
            default:
                sprintf(ordinal, "%dth", number);
                break;
        }
    }
    return ordinal;
}

void AlarmProc(void){
	if (flagA != 1){
		if (muteS){
			wasA = muteS;
		}
		else{
			wasA = false;
		}
		TEA5767_SetFrequency(readFreq, false, false, false);
	}
	flagA = 1;
	editElement = 0;
	elementInc = 0;
	elementSelect = 0;
    static int counter = 0;
    char alarmMenu[20];
    ssd1306_Reset();
    ssd1306_Fill(Black);
    if (counter % 20 < 10) {
        sprintf(alarmMenu, "ALARM!!!");
        ssd1306_SetCursor(0, 20);
        ssd1306_WriteString(alarmMenu, Font_16x26, White);
    }
    else {
        ssd1306_SetCursor(0, 20);
        ssd1306_WriteString(alarmMenu, Font_16x26, Black);
    }
    ssd1306_UpdateScreen();
    counter++;
}


void DisplayAlarm(void){
	char alarmMenu[20];
	static uint8_t editingHour;
	static uint8_t editingMinute;
	static uint8_t editingSecond;
	HAL_RTC_GetAlarm(&hrtc, &sAlarmA, RTC_ALARM_A, RTC_FORMAT_BIN);
	HAL_RTC_GetAlarm(&hrtc, &sAlarmB, RTC_ALARM_B, RTC_FORMAT_BIN);
	ssd1306_Reset();
	ssd1306_Fill(Black);
	sprintf(alarmMenu, "Set Alarm:  Next");
	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString(alarmMenu, Font_7x10, White);
	sprintf(alarmMenu, "Alarm 1: %02d:%02d:%02d", sAlarmA.AlarmTime.Hours, sAlarmA.AlarmTime.Minutes, sAlarmA.AlarmTime.Seconds);
	ssd1306_SetCursor(0, 16);
	ssd1306_WriteString(alarmMenu, Font_7x10, White);
	switch (elementSelect) {
		case -1:{
			elementSelect = 0;
			break;
		}
		case 0: {
			sprintf(alarmMenu, "Next");
			ssd1306_SetCursor(85, 0);
			ssd1306_WriteString(alarmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			editElement = 0;
			elementInc = 0;
			break;
		}
		case 1:{
			if (editElement == 0){
				editingHour = sAlarmA.AlarmTime.Hours;
				editingMinute = sAlarmA.AlarmTime.Minutes;
				editingSecond = sAlarmA.AlarmTime.Seconds;
				sprintf(alarmMenu, "%02d:%02d:%02d", sAlarmA.AlarmTime.Hours, sAlarmA.AlarmTime.Minutes, sAlarmA.AlarmTime.Seconds);
				ssd1306_SetCursor(63, 16);
				ssd1306_WriteString(alarmMenu, Font_7x10, Black);
				ssd1306_UpdateScreen();
			}
			else if (editElement == 1){
				sprintf(alarmMenu, "%02d", editingHour);
				ssd1306_SetCursor(63, 16);
				ssd1306_WriteString(alarmMenu, Font_7x10, Black);
				ssd1306_UpdateScreen();
				if (elementInc == 1){
					editingHour = (editingHour + 1) % 24;
					elementInc = 0;
				}
				else if (elementInc == -1) {
					editingHour = (editingHour == 0) ? 23 : editingHour - 1;
					elementInc = 0;
				}
			}
			else if (editElement == 2){
				sAlarmA.AlarmTime.Hours = editingHour;
				HAL_RTC_SetAlarm_IT(&hrtc, &sAlarmA, RTC_FORMAT_BIN);
				editElement = 3;
			}
			else if (editElement == 3){
				sprintf(alarmMenu, "%02d", editingMinute);
				ssd1306_SetCursor(83, 16);
				ssd1306_WriteString(alarmMenu, Font_7x10, Black);
				ssd1306_UpdateScreen();
				if (elementInc == 1) {
					editingMinute = (editingMinute + 1) % 60;
					elementInc = 0;
				}
				else if (elementInc == -1) {
					editingMinute = (editingMinute == 0) ? 59 : editingMinute - 1;
					elementInc = 0;
				}
			}
			else if (editElement == 4){
				sAlarmA.AlarmTime.Minutes = editingMinute;
				HAL_RTC_SetAlarm_IT(&hrtc, &sAlarmA, RTC_FORMAT_BIN);
				editElement = 5;
			}
			else if (editElement == 5){
				sprintf(alarmMenu, "%02d", editingSecond);
				ssd1306_SetCursor(104, 16);
				ssd1306_WriteString(alarmMenu, Font_7x10, Black);
				ssd1306_UpdateScreen();
				if (elementInc == 1) {
					editingSecond = (editingSecond + 1) % 60;
					elementInc = 0;
				}
				else if (elementInc == -1) {
					editingSecond = (editingSecond == 0) ? 59 : editingSecond - 1;
					elementInc = 0;
				}
			}
			else if (editElement == 6){
				sAlarmA.AlarmTime.Seconds = editingSecond;
				HAL_RTC_SetAlarm_IT(&hrtc, &sAlarmA, RTC_FORMAT_BIN);
				editElement = 0;
				elementInc = 0;
			}
			break;
		}
		case 2:{
			elementSelect = 1;
			break;
		}
	}
}

void DisplayFM(void){
	TEA5767_Status();
	char fmMenu[20];
	ssd1306_Reset();
	ssd1306_Fill(Black);
	sprintf(fmMenu, "FM Radio  %d  Next",adcLevel);
	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString(fmMenu, Font_7x10, White);
	sprintf(fmMenu, "Freq: %d.%02d MHz", freqI / 100, abs(freqI % 100));
	ssd1306_SetCursor(0, 16);
	ssd1306_WriteString(fmMenu, Font_7x10, White);
	sprintf(fmMenu, "scan: up / down");
	ssd1306_SetCursor(0, 28);
	ssd1306_WriteString(fmMenu, Font_7x10, White);
	sprintf(fmMenu, "Toggle: on/off");
	ssd1306_SetCursor(0, 40);
	ssd1306_WriteString(fmMenu, Font_7x10, White);
	switch (elementSelect){
		case -1:{
			elementSelect = 0;
			break;
		}
		case 0:{
			sprintf(fmMenu, "Next");
			ssd1306_SetCursor(89, 0);
			ssd1306_WriteString(fmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			editElement = 0;
			elementInc = 0;
			break;
		}
		case 1:{
			if (editElement == 0){
			    setFreqS = freqI;
			}
			sprintf(fmMenu, " %d.%02d MHz  ", setFreqS / 100, abs(setFreqS % 100));
			ssd1306_SetCursor(34, 16);
			ssd1306_WriteString(fmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			if(editElement == 1){
				if (scanFlag == 0){
					setFreq = readFreq;
				}
				scanFlag = 1;
				if (elementInc == 1){
					setFreq = roundf((setFreq + .1)*10.0f)/10.0f;
					setFreqS = (int)(setFreq *100);
					elementInc = 0;
				}
				else if (elementInc == -1){
					setFreq = roundf((setFreq - .1)*10.0f)/10.0f;
					setFreqS = (int)(setFreq *100);
					elementInc = 0;
				}
			}
			else if (editElement > 1){
				TEA5767_SetFrequency(setFreq, false, false, false);
				elementInc = 0;
				editElement = 0;
				scanFlag = 0;
			}
			break;
		}
		case 2:{
			sprintf(fmMenu, " up ");
			ssd1306_SetCursor(35, 28);
			ssd1306_WriteString(fmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			if (editElement == 1){
				if (scanFlag == 0){
					TEA5767_SetFrequency(readFreq+.1, false, true, true);
				}
				if (lastScan == readFreq){
					editElement++;
				}
				lastScan = readFreq;
				scanFlag = 1;
			}
			else if (editElement == 2){
				TEA5767_SetFrequency(readFreq, false, false, false);
				lastScan = 0;
				scanFlag = 0;
				editElement = 0;
				elementInc = 0;
			}
			break;
		}
		case 3:{
			sprintf(fmMenu, " down ");
			ssd1306_SetCursor(70, 28);
			ssd1306_WriteString(fmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			if (editElement == 1){
			    if (scanFlag == 0){
			    	TEA5767_SetFrequency(readFreq-.1, false, false, true);
			    }
			    if (lastScan == readFreq){
			    	editElement++;
			    }
			    lastScan = readFreq;
			    scanFlag = 1;
			}
			else if (editElement == 2){
			    TEA5767_SetFrequency(readFreq, false, false, false);
			    lastScan = 0;
			    scanFlag = 0;
			    editElement = 0;
			    elementInc = 0;
			}
			break;
		}
		case 4:{
			sprintf(fmMenu, "on/off");
			ssd1306_SetCursor(56, 40);
			ssd1306_WriteString(fmMenu, Font_7x10, Black);
			ssd1306_UpdateScreen();
			if (editElement == 1){
				if (muteS){
					TEA5767_SetFrequency(readFreq, false, false, false);
				}
				else {
					TEA5767_SetFrequency(readFreq, true, false, false);
				}
				editElement = 2;
			}
			else if (editElement == 2){
				editElement = 0;
				elementInc = 0;
			}
			break;
		}
		case 5:{
			elementSelect = 4;
			break;
		}
	}
}

void DisplayTimeOled(void) {
    char timeStr[20];
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    ssd1306_Reset();
    ssd1306_Fill(Black);
    // Format the time as a string
    sprintf(timeStr, "Set Time: Next");
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(timeStr, Font_7x10, White);
    sprintf(timeStr, "Time: %02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
    ssd1306_SetCursor(0, 16);
    ssd1306_WriteString(timeStr, Font_7x10, White);

    sprintf(timeStr, "Date: %02d/%02d/%02d", sDate.Month, sDate.Date, 2000 + sDate.Year);
    ssd1306_SetCursor(0, 28);
    ssd1306_WriteString(timeStr, Font_7x10, White);

    const char* weekdayStr = weekdays[sDate.WeekDay - 1];

    sprintf(timeStr, "Weekday: %s",weekdayStr);
    ssd1306_SetCursor(0, 40);
    ssd1306_WriteString(timeStr, Font_7x10, White);
    switch(elementSelect){
    	case -1:{
    		elementSelect = 0;
    		break;
    	}
    	case 0:{
    		sprintf(timeStr, "Next");
    		ssd1306_SetCursor(70, 0);
    	    ssd1306_WriteString(timeStr, Font_7x10, Black);
    	    ssd1306_UpdateScreen();
    	    editElement = 0;
    	    elementInc = 0;
    	    break;
    	}
    	case 1:{
    		if (editElement == 0){
    			sprintf(timeStr, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
    			ssd1306_SetCursor(42, 16);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    		}
    		else if (editElement == 1){
       	    	sprintf(timeStr, "%02d", sTime.Hours);
       	    	ssd1306_SetCursor(42, 16);
       	    	ssd1306_WriteString(timeStr, Font_7x10, Black);
       	    	ssd1306_UpdateScreen();
       	    	if (elementInc == 1){
       	    		sTime.Hours = (sTime.Hours + 1) % 24;
       	   	    	elementInc = 0;
       	       	}
       	    	else if (elementInc == -1){
       	    		sTime.Hours = (sTime.Hours == 0) ? 23 : sTime.Hours - 1;
       	    		elementInc = 0;
       	    	}
       	    	HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
       	    }
    		else if (editElement == 2){
    			sprintf(timeStr, "%02d", sTime.Minutes);
    			ssd1306_SetCursor(62, 16);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    		   	if (elementInc == 1){
    		   		sTime.Minutes = (sTime.Minutes + 1) % 60;
    		   		elementInc = 0;
    		   	}
    		    else if (elementInc == -1){
    		    	sTime.Minutes = (sTime.Minutes == 0) ? 59 : sTime.Minutes - 1;
    		       	elementInc = 0;
    		    }
    		    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    		}
    		else if (editElement == 3){
    			sprintf(timeStr, "%02d", sTime.Seconds);
    			ssd1306_SetCursor(83, 16);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    			if (elementInc == 1){
    		    	sTime.Seconds = (sTime.Seconds + 1) % 60;
    		    	elementInc = 0;
    		    }
    		    else if (elementInc == -1){
    		    	sTime.Seconds = (sTime.Seconds == 0) ? 59 : sTime.Seconds - 1;
    		    	elementInc = 0;
    		    }
    		    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    	    }
    		else if (editElement > 3){
    			editElement = 0;
    			elementInc = 0;
    		}
    		break;
    	}
    	case 2:{
    		if (editElement == 0){
    			sprintf(timeStr, "%02d/%02d/%02d", sDate.Month, sDate.Date, 2000 + sDate.Year);
    			ssd1306_SetCursor(42, 28);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    		}
    		else if (editElement == 1){
    			sprintf(timeStr, "%02d", sDate.Month);
    			ssd1306_SetCursor(42, 28);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    			if (elementInc == 1){
    				sDate.Month = (sDate.Month % 12) + 1;
    				elementInc = 0;
    			}
    			else if (elementInc == -1){
    				sDate.Month = (sDate.Month == 1) ? 12 : sDate.Month - 1;
    				elementInc = 0;
    			}
    			HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    		}
    		else if (editElement == 2){
    			sprintf(timeStr, "%02d", sDate.Year);
    			ssd1306_SetCursor(97, 28);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    			if (elementInc == 1) {
    				sDate.Year = (sDate.Year + 1) % 100;  // RTC allows 00–99 (i.e., 2000–2099)
    				elementInc = 0;
    			}
    			else if (elementInc == -1) {
    				sDate.Year = (sDate.Year == 0) ? 99 : sDate.Year - 1;
    				elementInc = 0;
    			}
    			HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    		}
    		else if (editElement == 3){
    			uint8_t daysInMonth[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    			uint16_t fullYear = 2000 + sDate.Year;
    			if ((sDate.Month == 2) && ((fullYear % 4 == 0 && fullYear % 100 != 0) || (fullYear % 400 == 0))) {
    			    daysInMonth[1] = 29;
    			}
    			sprintf(timeStr, "%02d", sDate.Date);
    			ssd1306_SetCursor(62, 28);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    			if (elementInc == 1) {
    			    sDate.Date++;
    			    if (sDate.Date > daysInMonth[sDate.Month - 1]){
    			        sDate.Date = 1;
    			    }
    			    elementInc = 0;
    			}
    			else if (elementInc == -1) {
    			    if (sDate.Date == 1){
    			        sDate.Date = daysInMonth[sDate.Month - 1];
    			    }
    			    else {
    			        sDate.Date--;
    			    }
    			    elementInc = 0;
    			}
    			HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    		}
    		else if (editElement > 3){
    			editElement = 0;
    			elementInc = 0;
    		}
    		break;
    	}
    	case 3:{
    		if (editElement == 0){
    			sprintf(timeStr, "%s",weekdayStr);
    			ssd1306_SetCursor(63, 40);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    		}
    		else if (editElement == 1){
    			sprintf(timeStr, "%s",weekdayStr);
    			ssd1306_SetCursor(63, 40);
    			ssd1306_WriteString(timeStr, Font_7x10, Black);
    			ssd1306_UpdateScreen();
    			if (elementInc == 1){
    				sDate.WeekDay = (sDate.WeekDay % 7) + 1;
    		    	elementInc = 0;
    		    }
    		    else if (elementInc == -1){
    		    	sDate.WeekDay = (sDate.WeekDay == 1) ? 7 : sDate.WeekDay - 1;
    		    	elementInc = 0;
    		    }
    		    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    		}
    		else if (editElement > 1){
    		    editElement = 0;
    		    elementInc = 0;
    		}
    		break;
    	}
    	case 4:{
    		elementSelect = 3;
    		break;
    	}
    }
}


