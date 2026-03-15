#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_wifi.h"

/*************************************
Author: Michael Giardina
Purpose: Turn on the wifi driver for the
		esp32
Inputs: None
Outputs: SuccessStatus (est_err_t)
************************************/
esp_err_t InitWifiDriver()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_defauly_wifi_ap();
	
	//Configure the wifi driver
	wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();

	wifiConfig.osi_funcs = WIFI_IF_STA;
	
	//start the wifi driver
	esp_err_t SuccessStatus = esp_wifi_init(&wifiConfig);
	

	return SuccessStatus;
}


/*************************************
Author: Michael Giardina
Purpose: Main loop
Inputs: None
Outputs: None
************************************/
void app_main(void)
{
	//Flash the rom
	nvs_init_flash();
	
	//init the wifi driver
	InitWifiDriver();
	
	for (;;)
	{

	}

	exit(EXIT_SUCCESS);
}

