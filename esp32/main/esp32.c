/*******************************************
Author: Michael Giardina
Purpose: This program will 
		 1) Connect to a python script running on a laptop
		 2) Get X and Y coordinates from the Python script
		 3) Move a motor to those X and Y coordinates
*******************************************/
//includes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include <limits.h>
#include "config.h"

//constants for the wifi status
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1

//constanst for the wifi group
static EventGroupHandle_t wifiEventLoop;
static uint8_t cntTries = 0;

///////////////////////////////////////////////////////////
// Author: Michael Giardina
// Purpose: To handle the wifi event which starts the wifi driver
///////////////////////////////////////////////////////////
static void wifiHandler(void *arg, esp_event_base_t eventBase, int32_t eventID, void *eventData){
    //function variables
    char *TAG = "WIFIHANDLER";
    const uint8_t MAXTRIES = 10;
    //connect to the access point
    if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_START){
        ESP_LOGI(TAG, "***CONNECTING TO ACCESS POINT***\n");
        esp_wifi_connect();
    }
    else if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_DISCONNECTED){
        if (cntTries < MAXTRIES){
            ++cntTries;
            ESP_LOGI(TAG, "***ATTEMPING TO RECONNECT: ATTEMPT %u OUT OF %u***\n", cntTries, MAXTRIES);
            esp_wifi_connect();
        }
        else{
            xEventGroupSetBits(wifiEventLoop, WIFI_FAILURE);
        }
    }
}

///////////////////////////////////////////////////////////
// Author: Michael Giardina
// Purpose: To handle the ip event 
///////////////////////////////////////////////////////////
static void ipHandler(void *arg, esp_event_base_t eventBase, int32_t eventID, void *eventData){
    //function variables
    char *TAG  = "IPHANDLER";
    ip_event_got_ip_t *event;

    //get IP
    if (eventBase == IP_EVENT && eventID == IP_EVENT_STA_GOT_IP){
        event = (ip_event_got_ip_t*) eventData;
        ESP_LOGI(TAG, "***GOT IP: " IPSTR, IP2STR(&event->ip_info.ip)); 
        cntTries = 0;
        xEventGroupSetBits(wifiEventLoop, WIFI_SUCCESS);
    }
}

///////////////////////////////////////////////////////////
// Author: Michael Giardina
// Purpose: Configure the WIFI and register wifi relates events.
//			The Wifi related events will be in charge of connecting 
//			the ESP32 to the WIFI
///////////////////////////////////////////////////////////
esp_err_t wifiConnect(){
    //method variables
    char *TAG = "WIFICONNECT";
    uint8_t status = WIFI_FAILURE;
    wifi_init_config_t wifiCFG = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t wifiEventHandler;
    esp_event_handler_instance_t ipEventHandler;
    wifi_config_t wifiSettings = {
        .sta = {
            .ssid = SSID,
            .password = PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    EventBits_t wifiBits;
    //initialize wifi driver
    ESP_ERROR_CHECK(esp_netif_init());

    //initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //create station
    esp_netif_create_default_wifi_sta();

    //set station default
    ESP_ERROR_CHECK(esp_wifi_init(&wifiCFG));

    //set up event loop
    wifiEventLoop = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, 
                                                        ESP_EVENT_ANY_ID, 
                                                        &wifiHandler, 
                                                        NULL,
                                                        &wifiEventHandler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ipHandler,
                                                        NULL,
                                                        &ipEventHandler));


    //set the wifi mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    //set wifi config
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiSettings));

    //start driver
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "***WIFI INITIALIZING***\n");

    //block this event until we get success or failure
    wifiBits = xEventGroupWaitBits(wifiEventLoop, 
                                    WIFI_SUCCESS | WIFI_FAILURE,
                                    pdFALSE,
                                    pdFALSE,
                                    portMAX_DELAY);

    //Once we get either success or failure, the proram will stop blocking and this function can check what we got
    if (wifiBits == WIFI_SUCCESS){
        ESP_LOGI(TAG, "***SUCCESSFULLY CONNECTED TO ACCESS POINT***\n");
        status = WIFI_SUCCESS;
    }
    else if (wifiBits == WIFI_FAILURE){
        ESP_LOGE(TAG, "***FAILED TO CONNECT TO ACCESS POINT***\n");
        status = WIFI_FAILURE;
    }
    else{
        ESP_LOGE(TAG, "***UNEXPECTED EVENT***\n");
        status = WIFI_FAILURE;
    }
    
    //unregister the events
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ipEventHandler)); 
    vEventGroupDelete(wifiEventLoop);

    return status;
}

///////////////////////////////////////////////////////////
// Author: Michael Giardina
// Purpose: Connect to a TCP socket and start reading in commands
///////////////////////////////////////////////////////////
int tcpConnect (){
    //function variables
    const char *TAG = "TCPCONNECT";
	int sockFD = 0;
    const int PORT = 8080;
    const int MAX_CONNECTIONS = 10;
    int n = 0;
    char buffer[255] = {' '};
    struct sockaddr_in serverAddr;
    struct sockaddr_in clientAddr;
    socklen_t cliLen;

    //create the socket
    sockFD = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFD < 0){
        ESP_LOGE(TAG, "***COULD NOT OPEN SOCKET***\n");
		return TCP_FAILURE;
    }
    else{
        ESP_LOGI(TAG, "***SOCKET OPENED***\n");
    }
    
    //clear the server address
    memset(&serverAddr, 0, sizeof(serverAddr));

    //configure the socket
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);
     
    
    //bind the port to the address
    if (bind(sockFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0){
        ESP_LOGE(TAG, "***COULD NOT BIND TO PORT***\n");
		return TCP_FAILURE;
    }
    else{
        ESP_LOGI(TAG, "***PORT BINDED***\n");
    }
  
    //listen for incoming connections
    ESP_LOGI(TAG, "***LISTENING...***\n");
    listen(sockFD, MAX_CONNECTIONS);
    cliLen = sizeof(clientAddr);
    
    //accept the connections
    sockFD = accept(sockFD, (struct sockaddr *) &clientAddr, &cliLen);
    if (sockFD < 0){
        ESP_LOGE(TAG, "***COULD NOT ACCEPT THE SOCKET***\n");
		return TCP_FAILURE;
    }
    else{
        printf("***SOCKET ACCEPTED***\n");
    }
   
   
    while(true){
        //clear the buffer to ensure there is no data left over
        memset(&buffer, 0, sizeof(buffer));

        //read data from the client
        n = read(sockFD, buffer, sizeof(buffer));
        if (n < 0){
            ESP_LOGE(TAG, "***COULD NOT READ FROM THE CLIENT***\n");
			break;
        }
        ESP_LOGI(TAG, "CLIENT: %s\n", buffer);
        
        if (strcmp("QUIT\n", buffer) == 0){	
        	ESP_LOGI(TAG, "***We are waiting***\n");
			strcpy(buffer, "WAITING\n");
            n = write(sockFD, buffer, strlen(buffer));	
			if (n < 0){
    	        ESP_LOGE(TAG, "***COULD NOT WRITE TO THE CLIENT***\n");
				break;
    	    }
        }

        vTaskDelay(200/portTICK_PERIOD_MS);
    }
    shutdown(sockFD, 0);
    close(sockFD);
	return TCP_SUCCESS;
}

///////////////////////////////////////////////////////////
// Author: Michael Giardina
// Purpose: Main program loop
///////////////////////////////////////////////////////////
void app_main(void){
    const char *TAG = "MAIN";
	
    //flash the nvs
    esp_err_t status = nvs_flash_init();
    if (status != ESP_OK){
        ESP_LOGE(TAG, "***COULD NOT INITIALIZE NVS***");
        nvs_flash_erase();
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    //connect to the access point
    status = wifiConnect();
    if (status != WIFI_SUCCESS){
        ESP_LOGE(TAG, "***COULD NOT CONNECT TO AP***\n");
        exit(EXIT_FAILURE);
    }

	ESP_LOGI(TAG, "***Succesfully connected***\n");

/*
	do {
    	status = tcpConnect();
	}
    while (status == TCP_SUCCESS);
*/

	if (status == TCP_FAILURE){
		exit(EXIT_FAILURE);
	}
	
    //if we don't set a delay the esp will crash
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}
