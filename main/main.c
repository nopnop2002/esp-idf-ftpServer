/*
	FTP Server example.

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h" // for MACSTR
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "mdns.h"
#include "esp_sntp.h"

#include "lwip/dns.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define esp_vfs_fat_spiflash_mount esp_vfs_fat_spiflash_mount_rw_wl
#define esp_vfs_fat_spiflash_unmount esp_vfs_fat_spiflash_unmount_rw_wl
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init
#endif

static const char *TAG = "MAIN";
static char *MOUNT_POINT = "/root";

EventGroupHandle_t xEventTask;
int FTP_TASK_FINISH_BIT = BIT2;

#if CONFIG_ST_MODE
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;
#endif

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
#if CONFIG_AP_MODE
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
	}
#endif

#if CONFIG_ST_MODE
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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
#endif
} 

#if CONFIG_AP_MODE
void wifi_init_softap()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	//ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	//ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&wifi_event_handler,
		NULL,
		NULL));

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = CONFIG_ESP_WIFI_AP_SSID,
			.ssid_len = strlen(CONFIG_ESP_WIFI_AP_SSID),
			.password = CONFIG_ESP_WIFI_AP_PASSWORD,
			.max_connection = CONFIG_ESP_MAX_STA_CONN,
			.authmode = WIFI_AUTH_WPA_WPA2_PSK
		},
	};
	if (strlen(CONFIG_ESP_WIFI_AP_PASSWORD) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", CONFIG_ESP_WIFI_AP_SSID, CONFIG_ESP_WIFI_AP_PASSWORD);
}
#endif // CONFIG_AP_MODE

#if CONFIG_ST_MODE

#if CONFIG_STATIC_IP
static esp_err_t example_set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{
	if (addr && (addr != IPADDR_NONE)) {
		esp_netif_dns_info_t dns;
		dns.ip.u_addr.ip4.addr = addr;
		dns.ip.type = IPADDR_TYPE_V4;
		ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
	}
	return ESP_OK;
}
#endif

esp_err_t wifi_init_sta()
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	assert(netif);

#if CONFIG_STATIC_IP

	ESP_LOGI(TAG, "CONFIG_STATIC_IP_ADDRESS=[%s]",CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_GW_ADDRESS=[%s]",CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_NM_ADDRESS=[%s]",CONFIG_STATIC_NM_ADDRESS);

	/* Stop DHCP client */
	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
	ESP_LOGI(TAG, "Stop DHCP Services");

	/* Set STATIC IP Address */
	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0 , sizeof(esp_netif_ip_info_t));
	ip_info.ip.addr = ipaddr_addr(CONFIG_STATIC_IP_ADDRESS);
	ip_info.netmask.addr = ipaddr_addr(CONFIG_STATIC_NM_ADDRESS);
	ip_info.gw.addr = ipaddr_addr(CONFIG_STATIC_GW_ADDRESS);;
	ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));

	/* Set DNS Server */
	ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr("8.8.8.8"), ESP_NETIF_DNS_MAIN));
	ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr("8.8.4.4"), ESP_NETIF_DNS_BACKUP));

#endif // CONFIG_STATIC_IP

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&wifi_event_handler,
		NULL,
		&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&wifi_event_handler,
		NULL,
		&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_ST_SSID,
			.password = CONFIG_ESP_WIFI_ST_PASSWORD,
			/* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
			 * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
			 * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
			 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
			 */
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	esp_err_t ret_value = ESP_OK;
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", CONFIG_ESP_WIFI_ST_SSID, CONFIG_ESP_WIFI_ST_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGE(TAG, "Failed to connect to SSID:%s, password:%s", CONFIG_ESP_WIFI_ST_SSID, CONFIG_ESP_WIFI_ST_PASSWORD);
		ret_value = ESP_FAIL;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_ERR_INVALID_STATE;
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group); 
	ESP_LOGI(TAG, "wifi_init_sta finished.");
	return ret_value; 
}
#endif // CONFIG_ST_MODE

#if CONFIG_FATFS
wl_handle_t mountFATFS(char * partition_label, char * mount_point) {
	ESP_LOGI(TAG, "Initializing FATFS on Builtin SPI Flash Memory");
	// To mount device we need name of device partition, define base_path
	// and allow format partition in case if it is new one and was not formated before
	const esp_vfs_fat_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4, // maximum number of files which can be open at the same time
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};
	wl_handle_t s_wl_handle;
	esp_err_t ret = esp_vfs_fat_spiflash_mount(mount_point, partition_label, &mount_config, &s_wl_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(ret));
		return -1;
	}

	uint64_t total=0, free=0;
	ret = esp_vfs_fat_info(mount_point, &total, &free);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get FATFS partition information (%s)", esp_err_to_name(ret));
		return -1;
	}
	ESP_LOGI(TAG, "Partition size: total: %llu, free: %llu", total, free);
	ESP_LOGI(TAG, "Mount FATFS on %s", mount_point);
	ESP_LOGI(TAG, "s_wl_handle=%"PRIi32, s_wl_handle);
	return s_wl_handle;
}
#endif // CONFIG_FATFS

#if CONFIG_LITTLEFS 
esp_err_t mountLITTLEFS(char * partition_label, char * mount_point) {
	ESP_LOGI(TAG, "Initializing LittleFS on Builtin SPI Flash Memory");

	esp_vfs_littlefs_conf_t conf = {
		.base_path = mount_point,
		.partition_label = partition_label,
		.format_if_mount_failed = true,
		.dont_mount = false,
	};

	// Use settings defined above to initialize and mount LittleFS filesystem.
	// Note: esp_vfs_littlefs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_littlefs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find LittleFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
		}
		return ret;
	}

	size_t total = 0, used = 0;
	ret = esp_littlefs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
		//esp_littlefs_format(conf.partition_label);
		return ret;
	}
	ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
	ESP_LOGI(TAG, "Mount LittleFS on %s", mount_point);
	return ret;
}
#endif // CONFIG_LITTLEFS

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
esp_err_t mountSDCARD(char * mount_point, sdmmc_card_t * card) {
	// Options for mounting the filesystem.
	// If format_if_mount_failed is set to true, SD card will be partitioned and
	// formatted in case when mounting fails.
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = true,
		.max_files = 4, // maximum number of files which can be open at the same time
		.allocation_unit_size = 16 * 1024
	};
	//sdmmc_card_t* card;

#if CONFIG_MMC_SDCARD
	ESP_LOGI(TAG, "Initializing FATFS on MMC SDCARD");
	// Use settings defined above to initialize SD card and mount FAT filesystem.
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();

	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

	// Set bus width to use
#ifdef CONFIG_SDMMC_BUS_WIDTH_4
	ESP_LOGI(TAG, "SDMMC 4 line mode");
	slot_config.width = 4;
#else
	ESP_LOGI(TAG, "SDMMC 1 line mode");
	slot_config.width = 1;
#endif

	// On chips where the GPIOs used for SD card can be configured, set them in
	// the slot_config structure:
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
	ESP_LOGI(TAG, "SOC_SDMMC_USE_GPIO_MATRIX");
	slot_config.clk = CONFIG_SDMMC_CLK; //GPIO_NUM_36;
	slot_config.cmd = CONFIG_SDMMC_CMD; //GPIO_NUM_35;
	slot_config.d0 = CONFIG_SDMMC_D0; //GPIO_NUM_37;
#ifdef CONFIG_SDMMC_BUS_WIDTH_4
	slot_config.d1 = CONFIG_SDMMC_D1; //GPIO_NUM_38;
	slot_config.d2 = CONFIG_SDMMC_D2; //GPIO_NUM_33;
	slot_config.d3 = CONFIG_SDMMC_D3; //GPIO_NUM_34;
#endif // CONFIG_SDMMC_BUS_WIDTH_4
#endif // SOC_SDMMC_USE_GPIO_MATRIX

	// Enable internal pullups on enabled pins. The internal pullups
	// are insufficient however, please make sure 10k external pullups are
	// connected on the bus. This is for debug / example purpose only.
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	// Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
	// Please check its source code and implement error recovery when developing
	// production applications.
	esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

#endif // CONFIG_MMC_SDCARD

#if CONFIG_SPI_SDCARD
	ESP_LOGI(TAG, "Initializing FATFS on SPI SDCARD");
	ESP_LOGI(TAG, "SDSPI_MOSI=%d", CONFIG_SDSPI_MOSI);
	ESP_LOGI(TAG, "SDSPI_MISO=%d", CONFIG_SDSPI_MISO);
	ESP_LOGI(TAG, "SDSPI_CLK=%d", CONFIG_SDSPI_CLK);
	ESP_LOGI(TAG, "SDSPI_CS=%d", CONFIG_SDSPI_CS);
	ESP_LOGI(TAG, "SDSPI_POWER=%d", CONFIG_SDSPI_POWER);

	if (CONFIG_SDSPI_POWER != -1) {
		//gpio_pad_select_gpio(CONFIG_SPI_POWER);
		gpio_reset_pin(CONFIG_SDSPI_POWER);
		/* Set the GPIO as a push/pull output */
		gpio_set_direction(CONFIG_SDSPI_POWER, GPIO_MODE_OUTPUT);
		ESP_LOGI(TAG, "Turning on the peripherals power using GPIO%d", CONFIG_SDSPI_POWER);
		gpio_set_level(CONFIG_SDSPI_POWER, 1);
		vTaskDelay(3000 / portTICK_PERIOD_MS);
	}

	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	spi_bus_config_t bus_cfg = {
		.mosi_io_num = CONFIG_SDSPI_MOSI,
		.miso_io_num = CONFIG_SDSPI_MISO,
		.sclk_io_num = CONFIG_SDSPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4000,
	};
	esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize bus.");
		return ret;
	}
	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = CONFIG_SDSPI_CS;
	slot_config.host_id = host.slot;

	// Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
	// Please check its source code and implement error recovery when developing
	// production applications.
	ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
	ESP_LOGI(TAG, "esp_vfs_fat_sdspi_mount=%d", ret);
#endif // CONFIG_SPI_SDCARD

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem. "
				"If you want the card to be formatted, set format_if_mount_failed = true.");
		} else {
			ESP_LOGE(TAG, "Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		}
#if CONFIG_MMC_SDCARD
		ESP_LOGI(TAG, "Try setting the 1-line SD/MMC mode and lower the SD/MMC card speed.");
#endif // CONFIG_MMC_SDCARD
		return ret;
	}

	// Card has been initialized, print its properties
	sdmmc_card_print_info(stdout, card);
	ESP_LOGI(TAG, "Mounte SD card on %s", mount_point);
	return ret;
}
#endif // CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD

#if CONFIG_ST_MODE
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
#endif // CONFIG_ST_MODE

void ftp_task (void *pvParameters);

void app_main(void)
{
	// Initialize NVS
	esp_err_t ret;
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Initialize WiFi
#if CONFIG_AP_MODE
	ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
	wifi_init_softap();
#endif

#if CONFIG_ST_MODE
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	ESP_ERROR_CHECK(wifi_init_sta());

	// Initialize mDNS
	initialise_mdns();

	// Obtain local IP address
	esp_netif_ip_info_t ip_info;
	ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info));

	// Print the local IP address
	ESP_LOGI(TAG, "IP Address : " IPSTR, IP2STR(&ip_info.ip));
	ESP_LOGI(TAG, "Subnet mask: " IPSTR, IP2STR(&ip_info.netmask));
	ESP_LOGI(TAG, "Gateway	  : " IPSTR, IP2STR(&ip_info.gw));

	// Obtain time over NTP
	ESP_LOGI(TAG, "Getting time over NTP.");
	ESP_ERROR_CHECK(obtain_time());

	// Show current date & time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The local date/time is: %s", strftime_buf);
	ESP_LOGW(TAG, "This server manages file timestamps in GMT.");
#endif // CONFIG_ST_MODE

	// Mount file system
#if CONFIG_FATFS
	char *partition_label = "storage";
	wl_handle_t s_wl_handle = mountFATFS(partition_label, MOUNT_POINT);
	if (s_wl_handle < 0) return;
#endif 

#if CONFIG_LITTLEFS 
	char *partition_label = "storage";
	ret = mountLITTLEFS(partition_label, MOUNT_POINT);
	if (ret != ESP_OK) return;
#endif

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
	sdmmc_card_t card;
	ret = mountSDCARD(MOUNT_POINT, &card);
	if (ret != ESP_OK) return;
#endif 

	// Create FTP server task
	xEventTask = xEventGroupCreate();
	xTaskCreate(ftp_task, "FTP", 1024*6, NULL, 2, NULL);
	xEventGroupWaitBits( xEventTask,
		FTP_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
	ESP_LOGE(TAG, "ftp_task finish");

	// Unmount file system
#if CONFIG_FATFS
	esp_vfs_fat_spiflash_unmount(MOUNT_POINT, s_wl_handle);
	ESP_LOGI(TAG, "FATFS unmounted");
#endif 

#if CONFIG_LITTLEFS
	esp_vfs_littlefs_unregister(partition_label);
	ESP_LOGI(TAG, "LittleFS unmounted");
#endif

#if CONFIG_SPI_SDCARD || CONFIG_MMC_SDCARD
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	ESP_LOGI(TAG, "SDCARD unmounted");
#endif 

}
