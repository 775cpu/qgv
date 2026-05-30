/*
 * hovercar-gd32 - 双电机BLDC驱动
 * 基于霍尔传感器的6步换相控制
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <math.h>
#include "firmware_api.h"

// ============================================
// 全局变量
// ============================================

// 电机状态
MotorState left_motor = {0};
MotorState right_motor = {0};

// ADC缓冲区
adc_buf_t adc_buffer = {0};

// 控制变量
float battery_voltage = BATTERY_CELLS * 3.6f;  // 初始电池电压
float dc_current = 0.0f;                       // DC电流
FlagStatus motors_enabled = RESET;             // 电机使能状态

// 换相表 - 6步换相
const uint8_t hall_to_position[8] = {
    0,  // 000 - 无效
    3,  // 001 - 位置3
    5,  // 010 - 位置5
    4,  // 011 - 位置4
    1,  // 100 - 位置1
    2,  // 101 - 位置2
    6,  // 110 - 位置6
    0   // 111 - 无效
};

// PWM占空比表 (正弦波近似)
const int16_t pwm_pattern[6][3] = {
    // U, V, W
    { 1000,  500, -500},  // 位置1
    {  500, 1000, -500},  // 位置2
    { -500, 1000,  500},  // 位置3
    {-1000,  500,  500},  // 位置4
    { -500, -500, 1000},  // 位置5
    {  500, -500, 1000}   // 位置6
};

// ============================================
// 内部函数声明
// ============================================
static uint8_t read_hall_sensors(uint32_t port_u, uint16_t pin_u,
                                 uint32_t port_v, uint16_t pin_v,
                                 uint32_t port_w, uint16_t pin_w);
static void set_pwm_duty(uint32_t timer, uint8_t channel, int16_t duty);
static void apply_motor_pwm(MotorState* motor, uint32_t timer, 
                           uint8_t ch_u, uint8_t ch_v, uint8_t ch_w);

// ============================================
// 公共函数
// ============================================

/**
 * @brief 初始化BLDC驱动
 */
void BLDC_Init(void) {
    // 初始化电机状态
    left_motor.pwm = 0;
    left_motor.hall_state = 0;
    left_motor.position = 0;
    left_motor.speed = 0.0f;
    left_motor.current = 0.0f;
    
    right_motor.pwm = 0;
    right_motor.hall_state = 0;
    right_motor.position = 0;
    right_motor.speed = 0.0f;
    right_motor.current = 0.0f;
    
    printf("BLDC driver init complete\n");
}

/**
 * @brief 设置左电机PWM
 * @param pwm PWM值 -1000~1000
 */
void SetLeftMotorPWM(int16_t pwm) {
    left_motor.pwm = CLAMP(pwm, MIN_PWM_VALUE, MAX_PWM_VALUE);
}

/**
 * @brief 设置右电机PWM
 * @param pwm PWM值 -1000~1000
 */
void SetRightMotorPWM(int16_t pwm) {
    right_motor.pwm = CLAMP(pwm, MIN_PWM_VALUE, MAX_PWM_VALUE);
}

/**
 * @brief 设置电机使能
 * @param enable 使能状态
 */
void SetMotorsEnable(FlagStatus enable) {
    motors_enabled = enable;
    
    if (enable == RESET) {
        // 禁用时清零PWM
        left_motor.pwm = 0;
        right_motor.pwm = 0;
    }
    
    printf("Motor enable: %s\n", enable ? "ON" : "OFF");
}

/**
 * @brief 读取电池电压
 * @return 电池电压(V)
 */
float GetBatteryVoltage(void) {
    return battery_voltage;
}

/**
 * @brief 读取总电流
 * @return 总电流(A)
 */
float GetTotalCurrent(void) {
    return dc_current;
}

/**
 * @brief 读取左电机速度
 * @return 速度(RPM)
 */
float GetLeftMotorSpeed(void) {
    return left_motor.speed;
}

/**
 * @brief 读取右电机速度
 * @return 速度(RPM)
 */
float GetRightMotorSpeed(void) {
    return right_motor.speed;
}

/**
 * @brief BLDC控制主循环
 * 应在定时器中断中调用 (16kHz)
 */
void BLDC_ControlLoop(void) {
    static uint32_t speed_counter = 0;
    static uint32_t last_hall_change[2] = {0, 0};
    static uint8_t last_hall_state[2] = {0, 0};
    
    // 1. 读取霍尔传感器
    left_motor.hall_state = read_hall_sensors(
        LEFT_HALL_U_PORT, LEFT_HALL_U_PIN,
        LEFT_HALL_V_PORT, LEFT_HALL_V_PIN,
        LEFT_HALL_W_PORT, LEFT_HALL_W_PIN
    );
    
    right_motor.hall_state = read_hall_sensors(
        RIGHT_HALL_U_PORT, RIGHT_HALL_U_PIN,
        RIGHT_HALL_V_PORT, RIGHT_HALL_V_PIN,
        RIGHT_HALL_W_PORT, RIGHT_HALL_W_PIN
    );
    
    // 2. 计算换相位置
    left_motor.position = hall_to_position[left_motor.hall_state];
    right_motor.position = hall_to_position[right_motor.hall_state];
    
    // 3. 计算电机速度 (基于霍尔变化)
    if (left_motor.hall_state != last_hall_state[0] && left_motor.hall_state != 0) {
        uint32_t period = speed_counter - last_hall_change[0];
        if (period > 0) {
            // 每个霍尔变化对应60度电角度，6个变化为一圈
            // 速度 = (60 / 时间) * (1000 / 6) RPM
            left_motor.speed = 10000.0f / period;  // 简化计算
        }
        last_hall_change[0] = speed_counter;
        last_hall_state[0] = left_motor.hall_state;
    }
    
    if (right_motor.hall_state != last_hall_state[1] && right_motor.hall_state != 0) {
        uint32_t period = speed_counter - last_hall_change[1];
        if (period > 0) {
            right_motor.speed = 10000.0f / period;
        }
        last_hall_change[1] = speed_counter;
        last_hall_state[1] = right_motor.hall_state;
    }
    
    // 4. 读取ADC值
    // 电池电压滤波
    battery_voltage = battery_voltage * 0.99f + 
                     ((float)adc_buffer.battery * 0.1f) * 0.01f;  // 假设0.1V/LSB
    
    // DC电流滤波
    dc_current = dc_current * 0.99f + 
                ((float)adc_buffer.current * 0.01f) * 0.01f;  // 假设0.01A/LSB
    
    // 5. 电流保护
    if (dc_current > CURRENT_LIMIT || motors_enabled == RESET) {
        // 过流或禁用时关闭PWM
        left_motor.pwm = 0;
        right_motor.pwm = 0;
        
        // 禁用PWM输出
        TIMER_CARRIER_DISABLE(TIMER0);
        TIMER_CARRIER_DISABLE(TIMER3);
        TIMER_CARRIER_DISABLE(TIMER4);
        
        return;
    }
    
    // 6. 应用PWM到电机
    if (motors_enabled == SET && left_motor.position != 0) {
        // 左电机使用TIM1
        apply_motor_pwm(&left_motor, TIMER1, 
                       TIMER_CH_1, TIMER_CH_2, TIMER_CH_3);
    }
    
    if (motors_enabled == SET && right_motor.position != 0) {
        // 右电机使用TIM3和TIM4
        // U相用TIM3_CH3, V相用TIM4_CH1, W相用TIM4_CH3
        // 这里简化处理，实际需要根据引脚分配调整
        apply_motor_pwm(&right_motor, TIMER3,
                       TIMER_CH_3, TIMER_CH_1, TIMER_CH_3);
    }
    
    speed_counter++;
}

/**
 * @brief 混控函数 - 将油门和转向转换为左右电机PWM
 * @param throttle 油门 -1000~1000
 * @param steering 转向 -1000~1000
 * @param left_pwm 输出左电机PWM
 * @param right_pwm 输出右电机PWM
 */
void Mixer(int16_t throttle, int16_t steering, 
           int16_t* left_pwm, int16_t* right_pwm) {
    // 差速转向混控
    float steer_factor = (float)steering / 1000.0f;
    
    // 基础速度
    int16_t base_speed = throttle;
    
    // 差速计算
    *left_pwm = CLAMP(base_speed * (1.0f + steer_factor), MIN_PWM_VALUE, MAX_PWM_VALUE);
    *right_pwm = CLAMP(base_speed * (1.0f - steer_factor), MIN_PWM_VALUE, MAX_PWM_VALUE);
    
    // 静止区域处理
    if (ABS(throttle) < STAND_STILL_THRESHOLD && ABS(steering) < STAND_STILL_THRESHOLD) {
        *left_pwm = 0;
        *right_pwm = 0;
    }
}

// ============================================
// 内部函数实现
// ============================================

/**
 * @brief 读取霍尔传感器状态
 */
uint8_t read_hall_sensors(uint32_t port_u, uint16_t pin_u,
                          uint32_t port_v, uint16_t pin_v,
                          uint32_t port_w, uint16_t pin_w) {
    uint8_t hall_u = GPIO_INPUT_BIT_GET(port_u, pin_u) ? 0 : 1;  // 低电平有效
    uint8_t hall_v = GPIO_INPUT_BIT_GET(port_v, pin_v) ? 0 : 1;
    uint8_t hall_w = GPIO_INPUT_BIT_GET(port_w, pin_w) ? 0 : 1;
    
    return (hall_u << 0) | (hall_v << 1) | (hall_w << 2);
}

/**
 * @brief 设置PWM占空比
 */
static void set_pwm_duty(uint32_t timer, uint8_t channel, int16_t duty) {
    // 将-1000~1000转换为0~BLDC_TIMER_PERIOD
    int16_t pwm_value = BLDC_TIMER_PERIOD / 2 + (duty * BLDC_TIMER_PERIOD) / 2000;
    
    // 限制在安全范围内
    pwm_value = CLAMP(pwm_value, 10, BLDC_TIMER_PERIOD - 10);
    
    // 设置PWM值
    switch (channel) {
        case TIMER_CH_1:
            TIMER_CH1CV(timer) = pwm_value;
            break;
        case TIMER_CH_2:
            TIMER_CH2CV(timer) = pwm_value;
            break;
        case TIMER_CH_3:
            TIMER_CH3CV(timer) = pwm_value;
            break;
        case 4:
            TIMER_CH3CV(timer) = pwm_value;
            break;
    }
}

/**
 * @brief 应用PWM到电机
 */
static void apply_motor_pwm(MotorState* motor, uint32_t timer, 
                           uint8_t ch_u, uint8_t ch_v, uint8_t ch_w) {
    if (motor->position == 0 || motor->position > 6) {
        // 无效位置，关闭PWM
        set_pwm_duty(timer, ch_u, 0);
        set_pwm_duty(timer, ch_v, 0);
        set_pwm_duty(timer, ch_w, 0);
        return;
    }
    
    // 根据位置和PWM值计算各相占空比
    uint8_t pos_index = motor->position - 1;  // 转换为0-based索引
    float scale = (float)motor->pwm / 1000.0f;
    
    int16_t pwm_u = pwm_pattern[pos_index][0] * scale;
    int16_t pwm_v = pwm_pattern[pos_index][1] * scale;
    int16_t pwm_w = pwm_pattern[pos_index][2] * scale;
    
    // 设置PWM
    set_pwm_duty(timer, ch_u, pwm_u);
    set_pwm_duty(timer, ch_v, pwm_v);
    set_pwm_duty(timer, ch_w, pwm_w);
}