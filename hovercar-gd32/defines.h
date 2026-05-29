/*
 * hovercar-gd32 - 双电机BLDC控制器
 * 针对 GD32F103RCT6 MCU
 * 一个主板驱动两路电机，引脚可配置
 */

#ifndef __DEFINES_H
#define __DEFINES_H

#include <stdint.h>
#include "gd32f10x.h"

// ============================================
// 系统配置
// ============================================
#define SYSTEM_CORE_CLOCK    72000000    // 系统时钟频率 72MHz
#define PWM_FREQ             16000       // PWM频率 16kHz
#define BLDC_TIMER_PERIOD    (SYSTEM_CORE_CLOCK / 2 / PWM_FREQ)  // 2250

// ============================================
// 电机引脚配置 - 可修改
// ============================================

// 左电机PWM引脚
#define LEFT_MOTOR_UH_PIN    GPIO_PIN_6   // PC6 - TIM8_CH1
#define LEFT_MOTOR_UH_PORT   GPIOC
#define LEFT_MOTOR_UL_PIN    GPIO_PIN_7   // PA7 - TIM8_CH1N
#define LEFT_MOTOR_UL_PORT   GPIOA

#define LEFT_MOTOR_VH_PIN    GPIO_PIN_7   // PC7 - TIM8_CH2
#define LEFT_MOTOR_VH_PORT   GPIOC
#define LEFT_MOTOR_VL_PIN    GPIO_PIN_0   // PB0 - TIM8_CH2N
#define LEFT_MOTOR_VL_PORT   GPIOB

#define LEFT_MOTOR_WH_PIN    GPIO_PIN_8   // PC8 - TIM8_CH3
#define LEFT_MOTOR_WH_PORT   GPIOC
#define LEFT_MOTOR_WL_PIN    GPIO_PIN_1   // PB1 - TIM8_CH3N
#define LEFT_MOTOR_WL_PORT   GPIOB

// 右电机PWM引脚
#define RIGHT_MOTOR_UH_PIN   GPIO_PIN_8   // PA8 - TIM1_CH1
#define RIGHT_MOTOR_UH_PORT  GPIOA
#define RIGHT_MOTOR_UL_PIN   GPIO_PIN_13  // PB13 - TIM1_CH1N
#define RIGHT_MOTOR_UL_PORT  GPIOB

#define RIGHT_MOTOR_VH_PIN   GPIO_PIN_9   // PA9 - TIM1_CH2
#define RIGHT_MOTOR_VH_PORT  GPIOA
#define RIGHT_MOTOR_VL_PIN   GPIO_PIN_14  // PB14 - TIM1_CH2N
#define RIGHT_MOTOR_VL_PORT  GPIOB

#define RIGHT_MOTOR_WH_PIN   GPIO_PIN_10  // PA10 - TIM1_CH3
#define RIGHT_MOTOR_WH_PORT  GPIOA
#define RIGHT_MOTOR_WL_PIN   GPIO_PIN_15  // PB15 - TIM1_CH3N
#define RIGHT_MOTOR_WL_PORT  GPIOB

// 霍尔传感器引脚
#define LEFT_HALL_U_PIN      GPIO_PIN_5   // PB5
#define LEFT_HALL_U_PORT     GPIOB
#define LEFT_HALL_V_PIN      GPIO_PIN_6   // PB6
#define LEFT_HALL_V_PORT     GPIOB
#define LEFT_HALL_W_PIN      GPIO_PIN_7   // PB7
#define LEFT_HALL_W_PORT     GPIOB

#define RIGHT_HALL_U_PIN     GPIO_PIN_10  // PC10
#define RIGHT_HALL_U_PORT    GPIOC
#define RIGHT_HALL_V_PIN     GPIO_PIN_11  // PC11
#define RIGHT_HALL_V_PORT    GPIOC
#define RIGHT_HALL_W_PIN     GPIO_PIN_12  // PC12
#define RIGHT_HALL_W_PORT    GPIOC

// 控制引脚
#define ENABLE_PIN           GPIO_PIN_13  // PC13 - 使能引脚
#define ENABLE_PORT          GPIOC
#define BUZZER_PIN           GPIO_PIN_4   // PA4 - 蜂鸣器
#define BUZZER_PORT          GPIOA
#define LED_PIN              GPIO_PIN_2   // PB2 - 状态LED
#define LED_PORT             GPIOB

// ADC引脚
#define BATTERY_ADC_PIN      GPIO_PIN_2   // PC2 - ADC1_CH12 - 电池电压
#define BATTERY_ADC_PORT     GPIOC
#define CURRENT_ADC_PIN      GPIO_PIN_1   // PA1 - ADC1_CH1 - 电流检测
#define CURRENT_ADC_PORT     GPIOA

// 左侧模拟输入 (PA2/PA3) 用于油门/模拟控制
#define THROTTLE_ADC_PORT    GPIOA
#define THROTTLE_ADC_PIN_1   GPIO_PIN_2   // PA2 - ADC1_CH2
#define THROTTLE_ADC_PIN_2   GPIO_PIN_3   // PA3 - ADC1_CH3

// USART2，重映射到 PB10/PB11）   GD32 的 USART2 = 0x40004800 而 STM32 同地址是 USART3，原因 hovercar-gd32/Read=uart.md
#define DEBUG_USART          USART2
#define DEBUG_USART_CLK      RCU_USART2
#define DEBUG_USART_IRQn     USART2_IRQn
#define DEBUG_USART_TX_PORT  GPIOB
#define DEBUG_USART_TX_PIN   GPIO_PIN_10  // PB10 - USART2_TX (full remap)
#define DEBUG_USART_RX_PORT  GPIOB
#define DEBUG_USART_RX_PIN   GPIO_PIN_11  // PB11 - USART2_RX (full remap)

// Debug UART baud rate (USART2 on PB10/PB11)
#define DEBUG_UART_BAUDRATE 115200

// 电源自锁（Power Latch）引脚 - 典型板子使用 PA11
#define PWR_LATCH_PORT       GPIOA
#define PWR_LATCH_PIN        GPIO_PIN_11  // PA11 - 电源自锁/闩锁引脚

// ============================================
// 电机参数
// ============================================
#define DEAD_TIME            50           // 死区时间 ns
#define CURRENT_LIMIT        20.0f        // 电流限制 20A
#define BATTERY_CELLS        10           // 电池节数 (10S = 37V)
#define BATTERY_LOW_VOLTAGE  2.0f        // 低电压保护 30V
#define BATTERY_DEAD_VOLTAGE 1.0f        // 关机电压 28V

// ============================================
// 控制参数
// ============================================
#define MAX_PWM_VALUE        1000         // 最大PWM值
#define MIN_PWM_VALUE        -1000        // 最小PWM值
#define STAND_STILL_THRESHOLD 50          // 静止阈值
#define SPEED_COEFFICIENT    1.0f         // 速度系数
#define STEER_COEFFICIENT    0.7f         // 转向系数

// ============================================
// 时间参数
// ============================================
#define DELAY_IN_MAIN_LOOP   5            // 主循环延时 ms
#define INACTIVITY_TIMEOUT   10           // 无操作超时分钟
#define REMOTE_TIMEOUT_MS    1000         // 遥控器超时 ms

// 遥控器模式
typedef enum {
    REMOTE_MODE_NONE = 0,
    REMOTE_MODE_PPM,
    REMOTE_MODE_PWM,
    REMOTE_MODE_ADC,
    REMOTE_MODE_CRSF,
    REMOTE_MODE_SBUS
} RemoteMode;

// ADC缓冲区结构
typedef struct {
    uint16_t battery;      // 电池电压
    uint16_t current;      // 电流检测
    uint16_t temp;         // 温度
} adc_buf_t;

// 电机状态
typedef struct {
    int16_t pwm;           // PWM值 -1000~1000
    uint8_t hall_state;    // 霍尔状态
    uint8_t position;      // 换相位置
    float speed;           // 速度 RPM
    float current;         // 电流 A
} MotorState;

// 车辆控制
typedef struct {
    int16_t throttle;      // 油门 -1000~1000
    int16_t steering;      // 转向 -1000~1000
    FlagStatus enable;     // 使能状态
    float battery_voltage; // 电池电压
} VehicleControl;

// ============================================
// 宏定义
// ============================================
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define MAP(x, in_min, in_max, out_min, out_max) \
    (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min))

#endif /* __DEFINES_H */