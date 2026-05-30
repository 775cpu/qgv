
#ifndef __FIRMWARE_API_H
#define __FIRMWARE_API_H

#include "defines.h"
#include "remote.h"
#include "usart.h"

#ifndef TIMER_CARRIER_DISABLE
#define TIMER_CARRIER_DISABLE(timer) ((void)(timer))
#endif

void SystemClock_Init(void);
void SysTick_Init(void);
void GPIO_Init(void);
void PWM_Timer_Init(void);
void ADC_Init(void);
void Watchdog_Init(void);
void Delay(uint32_t ms);
uint32_t GetSystemTicks(void);
void fwdgt_counter_reload(void);
float GetBatteryVoltage(void);
float GetTotalCurrent(void);
void BatteryVoltageCheck_Enable(void);
void BatteryVoltageCheck_Disable(void);
FlagStatus BatteryVoltageCheck_IsEnabled(void);
void BLDC_Init(void);
void SetLeftMotorPWM(int16_t pwm);
void SetRightMotorPWM(int16_t pwm);
void SetMotorsEnable(FlagStatus enable);
void Mixer(int16_t throttle, int16_t steering, int16_t *left_pwm, int16_t *right_pwm);
float GetLeftMotorSpeed(void);
float GetRightMotorSpeed(void);
uint8_t read_hall_sensors(uint32_t port_u, uint16_t pin_u,
                          uint32_t port_v, uint16_t pin_v,
                          uint32_t port_w, uint16_t pin_w);

#define GPIO_INPUT_BIT_GET(port, pin) gpio_input_bit_get((port), (pin))

extern MotorState left_motor;
extern MotorState right_motor;
extern adc_buf_t adc_buffer;

#endif
