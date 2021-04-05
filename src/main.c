#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include "hci.h"
#include "uart.h"
#include "periodic.h"
#include "adc.h"
#include "dac.h"
#include "can.h"
#include "led.h"

/* Firmware main, sets up running threads */
int app_main()
{
	/* Initialize NVS */
	esp_err_t ret = nvs_flash_init();

	if(ret == ESP_ERR_NVS_NO_FREE_PAGES ||
	   ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	hci_init();
	adc_init();
	dac_init();
	can_init();
	led_init();

	xTaskCreatePinnedToCore(&hci_thread, "hci", 20000, NULL, 1, NULL, 0);
	xTaskCreatePinnedToCore(&periodic_thread, "periodic", 10000, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&can_rx_thread, "can", 10000, NULL, 4, NULL, 0);

	struct uart_thread_parameters uart_parameters = { .uart = 1 };
	xTaskCreatePinnedToCore(&uart_thread, "uart", 10000, &uart_parameters,
	                        3, NULL, 0);


	while(1)
	{
		esp_task_wdt_reset();
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	return 0;
}
