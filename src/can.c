#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/can.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "can.h"
#include "listener.h"

const int can_tx_pin = GPIO_NUM_0;
const int can_rx_pin = GPIO_NUM_2;

struct
{
	can_timing_config_t timing;
	can_filter_config_t filter;
	can_message_t periodic_messages[8];

	uint8_t flags; /* initialized, rx */
} can_config;

const uint8_t CAN_FLAG_INIT = 1 << 0;
const uint8_t CAN_FLAG_RX_ON = 1 << 1;

static QueueHandle_t can_rx_queue;

struct can_rx_event
{
	uint8_t event;
};

enum
{
	EVENT_CAN_RX_OFF = 0,
	EVENT_CAN_RX_ON = 1
};


int can_init()
{
	esp_err_t err;

	can_general_config_t config =
	{
		.mode = CAN_MODE_NORMAL,
		.tx_io = can_tx_pin,
		.rx_io = can_rx_pin,
		.clkout_io = -1,
		.bus_off_io = -1,
		.tx_queue_len = 10,
		.rx_queue_len = 10,
		.alerts_enabled = CAN_ALERT_NONE,
		.clkout_divider = 0
	};

	/* Setup default timing */
	can_config.timing.brp = 8;
	can_config.timing.tseg_1 = 5;
	can_config.timing.tseg_2 = 4;
	can_config.timing.sjw = 1;
	err = can_driver_install(&config, &can_config.timing, &can_config.filter);

	if(err != ESP_OK)
	{
		printf("ERR CAN init failed!\n");
		return -1;
	}

	err = can_start();
	if(err != ESP_OK)
	{
		printf("ERR CAN init failed!\n");
		return -1;
	}

	return 0;
}

int parse_message_format(can_message_t *msg, const char *fmt)
{
	uint32_t id;
	char remote;

	memset(msg, 0, sizeof(*msg));
	sscanf(fmt, "%x#%c", &id, &remote);

	if(remote == 'R')
	{
		msg->flags |= CAN_MSG_FLAG_RTR;
		msg->identifier = id;
		msg->data_length_code = 0;
	}
	else
	{
		int data[8];
		int ret = sscanf(fmt, "%x#%2x%2x%2x%2x%2x%2x%2x%2x",
			&id, &data[0], &data[1], &data[2], &data[3],
			&data[4], &data[5], &data[6], &data[7]);

		if(ret < 1)
			return -1;

		int len = ret - 1;

		msg->identifier = id;
		msg->data_length_code = len;
		for(int i = 0; i < len; i++)
			msg->data[i] = data[i];
	}

	return 0;
}

void can_command()
{
	char *cmd = strtok(NULL, " ");
	esp_err_t err;

	/* Make sure we have a command */
	if(!cmd)
		goto einval;

	/*
	 * Compare commande against known commands and handle them, otherwise tell
	 * the user it was invalid.
	 */
	if(strcmp(cmd, "help") == 0)
	{
		printf("OK\n");
		printf(
			"Available commands:\n"
			"\n"
			"help - write this text\n"
			"echo - echo all arguments\n"
			"hello - say hello\n");
	}
	else if(strcmp(cmd, "rx") == 0)
	{
		const char *arg = strtok(NULL, " ");

		if(strcmp(cmd, "on") == 0)
			can_rx_on();
		else if(strcmp(cmd, "off") == 0)
			can_rx_off();
		else
			goto einval;
	}
	else if(strcmp(cmd, "send") == 0)
	{
		can_message_t msg;
		const char *msg_str = strtok(NULL, " ");

		memset(&msg, 0, sizeof(msg));
		if(parse_message_format(&msg, msg_str) < 0)
			goto einval;

		err = can_transmit(&msg, 0);
		if(err != ESP_OK)
		{
			printf("ERR Transmit failed!\n");
			return;
		}

		printf("OK\n");
	}
	else if(strcmp(cmd, "config") == 0)
	{
	}
	else if(strcmp(cmd, "status") == 0)
	{
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void can_rx_off()
{
	struct can_rx_event event =
	{
		.event = EVENT_CAN_RX_OFF
	};

	xQueueSendToBack(can_rx_queue, &event, 0);
}

void can_rx_on()
{
	struct can_rx_event event =
	{
		.event = EVENT_CAN_RX_ON
	};

	xQueueSendToBack(can_rx_queue, &event, 0);
}

void can_rx_thread(void *parameters)
{
	/* Check that CAN initialized correctly */
	while(!(can_config.flags & CAN_FLAG_INIT))
		vTaskDelay(100 * portTICK_PERIOD_MS);

	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	can_rx_queue = xQueueCreate(10, sizeof(struct can_rx_event));

	int rx_running = 0;

	while(1)
	{
		can_message_t msg;
		if(can_receive(&msg, 0) == ESP_OK && rx_running)
		{
			printf("CAN frame %x#");
			if(msg.flags & CAN_MSG_FLAG_RTR)
				printf("R");
			else
				for(int i = 0; i < msg.data_length_code; i++)
					printf("%02x", msg.data[i]);

			printf("\n");
		}


		struct can_rx_event event;
		if(xQueueReceive(can_rx_queue, &event, 0))
		{
			if(event.event == EVENT_CAN_RX_OFF)
			{
				rx_running = 0;
				printf("OK\n");
			}

			else if(event.event == EVENT_CAN_RX_ON)
			{
				rx_running = 1;
				printf("OK\n");
			}
		}
	}
}
