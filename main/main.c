/* FTP Client example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "mdns.h"
#include "esp_sntp.h"

#include "lwip/dns.h"

static const char *TAG = "MAIN";
static char *MOUNT_POINT = "/root";
EventGroupHandle_t xEventTask;

//for test
//#define CONFIG_FATFS	1
//#define CONFIG_SPI_SDCARD  1
//#define CONFIG_MMC_SDCARD  1

/* FreeRTOS event group to signal when we are connected*/
int FTP_TASK_FINISH_BIT = BIT2;
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT	   BIT1

#if CONFIG_SPI_SDCARD
// Pin mapping when using SPI mode.
// With this mapping, SD card can be used both in SPI and 1-line SD mode.
// Note that a pull-up on CS line is required in SD mode.
#define PIN_NUM_MISO	2
#define PIN_NUM_MOSI	15
#define PIN_NUM_CLK		14
#define PIN_NUM_CS		13
#endif 

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
} 

bool parseAddress(int * ip, char * text) {
	ESP_LOGD(TAG, "parseAddress text=[%s]",text);
	int len = strlen(text);
	int octet = 0;
	char buf[4];
	int index = 0;
	for(int i=0;i<len;i++) {
		char c = text[i];
		if (c == '.') {
			ESP_LOGD(TAG, "buf=[%s] octet=%d", buf, octet);
			ip[octet] = strtol(buf, NULL, 10);
			octet++;
			index = 0;
		} else {
			if (index == 3) return false;
			if (c < '0' || c > '9') return false;
			buf[index++] = c;
			buf[index] = 0;
		}
	}

	if (strlen(buf) > 0) {
		ESP_LOGD(TAG, "buf=[%s] octet=%d", buf, octet);
		ip[octet] = strtol(buf, NULL, 10);
		octet++;
	}
	if (octet != 4) return false;
	return true;

}

esp_err_t wifi_init_sta()
{
	esp_err_t ret_value = ESP_OK;
	s_wifi_event_group = xEventGroupCreate();
	ESP_LOGI(TAG,"ESP-IDF Ver%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);

#if ESP_IDF_VERSION_MAJOR >= 4 && ESP_IDF_VERSION_MINOR >= 1
	ESP_LOGI(TAG,"ESP-IDF esp_netif");
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
#else
	ESP_LOGI(TAG,"ESP-IDF tcpip_adapter");
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

#if CONFIG_STATIC_IP

	ESP_LOGI(TAG, "CONFIG_STATIC_IP_ADDRESS=[%s]",CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_GW_ADDRESS=[%s]",CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_NM_ADDRESS=[%s]",CONFIG_STATIC_NM_ADDRESS);

	int ip[4];
	bool ret = parseAddress(ip, CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d ip=%d.%d.%d.%d", ret, ip[0], ip[1], ip[2], ip[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_IP_ADDRESS [%s] not correct", CONFIG_STATIC_IP_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

	int gw[4];
	ret = parseAddress(gw, CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d gw=%d.%d.%d.%d", ret, gw[0], gw[1], gw[2], gw[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_GW_ADDRESS [%s] not correct", CONFIG_STATIC_GW_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

	int nm[4];
	ret = parseAddress(nm, CONFIG_STATIC_NM_ADDRESS);
	ESP_LOGI(TAG, "parseAddress ret=%d nm=%d.%d.%d.%d", ret, nm[0], nm[1], nm[2], nm[3]);
	if (!ret) {
		ESP_LOGE(TAG, "CONFIG_STATIC_NM_ADDRESS [%s] not correct", CONFIG_STATIC_NM_ADDRESS);
	while(1) { vTaskDelay(1); }
	}

#if ESP_IDF_VERSION_MAJOR >= 4 && ESP_IDF_VERSION_MINOR >= 1
	/* Stop DHCP client */
	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
	ESP_LOGI(TAG, "Stop DHCP Services");

	/* Set STATIC IP Address */
	esp_netif_ip_info_t ip_info;
	IP4_ADDR(&ip_info.ip, ip[0], ip[1], ip[2], ip[3]);
	IP4_ADDR(&ip_info.gw, gw[0], gw[1], gw[2], gw[3]);
	IP4_ADDR(&ip_info.netmask, nm[0], nm[1], nm[2], nm[3]);
	//tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
	esp_netif_set_ip_info(netif, &ip_info);

#else
	/* Stop DHCP client */
	tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);

	/* Set STATIC IP Address */
	tcpip_adapter_ip_info_t ipInfo;
	IP4_ADDR(&ipInfo.ip, ip[0], ip[1], ip[2], ip[3]);
	IP4_ADDR(&ipInfo.gw, gw[0], gw[1], gw[2], gw[3]);
	IP4_ADDR(&ipInfo.netmask, nm[0], nm[1], nm[2], nm[3]);
	tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
#endif

	/*
	I referred from here.
	https://www.esp32.com/viewtopic.php?t=5380
	if we should not be using DHCP (for example we are using static IP addresses),
	then we need to instruct the ESP32 of the locations of the DNS servers manually.
	Google publicly makes available two name servers with the addresses of 8.8.8.8 and 8.8.4.4.
	*/

	ip_addr_t d;
	d.type = IPADDR_TYPE_V4;
	d.u_addr.ip4.addr = 0x08080808; //8.8.8.8 dns
	dns_setserver(0, &d);
	d.u_addr.ip4.addr = 0x08080404; //8.8.4.4 dns
	dns_setserver(1, &d);

#endif

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,			// xClearOnExit
		pdFALSE,			// xWaitForAllBits
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGE(TAG, "Failed to connect to SSID:%s, password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret_value = ESP_FAIL;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
	vEventGroupDelete(s_wifi_event_group); 
	return ret_value; 
}

#if CONFIG_FATFS
wl_handle_t mountFATFS(char * partition_label, char * mount_point) {
	ESP_LOGI(TAG, "Initializing FAT file system");
	// To mount device we need name of device partition, define base_path
	// and allow format partition in case if it is new one and was not formated before
	const esp_vfs_fat_mount_config_t mount_config = {
		.max_files = 4,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};
	wl_handle_t s_wl_handle;
	esp_err_t err = esp_vfs_fat_spiflash_mount(mount_point, partition_label, &mount_config, &s_wl_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
		return -1;
	}
	ESP_LOGI(TAG, "Mount FAT filesystem on %s", mount_point);
	ESP_LOGI(TAG, "s_wl_handle=%d",s_wl_handle);
	return s_wl_handle;
}
#endif


#ifdef CONFIG_IDF_TARGET_ESP32S2
// on ESP32-S2, DMA channel must be the same as host id
#define SPI_DMA_CHAN	host.slot
#else
// on ESP32, DMA channel to be used by the SPI peripheral
#define SPI_DMA_CHAN	1
#endif

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
esp_err_t mountSDCARD(char * mount_point, sdmmc_card_t * card) {
	esp_err_t ret;
	// Options for mounting the filesystem.
	// If format_if_mount_failed is set to true, SD card will be partitioned and
	// formatted in case when mounting fails.
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};
	//sdmmc_card_t* card;

#if CONFIG_MMC_SDCARD
	ESP_LOGI(TAG, "Initializing SDMMC peripheral");
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();

	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

	// To use 1-line SD mode, uncomment the following line:
	// slot_config.width = 1;

	// GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
	// Internal pull-ups are not sufficient. However, enabling internal pull-ups
	// does make a difference some boards, so we do that here.
	gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);	// CMD, needed in 4- and 1- line modes
	gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);	// D0, needed in 4- and 1-line modes
	gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);	// D1, needed in 4-line mode only
	gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);	// D2, needed in 4-line mode only
	gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);	// D3, needed in 4- and 1-line modes

	ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
#else
	ESP_LOGI(TAG, "Initializing SPI peripheral");
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	spi_bus_config_t bus_cfg = {
		.mosi_io_num = PIN_NUM_MOSI,
		.miso_io_num = PIN_NUM_MISO,
		.sclk_io_num = PIN_NUM_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4000,
	};
	ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize bus.");
		return ret;
	}
	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = PIN_NUM_CS;
	slot_config.host_id = host.slot;

	ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
#endif

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem. "
				"If you want the card to be formatted, set format_if_mount_failed = true.");
		} else {
			ESP_LOGE(TAG, "Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		}
		return ret;
	}

	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);
	ESP_LOGI(TAG, "Mounte SD card on %s", mount_point);
	return ret;
}
#endif

void initialise_mdns(void)
{
	//initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	//set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_MDNS_HOSTNAME) );
	ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

#if 0
	//set default mDNS instance name
	ESP_ERROR_CHECK( mdns_instance_name_set("ESP32 with mDNS") );
#endif
}

void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
	ESP_LOGI(TAG, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	//sntp_setservername(0, "pool.ntp.org");
	ESP_LOGI(TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
	sntp_setservername(0, CONFIG_NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();
}

static esp_err_t obtain_time(void)
{
	initialize_sntp();
	// wait for time to be set
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

	if (retry == retry_count) return ESP_FAIL;
	return ESP_OK;
}

void ftp_task (void *pvParameters);

void app_main(void)
{
	//Initialize NVS
	esp_err_t ret;
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	if (wifi_init_sta() != ESP_OK) {
		ESP_LOGE(TAG, "Connection failed");
		while(1) { vTaskDelay(1); }
	}
	initialise_mdns();
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));

	/* Print the local IP address */
	ESP_LOGI(TAG, "IP Address : %s", ip4addr_ntoa(&ip_info.ip));
	ESP_LOGI(TAG, "Subnet mask: %s", ip4addr_ntoa(&ip_info.netmask));
	ESP_LOGI(TAG, "Gateway	  : %s", ip4addr_ntoa(&ip_info.gw));

	// obtain time over NTP
	ESP_LOGI(TAG, "Getting time over NTP.");
	ret = obtain_time();
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "Fail to getting time over NTP.");
		return;
	}

	// update 'now' variable with current time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The local date/time is: %s", strftime_buf);
	ESP_LOGW(TAG, "This server manages file timestamps in GMT.");

#if CONFIG_FATFS
	char *partition_label = "storage";
	wl_handle_t s_wl_handle = mountFATFS(partition_label, MOUNT_POINT);
	if (s_wl_handle < 0) return;
#endif 

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
	sdmmc_card_t card;
	ret = mountSDCARD(MOUNT_POINT, &card);
	if (ret != ESP_OK) return;
#endif 

	xEventTask = xEventGroupCreate();
	xTaskCreate(ftp_task, "FTP", 1024*6, NULL, 2, NULL);
	while(1) {
		xEventGroupWaitBits( xEventTask,
			FTP_TASK_FINISH_BIT,			/* The bits within the event group to wait for. */
			pdTRUE,		   /* BIT_0 should be cleared before returning. */
			pdFALSE,	   /* Don't wait for both bits, either bit will do. */
			portMAX_DELAY);/* Wait forever. */
		ESP_LOGE(TAG, "ftp_task finish");
		break;
	}

#if CONFIG_FATFS
	esp_vfs_fat_spiflash_unmount(MOUNT_POINT, s_wl_handle);
	ESP_LOGI(TAG, "FATFS unmounted");
#endif 

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	ESP_LOGI(TAG, "SDCARD unmounted");
#endif 

}
