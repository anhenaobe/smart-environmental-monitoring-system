/* Smart Environmental Monitor: first bring-up milestone for NUCLEO-L432KC.
 * This is NOT a complete product firmware. See README.md for omitted features.
 * Hardware provisional: USART1 PA9(TX), PA10(RX); I2C1 PB6(SCL), PB7(SDA).
 * The STM32 debug port/ST-LINK and Milk-V Linux console are NOT this UART.
 */
#include "stm32l4xx_hal.h"
#include "env_protocol.h"
#include <stdio.h>
#include <string.h>

#define SENSOR_ADDR ((uint16_t)(0x44u << 1))
#define RX_Q_SIZE 128u
/* Provisional for I2C1 kernel clock = 16 MHz; recalculate with CubeMX and measure.
 * Do not treat an unmeasured I2C clock as hardware validation. */
#define I2C1_TIMING_PROVISIONAL 0x30311013u

static UART_HandleTypeDef huart1;
static I2C_HandleTypeDef hi2c1;
static volatile uint8_t rx_q[RX_Q_SIZE];
static volatile uint8_t rx_head, rx_tail, rx_byte, rx_overflow;
static env_rx_t uart_parser;
static uint32_t next_sample_ms;
static uint32_t conversion_started_ms;
static uint32_t last_sys_ms;
static uint32_t serial_seq, i2c_errors;
static uint8_t measurement_pending;
static int sys_state = -1;

void SysTick_Handler(void) { HAL_IncTick(); } /* Required: previous project hung here. */

static void fail_blink(void) {
    for (;;) {HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);HAL_Delay(100);}
}
static void clock_init(void) {
    RCC_OscInitTypeDef osc={0};RCC_ClkInitTypeDef clk={0};
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSI;
    osc.HSIState=RCC_HSI_ON;
    osc.HSICalibrationValue=RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState=RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc)!=HAL_OK) fail_blink();
    clk.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV1;
    clk.APB2CLKDivider=RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_0)!=HAL_OK) fail_blink();
}
static void led_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_3;g.Mode=GPIO_MODE_OUTPUT_PP;
    g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB,&g);
}
void HAL_UART_MspInit(UART_HandleTypeDef *h) {
    if(h->Instance!=USART1) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_9|GPIO_PIN_10;
    g.Mode=GPIO_MODE_AF_PP;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA,&g);
    HAL_NVIC_SetPriority(USART1_IRQn,1,0);HAL_NVIC_EnableIRQ(USART1_IRQn);
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *h) {
    if(h->Instance!=I2C1) return;
    __HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_6|GPIO_PIN_7;
    g.Mode=GPIO_MODE_AF_OD;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB,&g);
}
static void uart_init(void) {
    huart1.Instance=USART1;huart1.Init.BaudRate=115200;
    huart1.Init.WordLength=UART_WORDLENGTH_8B;
    huart1.Init.StopBits=UART_STOPBITS_1;
    huart1.Init.Parity=UART_PARITY_NONE;
    huart1.Init.Mode=UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl=UART_HWCONTROL_NONE;
    huart1.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart1)!=HAL_OK) fail_blink();
    env_rx_init(&uart_parser);
    if(HAL_UART_Receive_IT(&huart1,(uint8_t*)&rx_byte,1)!=HAL_OK) fail_blink();
}
static void i2c_init(void) {
    RCC_PeriphCLKInitTypeDef clk={0};
    clk.PeriphClockSelection=RCC_PERIPHCLK_I2C1;
    clk.I2c1ClockSelection=RCC_I2C1CLKSOURCE_SYSCLK;
    if(HAL_RCCEx_PeriphCLKConfig(&clk)!=HAL_OK) fail_blink();
    hi2c1.Instance=I2C1;
    hi2c1.Init.Timing=I2C1_TIMING_PROVISIONAL;
    hi2c1.Init.OwnAddress1=0;
    hi2c1.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2=0;
    hi2c1.Init.OwnAddress2Masks=I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    if(HAL_I2C_Init(&hi2c1)!=HAL_OK) fail_blink();
    if(HAL_I2CEx_ConfigAnalogFilter(&hi2c1,I2C_ANALOGFILTER_ENABLE)!=HAL_OK) fail_blink();
    if(HAL_I2CEx_ConfigDigitalFilter(&hi2c1,0)!=HAL_OK) fail_blink();
}
void USART1_IRQHandler(void) {HAL_UART_IRQHandler(&huart1);}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    if(h!=&huart1) return;
    uint8_t next=(uint8_t)((rx_head+1u)&(RX_Q_SIZE-1u));
    if(next!=rx_tail) {rx_q[rx_head]=rx_byte;rx_head=next;}
    else rx_overflow++;
    (void)HAL_UART_Receive_IT(&huart1,(uint8_t*)&rx_byte,1);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    if(h==&huart1) (void)HAL_UART_Receive_IT(&huart1,(uint8_t*)&rx_byte,1);
}
static void parse_incoming(void) {
    char payload[ENV_MAX_FRAME+1];
    while(rx_tail!=rx_head) {
        char b=(char)rx_q[rx_tail];rx_tail=(uint8_t)((rx_tail+1u)&(RX_Q_SIZE-1u));
        if(!env_rx_byte(&uart_parser,b,payload,sizeof(payload))) continue;
        if(!strncmp(payload,"SYS,",4) && payload[5]=='\0' && payload[4]>='0' && payload[4]<='3') {
            sys_state=payload[4]-'0';last_sys_ms=HAL_GetTick();
        }
        /* ACK,RST is reserved for later button/reset milestone. */
    }
}
static void tx_payload(const char *payload) {
    char frame[ENV_MAX_FRAME+1];
    size_t n=env_frame(frame,sizeof(frame),payload);
    if(n) (void)HAL_UART_Transmit(&huart1,(uint8_t*)frame,(uint16_t)n,20u);
}
static void report_measurement(uint8_t error,int32_t tc,uint16_t rh) {
    char payload[ENV_MAX_FRAME+1];
    if(error) (void)snprintf(payload,sizeof(payload),"ENV,%08lu,,,%u",(unsigned long)serial_seq,(unsigned)error);
    else (void)snprintf(payload,sizeof(payload),"ENV,%08lu,%+06ld,%05u,0",(unsigned long)serial_seq,(long)tc,(unsigned)rh);
    tx_payload(payload);
    serial_seq=(serial_seq+1u)%100000000u;
}
static void sensor_task(uint32_t now) {
    if(!measurement_pending && (int32_t)(now-next_sample_ms)>=0) {
        uint8_t cmd[2]={0x24u,0x00u}; /* SHT31, single-shot high repeatability, no clock stretching. */
        next_sample_ms=now+1000u;
        if(HAL_I2C_Master_Transmit(&hi2c1,SENSOR_ADDR,cmd,2,10u)!=HAL_OK) {
            i2c_errors++;report_measurement(1,0,0);return;
        }
        conversion_started_ms=HAL_GetTick();measurement_pending=1;
    }
    if(measurement_pending && (uint32_t)(now-conversion_started_ms)>=20u) {
        uint8_t data[6]={0};measurement_pending=0;
        if(HAL_I2C_Master_Receive(&hi2c1,SENSOR_ADDR,data,6,10u)!=HAL_OK) {
            i2c_errors++;report_measurement(1,0,0);return;
        }
        if(env_sht_crc8(data,2)!=data[2] || env_sht_crc8(data+3,2)!=data[5]) {
            report_measurement(2,0,0);return;
        }
        uint16_t rt=(uint16_t)(((uint16_t)data[0]<<8)|data[1]);
        uint16_t rh=(uint16_t)(((uint16_t)data[3]<<8)|data[4]);
        report_measurement(0,env_temp_centi(rt),env_humidity_centi(rh));
    }
}
int main(void) {
    HAL_Init();led_init();clock_init();uart_init();i2c_init();
    next_sample_ms=HAL_GetTick();
    uint32_t next_led_ms=HAL_GetTick();
    for(;;) {
        uint32_t now=HAL_GetTick();
        parse_incoming();sensor_task(now);
        /* Diagnostic heartbeat only. Final 2-LED UI is a separate milestone. */
        uint32_t interval=(sys_state>=0 && (uint32_t)(now-last_sys_ms)<5000u)?500u:150u;
        if((int32_t)(now-next_led_ms)>=0) {
            HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_3);
            next_led_ms=now+interval;
        }
    }
}
