/*
 * app.h
 *
 *  Created on: Jun 19, 2022
 *      Author: xl0021
 */

#ifndef SRC_APP_H_
#define SRC_APP_H_

#include "stm32l4xx_hal.h"

extern volatile int button;
extern volatile int buttonCounter;
extern volatile int menuSelect;
extern volatile int elementSelect;
extern volatile int editElement;
extern volatile int flagA;


void App_Init(void);
void App_MainLoop(void);


#endif /* SRC_APP_H_ */
