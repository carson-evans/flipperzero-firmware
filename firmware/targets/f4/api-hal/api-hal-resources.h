#pragma once

#include "main.h"
#include <furi.h>

#include <stm32wbxx.h>
#include <stm32wbxx_ll_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_I2C_SCL_Pin LL_GPIO_PIN_9
#define POWER_I2C_SCL_GPIO_Port GPIOA
#define POWER_I2C_SDA_Pin LL_GPIO_PIN_10
#define POWER_I2C_SDA_GPIO_Port GPIOA

#define POWER_I2C I2C1
/* Timing register value is computed with the STM32CubeMX Tool,
  * Fast Mode @100kHz with I2CCLK = 64 MHz,
  * rise time = 0ns, fall time = 0ns
  */
#define POWER_I2C_TIMINGS 0x10707DBC

/* Input Related Constants */
#define INPUT_DEBOUNCE_TICKS 20

/* Input Keys */
typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
} InputKey;

/* Light */
typedef enum {
    LightRed,
    LightGreen,
    LightBlue,
    LightBacklight,
} Light;

typedef struct {
    const GPIO_TypeDef* port;
    const uint16_t pin;
    const InputKey key;
    const bool inverted;
} InputPin;

extern const InputPin input_pins[];
extern const size_t input_pins_count;

extern const GpioPin sd_cs_gpio;
extern const GpioPin vibro_gpio;
extern const GpioPin ibutton_gpio;
extern const GpioPin cc1101_g0_gpio;

// external gpio's
extern const GpioPin ext_pc0_gpio;
extern const GpioPin ext_pc1_gpio;
extern const GpioPin ext_pc3_gpio;
extern const GpioPin ext_pb2_gpio;
extern const GpioPin ext_pb3_gpio;
extern const GpioPin ext_pa4_gpio;
extern const GpioPin ext_pa6_gpio;
extern const GpioPin ext_pa7_gpio;

#ifdef __cplusplus
}
#endif