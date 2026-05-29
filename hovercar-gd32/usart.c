/*
 * hovercar-gd32 - 串口调试模块
 * 用于输出调试信息和接收命令
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "firmware_api.h"
#include "buzzer.h"

// ============================================
// 全局变量
// ============================================

// 串口接收缓冲区
static uint8_t rx_buffer[256];
static uint16_t rx_index = 0;
static FlagStatus rx_ready = RESET;

// 串口发送缓冲区
static uint8_t tx_buffer[256];
static uint16_t tx_index = 0;
static uint16_t tx_length = 0;

static void process_command(const char* command);

// 外部声明（来自 main.c 和 bldc.c）
extern VehicleControl vehicle;
extern uint8_t system_state;
extern MotorState left_motor;
extern MotorState right_motor;
extern adc_buf_t adc_buffer;

// ============================================
// 公共函数
// ============================================
#include <stdint.h>

void USART_Init(uint32_t baudrate)
{
    baudrate=baudrate; // 避免未使用参数的编译警告
    /* 1. 对应 a1: set *(uint32_t*)0x40021018 = *(uint32_t*)0x40021018 | 0x00000008 */
    /* 作用: 使能 GPIOB 时钟 (APB2ENR Bit 3) */
    *(volatile uint32_t*)0x40021018 |= 0x00000008;

    /* 2. 对应 a1: set *(uint32_t*)0x4002101C = *(uint32_t*)0x4002101C | 0x00040000 */
    /* 作用: 使能 USART3 时钟 (APB1ENR Bit 18) */
    *(volatile uint32_t*)0x4002101C |= 0x00040000;

    /* 3. 对应 a1: set *(uint32_t*)0x4000480C = 0x0000 */
    /* 作用: 清除 USART3_CR1，关闭串口准备配置 */
    *(volatile uint32_t*)0x4000480C = 0x0000;

    /* 4. 对应 a1: set *(uint32_t*)0x40010C04 = (*(uint32_t*)0x40010C04 & ~0xFFFF00) | 0x4B00 */
    /* 作用: 配置 GPIOB_CRH。PB10配置为复用推挽输出(0xB)，PB11配置为浮空输入(0x4) */
    *(volatile uint32_t*)0x40010C04 = (*(volatile uint32_t*)0x40010C04 & ~0xFFFF00) | 0x4B00;

    /* 5. 对应 a1: set *(uint32_t*)0x40004808 = 312 */
    /* 作用: 写入波特率寄存器 (BRR)。 */
    /* 注意：a1 中直接写死了 312 (通常对应 36MHz PCLK 下的 115200 波特率)。*/
    /* 为了严格对应，这里默认写入 312。若要让 baudrate 参数生效，可替换为: 
       *(volatile uint32_t*)0x40004808 = (36000000 + baudrate / 2) / baudrate; */
    *(volatile uint32_t*)0x40004808 = 312; 

    /* 6. 对应 a1: set *(uint32_t*)0x4000480C = 0x200C */
    /* 作用: 配置 USART3_CR1，开启 USART(Bit13), TX(Bit3), RX(Bit2) */
    *(volatile uint32_t*)0x4000480C = 0x200C;
}

void USART_Init_old(uint32_t baudrate)
{
    /* 1. 使能 GPIOB、USART2 以及 AFIO 的时钟 */
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_USART2);
    rcu_periph_clock_enable(RCU_AF);         // 重映射需要 AFIO 时钟

    /* 2. 启用 USART2 完全重映射 (TX -> PB10, RX -> PB11) */
    gpio_pin_remap_config(GPIO_USART2_FULL_REMAP, ENABLE);

    /* 3. 配置 TX (PB10) 为复用推挽输出 */
    gpio_init(DEBUG_USART_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, DEBUG_USART_TX_PIN);
    /* 4. 配置 RX (PB11) 为浮空输入 */
    gpio_init(DEBUG_USART_RX_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, DEBUG_USART_RX_PIN);

    /* 5. USART2 复位并配置基本参数 */
    usart_deinit(USART2);
    usart_baudrate_set(USART2, baudrate);
    usart_word_length_set(USART2, USART_WL_8BIT);
    usart_stop_bit_set(USART2, USART_STB_1BIT);
    usart_parity_config(USART2, USART_PM_NONE);
    
    /* 6. 关闭硬件流控制 */
    usart_hardware_flow_rts_config(USART2, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART2, USART_CTS_DISABLE);

    /* 7. 使能发送和接收 */
    usart_transmit_config(USART2, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART2, USART_RECEIVE_ENABLE);

    /* 8. 使能 USART2 */
    usart_enable(USART2);
}

/**
 * @brief 发送字符串
 * @param str 要发送的字符串
 */
void USART_SendString(const char* str) {
    if (str == NULL) {
        return;
    }
    
    uint32_t len = strlen(str);
    for (uint32_t i = 0; i < len; i++) {
        usart_data_transmit(DEBUG_USART, (uint8_t)str[i]);
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param length 数据长度
 */
void USART_SendData(const uint8_t* data, uint16_t length) {
    if (data == NULL || length == 0) {
        return;
    }
    
    for (uint16_t i = 0; i < length; i++) {
        usart_data_transmit(DEBUG_USART, data[i]);
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

/**
 * @brief 检查是否有接收数据
 * @return 接收状态
 */
FlagStatus USART_DataAvailable(void) {
    return rx_ready;
}

/**
 * @brief 读取接收数据
 * @param buffer 输出缓冲区
 * @param max_length 最大长度
 * @return 实际读取长度
 */
uint16_t USART_ReadData(uint8_t* buffer, uint16_t max_length) {
    if (buffer == NULL || max_length == 0 || rx_ready == RESET) {
        return 0;
    }
    
    uint16_t length = (rx_index < max_length) ? rx_index : max_length;
    memcpy(buffer, rx_buffer, length);
    
    // 清空缓冲区
    rx_index = 0;
    rx_ready = RESET;
    
    return length;
}

/**
 * @brief 格式化输出
 * @param format 格式字符串
 * @param ... 参数
 */
void USART_Printf(const char* format, ...) {
    char buffer[128];
    va_list args;
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    USART_SendString(buffer);
}

/**
 * @brief 发送调试信息
 * @param info 调试信息结构
 */
void USART_SendDebugInfo(DebugInfo* info) {
    if (info == NULL) {
        return;
    }
    
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "State:%d Battery:%.1fV Current:%.1fA Left:%d/%d Right:%d/%d Ctrl:T:%d S:%d\n",
             info->system_state,
             info->battery_voltage,
             info->total_current,
             (int)info->left_speed,
             info->left_hall_state,
             (int)info->right_speed,
             info->right_hall_state,
             info->throttle,
             info->steering);
    
    USART_SendString(buffer);
}

// ============================================
// 内部函数
// ============================================

/**
 * @brief 处理接收到的字符
 * @param ch 接收到的字符
 */
static void process_received_char(uint8_t ch) {
    // 简单的命令行处理
    if (ch == '\r' || ch == '\n') {
        if (rx_index > 0) {
            rx_buffer[rx_index] = '\0';  // 添加字符串结束符
            rx_ready = SET;
            
            // 回显接收到的命令
            USART_SendString("\r\n> ");
            USART_SendString((char*)rx_buffer);
            USART_SendString("\r\n");
            
            // 处理命令
            process_command((char*)rx_buffer);
        }
        rx_index = 0;
    } else if (ch == 0x08 || ch == 0x7F) {  // 退格或删除
        if (rx_index > 0) {
            rx_index--;
            USART_SendString("\b \b");  // 回显退格
        }
    } else if (rx_index < sizeof(rx_buffer) - 1) {
        rx_buffer[rx_index++] = ch;
        usart_data_transmit(DEBUG_USART, ch);  // 回显字符
        while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    }
}

// 命令帮助函数
static int16_t parse_pwm_value(const char* str) {
    int16_t value = atoi(str);
    return CLAMP(value, -1000, 1000);
}

/**
 * @brief 处理命令
 * @param command 命令字符串
 */
static void process_command(const char* command) {
    if (command == NULL || strlen(command) == 0) {
        return;
    }
    
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // 转换为小写以便比较
    for (int i = 0; cmd_copy[i]; i++) {
        if (cmd_copy[i] >= 'A' && cmd_copy[i] <= 'Z') {
            cmd_copy[i] += 32;
        }
    }
    
    // 命令处理
    if (strcmp(cmd_copy, "help") == 0 || strcmp(cmd_copy, "?") == 0) {
        USART_SendString("\r\n=== HOVERCAR SERIAL COMMANDS ===\r\n");
        USART_SendString("Motor Control:\r\n");
        USART_SendString("  motor enable              - Enable motors\r\n");
        USART_SendString("  motor disable             - Disable motors\r\n");
        USART_SendString("  motor left <pwm>          - Set left motor PWM (-1000~1000)\r\n");
        USART_SendString("  motor right <pwm>         - Set right motor PWM (-1000~1000)\r\n");
        USART_SendString("  motor both <pwm>          - Set both motors PWM (-1000~1000)\r\n");
        USART_SendString("\r\nSystem Status:\r\n");
        USART_SendString("  status                    - Show system status\r\n");
        USART_SendString("  adc                       - Show ADC values\r\n");
        USART_SendString("  battery                   - Show battery voltage\r\n");
        USART_SendString("  current                   - Show motor current\r\n");
        USART_SendString("  speed                     - Show motor speeds\r\n");
        USART_SendString("  hall                      - Show hall sensor states\r\n");
        USART_SendString("\r\nBuzzer Control:\r\n");
        USART_SendString("  beep <n>                  - Beep n times (1-10)\r\n");
        USART_SendString("  buzzer <pattern>          - Play buzzer pattern\r\n");
        USART_SendString("\r\nSystem:\r\n");
        USART_SendString("  reset                     - Reset system\r\n");
        USART_SendString("  help/?                    - Show this help\r\n");
        USART_SendString("================================\r\n");
    }
    
    else if (strncmp(cmd_copy, "motor ", 6) == 0) {
        const char* motor_cmd = command + 6;
        
        if (strncmp(motor_cmd, "enable", 6) == 0) {
            SetMotorsEnable(SET);
            vehicle.enable = SET;
            USART_SendString("Motors enabled\r\n");
        }
        else if (strncmp(motor_cmd, "disable", 7) == 0) {
            SetMotorsEnable(RESET);
            vehicle.enable = RESET;
            USART_SendString("Motors disabled\r\n");
        }
        else if (strncmp(motor_cmd, "left ", 5) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 5);
            SetLeftMotorPWM(pwm);
            USART_Printf("Left motor PWM: %d\r\n", pwm);
        }
        else if (strncmp(motor_cmd, "right ", 6) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 6);
            SetRightMotorPWM(pwm);
            USART_Printf("Right motor PWM: %d\r\n", pwm);
        }
        else if (strncmp(motor_cmd, "both ", 5) == 0) {
            int16_t pwm = parse_pwm_value(motor_cmd + 5);
            SetLeftMotorPWM(pwm);
            SetRightMotorPWM(pwm);
            USART_Printf("Both motors PWM: %d\r\n", pwm);
        }
        else {
            USART_SendString("Motor command: enable|disable|left|right|both\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "status") == 0) {
        USART_SendString("\r\n=== SYSTEM STATUS ===\r\n");
        USART_Printf("State: %d (0=Init,1=Ready,2=Running,3=Error)\r\n", system_state);
        USART_Printf("Battery: %.2fV\r\n", vehicle.battery_voltage);
        USART_Printf("Current: %.2fA\r\n", GetTotalCurrent());
        USART_Printf("Left Speed: %.0f RPM, Hall: %d\r\n", GetLeftMotorSpeed(), left_motor.hall_state);
        USART_Printf("Right Speed: %.0f RPM, Hall: %d\r\n", GetRightMotorSpeed(), right_motor.hall_state);
        USART_Printf("Throttle: %d, Steering: %d\r\n", vehicle.throttle, vehicle.steering);
        USART_Printf("Motors Enabled: %s\r\n", vehicle.enable ? "YES" : "NO");
        USART_SendString("====================\r\n");
    }
    
    else if (strcmp(cmd_copy, "adc") == 0) {
        USART_SendString("\r\n=== ADC VALUES ===\r\n");
        USART_Printf("Battery ADC: %d (counts)\r\n", adc_buffer.battery);
        USART_Printf("Current ADC: %d (counts)\r\n", adc_buffer.current);
        USART_Printf("Temp ADC: %d (counts)\r\n", adc_buffer.temp);
        USART_SendString("==================\r\n");
    }
    
    else if (strcmp(cmd_copy, "battery") == 0) {
        USART_Printf("Battery Voltage: %.2fV\r\n", vehicle.battery_voltage);
        if (vehicle.battery_voltage < BATTERY_DEAD_VOLTAGE) {
            USART_SendString("WARNING: Battery critically low!\r\n");
        } else if (vehicle.battery_voltage < BATTERY_LOW_VOLTAGE) {
            USART_SendString("WARNING: Battery low!\r\n");
        } else {
            USART_SendString("Battery status: Normal\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "current") == 0) {
        float current = GetTotalCurrent();
        USART_Printf("Motor Current: %.2fA\r\n", current);
        if (current > CURRENT_LIMIT) {
            USART_SendString("WARNING: Current exceeds limit!\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "speed") == 0) {
        USART_Printf("Left Motor Speed: %.0f RPM\r\n", GetLeftMotorSpeed());
        USART_Printf("Right Motor Speed: %.0f RPM\r\n", GetRightMotorSpeed());
    }
    
    else if (strcmp(cmd_copy, "hall") == 0) {
        USART_Printf("Left Hall State: 0x%02X (%d)\r\n", left_motor.hall_state, left_motor.hall_state);
        USART_Printf("Right Hall State: 0x%02X (%d)\r\n", right_motor.hall_state, right_motor.hall_state);
        USART_Printf("Left Motor Position: %d\r\n", left_motor.position);
        USART_Printf("Right Motor Position: %d\r\n", right_motor.position);
    }
    
    else if (strncmp(cmd_copy, "beep", 4) == 0) {
        const char* param = command + 4;
        while (*param == ' ') param++;
        int beeps = atoi(param);
        if (beeps > 0 && beeps <= 10) {
            for (int i = 0; i < beeps; i++) {
                beep_pattern(1);
                Delay(100);
            }
            USART_Printf("Beep x%d\r\n", beeps);
        } else {
            USART_SendString("Beep count: 1-10\r\n");
        }
    }
    
    else if (strncmp(cmd_copy, "buzzer", 6) == 0) {
        const char* param = command + 6;
        while (*param == ' ') param++;
        int pattern = atoi(param);
        if (pattern > 0 && pattern <= 10) {
            beep_pattern(pattern);
            USART_Printf("Buzzer pattern: %d\r\n", pattern);
        } else {
            USART_SendString("Buzzer pattern: 1-10\r\n");
        }
    }
    
    else if (strcmp(cmd_copy, "reset") == 0) {
        USART_SendString("System reset in 1 second...\r\n");
        Delay(1000);
        NVIC_SystemReset();
    }
    
    else {
        USART_Printf("Unknown command: '%s'\r\n", command);
        USART_SendString("Type 'help' for available commands\r\n");
    }
}

// ============================================
// 中断处理函数
// ============================================

/**
 * @brief USART2 中断处理 (调试串口)
 */
void USART2_IRQHandler(void) {
    if (usart_interrupt_flag_get(DEBUG_USART, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t ch = usart_data_receive(DEBUG_USART);
        process_received_char(ch);
        usart_interrupt_flag_clear(DEBUG_USART, USART_INT_FLAG_RBNE);
    }
    
    if (usart_interrupt_flag_get(DEBUG_USART, USART_INT_FLAG_TC) != RESET) {
        usart_interrupt_flag_clear(DEBUG_USART, USART_INT_FLAG_TC);
    }
}

// ============================================
// 重定向printf到串口
// ============================================

#ifdef __GNUC__
// 对于GCC编译器
int _write(int file, char *ptr, int len) {
    (void)file;
    USART_SendData((uint8_t*)ptr, len);
    return len;
}
#else
// 对于其他编译器
int fputc(int ch, FILE *f) {
    (void)f;
    usart_data_transmit(DEBUG_USART, (uint8_t)ch);
    while (usart_flag_get(DEBUG_USART, USART_FLAG_TBE) == RESET);
    return ch;
}
#endif