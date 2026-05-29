/*
 * hovercar-gd32 - 主控制程序
 * 双电机BLDC控制器主循环
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <string.h>
#include "remote.h"
#include "firmware_api.h"
#include "power.h"
#include "buzzer.h"

// ============================================
// 全局变量
// ============================================

// 车辆控制状态
VehicleControl vehicle = {
    .throttle = 0,
    .steering = 0,
    .enable = RESET,
    .battery_voltage = 0.0f
};

// 控制变量
static uint32_t inactivity_counter = 0;
static FlagStatus remote_timeout = RESET;
static FlagStatus battery_voltage_check_enabled = RESET;
static uint8_t system_state = 0;  // 0=初始化, 1=就绪, 2=运行, 3=错误

// ============================================
// 函数声明
// ============================================
static void system_init(void);
static void control_loop(void);
static void safety_check(void);
static void update_led_indicator(void);
static void startup_ramp_sequence(void);
static void process_remote_input(void);
static void handle_button_input(void);
static void shutdown_procedure(void);
static void debug_output(void);

void BatteryVoltageCheck_Enable(void);
void BatteryVoltageCheck_Disable(void);
FlagStatus BatteryVoltageCheck_IsEnabled(void);

// ============================================
// 主函数
// ============================================

int main(void) {
    // 1. 系统初始化
    system_init();
    
    // 2. 电机自检序列：每秒蜂鸣一次，电机速度分10挡逐步提升
    printf("Startup self-check: motor speed 10 steps, increasing beep length\n");
    startup_ramp_sequence();

    // 3. 等待使能
    printf("Waiting for enable signal...\n");
    while (vehicle.enable == RESET) {
        handle_button_input();
        Delay(10);
        fwdgt_counter_reload();  // 喂狗
    }
    
    // 4. 就绪音
    beep_pattern(2);
    system_state = 1;
    
    printf("System ready, entering main loop\n");
    
    // 5. 主控制循环
    while (1) {
        control_loop();
    }
    
    return 0;
}

// ============================================
// 系统初始化
// ============================================

static void system_init(void) {
    // 1. 系统时钟
    SystemClock_Init();
    
    // 2. SysTick
    SysTick_Init();
    
    // 3. GPIO
    GPIO_Init();
    Buzzer_Init();

    // 3.1 初始化电源自锁并立即上锁（如果硬件支持）
    PowerLatch_Init();
    PowerLatch_Latch();
    
    // 4. PWM定时器
    PWM_Timer_Init();
    
    // 5. ADC
    ADC_Init();
    
    // 6. 看门狗
    Watchdog_Init();
    
    // 7. BLDC驱动
    BLDC_Init();
    
// 8. UART debug initialization
    USART_Init(DEBUG_UART_BAUDRATE);
    
    // 9. 默认关闭电压检测
    BatteryVoltageCheck_Disable();
    
    printf("================================\n");
    printf("hovercar-gd32 dual motor controller\n");
    printf("Version: 1.0.0\n");
    
    printf("PWM frequency: %d Hz\n", PWM_FREQ);
    printf("================================\n");
    
    system_state = 0;
}

void BatteryVoltageCheck_Enable(void) {
    battery_voltage_check_enabled = SET;
}

void BatteryVoltageCheck_Disable(void) {
    battery_voltage_check_enabled = RESET;
}

FlagStatus BatteryVoltageCheck_IsEnabled(void) {
    return battery_voltage_check_enabled;
}

// ============================================
// 控制循环
// ============================================

static void control_loop(void) {
    static uint32_t last_loop_time = 0;
    uint32_t current_time = GetSystemTicks();

    // 非阻塞蜂鸣器更新，每个主循环都需要调用，避免 Delay 阻塞
    Buzzer_Update();
    
    // 控制频率限制 (5ms)
    if (current_time - last_loop_time < DELAY_IN_MAIN_LOOP) {
        return;
    }
    last_loop_time = current_time;
    
    // 1. 安全检测
    safety_check();
    
    // 2. 处理遥控器输入
    process_remote_input();
    
    // 3. 处理按钮输入
    handle_button_input();
    
    // 4. 更新电池电压
    vehicle.battery_voltage = GetBatteryVoltage();
    
    // 5. 混控计算
    int16_t left_pwm = 0, right_pwm = 0;
    Mixer(vehicle.throttle, vehicle.steering, &left_pwm, &right_pwm);
    
    // 6. 设置电机PWM
    if (vehicle.enable == SET && remote_timeout == RESET) {
        SetLeftMotorPWM(left_pwm);
        SetRightMotorPWM(right_pwm);
        SetMotorsEnable(SET);
        system_state = 2;  // 运行状态
    } else {
        SetMotorsEnable(RESET);
        system_state = 1;  // 就绪状态
    }
    
    // 7. 无操作超时检测
    if (ABS(left_pwm) > STAND_STILL_THRESHOLD || 
        ABS(right_pwm) > STAND_STILL_THRESHOLD) {
        inactivity_counter = 0;
    } else {
        inactivity_counter++;
    }
    
    // 8. LED状态指示
    update_led_indicator();
    
    // 9. 调试输出 (可选)
    static uint32_t debug_counter = 0;
    if (debug_counter++ % 200 == 0) {  // 每秒输出一次
        debug_output();
    }
    
    // 10. 喂狗
    fwdgt_counter_reload();
}

// ============================================
// 安全检测
// ============================================

static void safety_check(void) {
    static uint8_t error_count = 0;
    static uint8_t last_error = 0;
    
    uint8_t current_error = 0;
    
    // 1. 电池电压检测（默认可关闭）
    if (BatteryVoltageCheck_IsEnabled()) {
        if (vehicle.battery_voltage < BATTERY_DEAD_VOLTAGE) {
            printf("Error: battery voltage too low %.1fV\n", vehicle.battery_voltage);
            current_error |= 0x01;
            
            // 低压关机
            if (vehicle.battery_voltage < BATTERY_DEAD_VOLTAGE - 2.0f) {
                shutdown_procedure();
            }
        } else if (vehicle.battery_voltage < BATTERY_LOW_VOLTAGE) {
            // 低压警告
            static uint32_t low_bat_blink = 0;
            if (low_bat_blink++ % 100 == 0) {
                beep_pattern(3);  // 低压警告音
            }
        }
    }
    
    // 2. 电流检测
    float current = GetTotalCurrent();
    if (current > CURRENT_LIMIT) {
        printf("Warning: current too high %.1fA\n", current);
        current_error |= 0x02;
    }
    
    // 3. 温度检测 (预留)
    // if (temperature > MAX_TEMPERATURE) {
    //     current_error |= 0x04;
    // }
    
    // 4. 霍尔传感器检测
    // 检查霍尔信号是否有效
    if (error_count > 10) {
        printf("Error: sensor fault\n");
        current_error |= 0x08;
        system_state = 3;  // 错误状态
    }
    
    // 5. 无操作超时
    if (inactivity_counter > (INACTIVITY_TIMEOUT * 60 * 1000 / DELAY_IN_MAIN_LOOP)) {
        printf("Inactivity timeout, shutting down\n");
        shutdown_procedure();
    }
    
    // 6. 遥控器超时
    if (remote_timeout == SET) {
        printf("Remote signal lost\n");
        current_error |= 0x10;
    }
    
    // 错误计数
    if (current_error != 0) {
        if (current_error == last_error) {
            error_count++;
        } else {
            error_count = 1;
        }
    } else {
        error_count = 0;
    }
    
    last_error = current_error;
}

// ============================================
// 遥控器输入处理
// ============================================

static void process_remote_input(void) {
    // 使用遥控器模块处理输入
    Remote_Update();
    
    // 获取遥控器通道值
    int16_t raw_throttle = Remote_GetThrottle();
    int16_t raw_steering = Remote_GetSteering();
    FlagStatus remote_enable = Remote_GetEnable();
    
    // 信号滤波
    static int16_t filtered_throttle = 0;
    static int16_t filtered_steering = 0;
    
    filtered_throttle = filtered_throttle * 0.8f + raw_throttle * 0.2f;
    filtered_steering = filtered_steering * 0.8f + raw_steering * 0.2f;
    
    // 死区处理
    if (ABS(filtered_throttle) < 20) filtered_throttle = 0;
    if (ABS(filtered_steering) < 20) filtered_steering = 0;
    
    // 限制范围
    vehicle.throttle = CLAMP(filtered_throttle, -1000, 1000);
    vehicle.steering = CLAMP(filtered_steering, -1000, 1000);
    
    // 遥控器使能状态
    if (remote_enable == SET) {
        vehicle.enable = SET;
    }
    
    // 遥控器超时检测
    remote_timeout = !Remote_IsConnected();
    
    if (remote_timeout == SET) {
        vehicle.throttle = 0;
        vehicle.steering = 0;
    }
}

// ============================================
// 按钮输入处理
// ============================================

static void handle_button_input(void) {
    // 使能按钮检测
    static uint32_t button_press_time = 0;
    static FlagStatus button_handled = RESET;
    
    // 读取使能引脚 (假设高电平使能)
    FlagStatus enable_state = GPIO_INPUT_BIT_GET(ENABLE_PORT, ENABLE_PIN) ? SET : RESET;
    
    if (enable_state == SET) {
        if (button_handled == RESET) {
            button_press_time = GetSystemTicks();
            button_handled = SET;
        }
        
        // 长按检测 (3秒)
        if (GetSystemTicks() - button_press_time > 3000) {
            // 长按关机
            printf("Long press shutdown\n");
            shutdown_procedure();
        }
    } else {
        if (button_handled == SET) {
            // 按钮释放
            uint32_t press_duration = GetSystemTicks() - button_press_time;
            
            if (press_duration < 1000) {
                // 短按切换使能状态
                vehicle.enable = !vehicle.enable;
                printf("Enable state: %s\n", vehicle.enable ? "ON" : "OFF");
                
                if (vehicle.enable) {
                    beep_pattern(2);  // 使能音
                } else {
                    beep_pattern(1);  // 禁用音
                }
            }
            button_handled = RESET;
        }
    }
}

// ============================================
// LED状态指示
// ============================================

static void update_led_indicator(void) {
    static uint32_t led_counter = 0;
    
    switch (system_state) {
        case 0:  // 初始化
            // 快速闪烁
            if (led_counter % 100 < 50) {
                gpio_bit_set(LED_PORT, LED_PIN);
            } else {
                gpio_bit_reset(LED_PORT, LED_PIN);
            }
            break;
            
        case 1:  // 就绪
            // 慢速闪烁
            if (led_counter % 500 < 250) {
                gpio_bit_set(LED_PORT, LED_PIN);
            } else {
                gpio_bit_reset(LED_PORT, LED_PIN);
            }
            break;
            
        case 2:  // 运行
            // 常亮
            gpio_bit_set(LED_PORT, LED_PIN);
            break;
            
        case 3:  // 错误
            // 快速双闪
            if (led_counter % 200 < 50 || 
                (led_counter % 200 >= 100 && led_counter % 200 < 150)) {
                gpio_bit_set(LED_PORT, LED_PIN);
            } else {
                gpio_bit_reset(LED_PORT, LED_PIN);
            }
            break;
    }
    
    led_counter++;
}

static void startup_ramp_sequence(void) {
    SetMotorsEnable(SET);

    for (uint8_t level = 0; level < 10; level++) {
        int16_t pwm_value = (int16_t)((level + 1) * 100);
        uint16_t beep_time = (uint16_t)((level + 1) * 50);
        if (beep_time > 500) {
            beep_time = 500;
        }

        SetLeftMotorPWM(pwm_value);
        SetRightMotorPWM(pwm_value);

        gpio_bit_set(BUZZER_PORT, BUZZER_PIN);
        Delay(beep_time);
        gpio_bit_reset(BUZZER_PORT, BUZZER_PIN);

        if (beep_time < 1000) {
            Delay(1000 - beep_time);
        }
    }

    SetLeftMotorPWM(0);
    SetRightMotorPWM(0);
    SetMotorsEnable(RESET);
}

// ============================================
// 关机程序
// ============================================

static void shutdown_procedure(void) {
    printf("Executing shutdown procedure\n");
    
    // 1. 禁用电机
    SetMotorsEnable(RESET);
    
    // 2. 关机提示音
    beep_pattern(4);

    timer_disable(TIMER1);
    timer_disable(TIMER3);
    timer_disable(TIMER4);
    
    // 4. 关闭外设
    adc_disable(ADC0);
    
    // 5. LED指示关机
    for (int i = 0; i < 5; i++) {
        gpio_bit_set(LED_PORT, LED_PIN);
        Delay(100);
        gpio_bit_reset(LED_PORT, LED_PIN);
        Delay(100);
    }
    
    printf("System shutdown\n");
    
    // 6. 进入低功耗模式或等待看门狗复位
    // 释放电源自锁，硬件会在松开闩锁后断电
    PowerLatch_Release();

    // 给出短延时，等待硬件掉电。如果硬件没有采取断电，进入低功耗等待
    Delay(100);

    while (1) {
        Delay(1000);
    }
}

// ============================================
// 调试输出
// ============================================

static void debug_output(void) {
    DebugInfo info;
    
    info.system_state = system_state;
    info.battery_voltage = vehicle.battery_voltage;
    info.total_current = GetTotalCurrent();
    info.left_speed = GetLeftMotorSpeed();
    info.left_hall_state = left_motor.hall_state;
    info.right_speed = GetRightMotorSpeed();
    info.right_hall_state = right_motor.hall_state;
    info.throttle = vehicle.throttle;
    info.steering = vehicle.steering;
    
    // 使用串口发送调试信息
    // USART_SendDebugInfo(&info);
    
    printf("SystemCoreClock: %d [%lu Hz]\n", (unsigned int)SystemCoreClock, (unsigned long)SystemCoreClock);
    printf("State:%d Bat:%.1fV Curr:%.1fA Lmotor:%d/%d Rmotor:%d/%d ctrl:T:%d S:%d\n",
           info.system_state,
           info.battery_voltage,
           info.total_current,
           (int)info.left_speed,
           info.left_hall_state,
           (int)info.right_speed,
           info.right_hall_state,
           info.throttle,
           info.steering);
}

// ============================================
// 中断处理函数
// ============================================

// SysTick中断已在setup.c中定义

// 其他中断处理函数可以根据需要添加
// 例如：遥控器PPM中断、串口中断等