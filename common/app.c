// Copyright (c) 2019 Cesanta Software Limited
// All rights reserved
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdint.h>

#include "app.h"
#include "mjson.h"

#define DEFAULT_BLINK_PERIOD_MS 100
#define DEFAULT_REPORT_PERIOD_MS 2500

static UART_HandleTypeDef *s_huart;
static GPIO_TypeDef *s_led1_port, *s_led2_port;
static uint16_t s_led1_pin, s_led2_pin;
const char *s_app_name, *s_app_version, *s_mqtt_topic;

static uint32_t s_blink_period_ms = DEFAULT_BLINK_PERIOD_MS;
static uint32_t s_next_blink = 0;

static uint32_t s_report_period_ms = DEFAULT_REPORT_PERIOD_MS;
static uint32_t s_next_report = 0;

int uart_sender(const char *buf, int len, void *privdata) {
	HAL_UART_Transmit(s_huart, (uint8_t *) buf, len, 1000);
  return len;
}

void shadow_delta_handler(struct jsonrpc_request *r) {
  int bpms = mjson_get_number(r->params, r->params_len, "$.app.blink_period_ms", -1);
  if (bpms >= 0) {
    s_blink_period_ms = bpms;
    s_next_blink = 0;
  }
}

void led_set_handler(struct jsonrpc_request *r) {
	int led_on = mjson_get_bool(r->params, r->params_len, "$.on", 0);
	HAL_GPIO_WritePin(s_led2_port, s_led2_pin, (led_on ? GPIO_PIN_SET : GPIO_PIN_RESET));
	jsonrpc_return_success(r, NULL);
}

void led_toggle_handler(struct jsonrpc_request *r) {
	HAL_GPIO_TogglePin(s_led2_port, s_led2_pin);
  jsonrpc_return_success(r, NULL);
}

void do_blink() {
  if (s_blink_period_ms <= 0) {
		HAL_GPIO_WritePin(s_led1_port, s_led1_pin, GPIO_PIN_RESET);
    return;
  }
  uint32_t now = HAL_GetTick();
  if (now >= s_next_blink) {
		HAL_GPIO_TogglePin(s_led1_port, s_led1_pin);
    s_next_blink = now + s_blink_period_ms * HAL_GetTickFreq();
  }
}

void do_report() {
  uint32_t now = HAL_GetTick();
  if (s_report_period_ms <= 0 || now < s_next_report) {
    return;
  }
  static int s_report_shadow = 1;
  if (s_report_shadow) {
    jsonrpc_call(
      "{\"method\": \"Shadow.Report\", \"params\": "
        "{\"app\": {\"name\": %Q, \"version\": %Q, \"uptime_ms\": %u, \"blink_period_ms\": %u}}",
      s_app_name, s_app_version, now, s_blink_period_ms);
  } else if (s_mqtt_topic != NULL) {
    jsonrpc_call(
      "{\"method\": \"MQTT.Pub\", \"params\": "
        "{\"topic\": %Q, \"qos\": 0, "
        "\"message\": {\"name\": %Q, \"version\": %Q, \"uptime_ms\": %u, \"blink_period_ms\": %u}}}",
      s_mqtt_topic, s_app_name, s_app_version, now, s_blink_period_ms);
  }
  s_report_shadow = !s_report_shadow;
  s_next_report = now + s_report_period_ms * HAL_GetTickFreq();
}

void init_led(GPIO_TypeDef *port, uint16_t pin) {
  if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
  if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
  if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
  if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
#ifdef GPIOE
  if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
#ifdef GPIOF
  if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
#endif
  
	GPIO_InitTypeDef Init_LED;
	Init_LED.Pin = pin;
  Init_LED.Mode = GPIO_MODE_OUTPUT_PP;
  Init_LED.Pull = GPIO_NOPULL;
  Init_LED.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &Init_LED);
}

void app_init(
    UART_HandleTypeDef *huart, const char *app_name, const char *app_version,
    GPIO_TypeDef *led1_port, uint16_t led1_pin, GPIO_TypeDef *led2_port, uint16_t led2_pin,
    const char * mqtt_topic) {
  s_huart = huart;
  s_led1_port = led1_port;
  s_led1_pin = led1_pin;
  s_led2_port = led2_port;
  s_led2_pin = led2_pin;
  s_app_name = app_name;
  s_app_version = app_version;
  s_mqtt_topic = mqtt_topic;
 
	jsonrpc_init(uart_sender, NULL /* response_cb */, NULL, s_app_version);

	jsonrpc_export("Shadow.Delta", shadow_delta_handler, NULL);
	jsonrpc_export("LED.Set", led_set_handler, NULL);
	jsonrpc_export("LED.Toggle", led_toggle_handler, NULL);
	
  init_led(led1_port, led1_pin);
  init_led(led2_port, led2_pin);

  HAL_GPIO_WritePin(led1_port, led1_pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(led2_port, led2_pin, GPIO_PIN_RESET);
}

void app_run(void) {
  uint8_t byte;
	if (HAL_UART_Receive(s_huart, &byte, 1, 1) == HAL_OK) {
    jsonrpc_process_byte(byte);
  }
  do_blink();
  do_report();
}
