/*
 * hovercar-gd32 - 串口调试模块
 * 用于输出调试信息和接收命令
 */

#include "defines.h"
#include "gd32f10x.h"
#include <stdio.h>
#include <string.h>
#include "firmware_api.h"

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

// ============================================
// 公共函数
// ============================================
void USART_Init(uint32_t baudrate)
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

/**
 * @brief 处理命令
 * @param command 命令字符串
 */
static void process_command(const char* command) {
    if (command == NULL || strlen(command) == 0) {
        return;
    }
    
    // 简单的命令解析
    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        USART_SendString("Available commands:\r\n");
        USART_SendString("  help/?      - show help\r\n");
        USART_SendString("  status      - show system status\r\n");
        USART_SendString("  enable      - enable motor\r\n");
        USART_SendString("  disable     - disable motor\r\n");
        USART_SendString("  pwm <value> - set PWM value (-1000~1000)\r\n");
        USART_SendString("  remote      - show remote status\r\n");
        USART_SendString("  reset       - reset system\r\n");
    } else if (strcmp(command, "status") == 0) {
        USART_SendString("System status command\r\n");
    } else if (strcmp(command, "enable") == 0) {
        USART_SendString("Motor enabled\r\n");
        // 这里可以调用使能函数
    } else if (strcmp(command, "disable") == 0) {
        USART_SendString("Motor disabled\r\n");
        // 这里可以调用禁用函数
    } else if (strncmp(command, "pwm ", 4) == 0) {
        int16_t pwm_value = atoi(command + 4);
        USART_Printf("Set PWM value: %d\r\n", pwm_value);
        // 这里可以调用设置PWM函数
    } else if (strcmp(command, "remote") == 0) {
        USART_SendString("Remote status command\r\n");
    } else if (strcmp(command, "reset") == 0) {
        USART_SendString("System reset...\r\n");
        NVIC_SystemReset();
    } else {
        USART_Printf("Unknown command: %s\r\n", command);
        USART_SendString("Type 'help' for commands\r\n");
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