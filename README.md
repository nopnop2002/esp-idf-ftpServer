# esp-idf-ftpServer
FTP Server for esp-idf.   
Since it uses the FAT file system instead of SPIFFS, directory operations are possible.   
I found [this](https://www.esp32.com/viewtopic.php?f=13&t=5013#p21738) information.   
So, I ported from [here](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/blob/master/MicroPython_BUILD/components/micropython/esp32/libs/ftp.c).   

# Installation for ESP32
```
git clone https://github.com/nopnop2002/esp-idf-ftpServer
cd esp-idf-ftpServer/
idf.py set-target esp32
idf.py menuconfig
idf.py flash monitor
```

# Installation for ESP32-S2

```
git clone https://github.com/nopnop2002/esp-idf-ftpServer
cd esp-idf-ftpServer/
idf.py set-target esp32s2
idf.py menuconfig
idf.py flash monitor
```
# Configure
You have to set this config value with menuconfig.   
- CONFIG_FILE_SYSTEM   
See below.
- CONFIG_ESP_WIFI_SSID   
SSID of your wifi.
- CONFIG_ESP_WIFI_PASSWORD   
PASSWORD of your wifi.
- CONFIG_ESP_MAXIMUM_RETRY   
Maximum number of retries when connecting to wifi.   
- CONFIG_STATIC_IP   
- CONFIG_STATIC_IP_ADDRESS   
- CONFIG_STATIC_GW_ADDRESS   
Enable Static IP Address.   
- CONFIG_NTP_SERVER   
- CONFIG_LOCAL_TIMEZONE   
Hostname for NTP Server and your timezone.
This server manages file timestamps in GMT.   
- CONFIG_MDNS_HOSTNAME   
MDNS of FTP Server.You can connect with mDNS.local.   
You need to change the mDNS strict mode according to [this](https://github.com/espressif/esp-idf/issues/6190) instruction.   
- CONFIG_FTP_USER   
Username of FTP Server.
- CONFIG_FTP_PASSWORD   
Password of FTP Server.

![config-main](https://user-images.githubusercontent.com/6020549/107847756-a4aa2900-6e31-11eb-9525-6fd82bead5a3.jpg)
![config-app-1](https://user-images.githubusercontent.com/6020549/107847757-a5db5600-6e31-11eb-8ffb-9b5a2b12e0a6.jpg)
![config-app-2](https://user-images.githubusercontent.com/6020549/107847758-a673ec80-6e31-11eb-8620-b81575f833e0.jpg)
![config-app-3](https://user-images.githubusercontent.com/6020549/107847759-a673ec80-6e31-11eb-8fa6-e874cda34a2a.jpg)

# File system   
ESP32 supports the following file systems.   
You can select any one using menuconfig.   
- FAT file system on FLASH   
- FAT file system on SPI peripheral SDCARD   
- FAT file system on SDMMC peripheral SDCARD   

Besides this, the ESP32 supports the SPIFFS filesystem, but I don't use it because it can't handle directories.   

# Using FAT file system on SPI peripheral SDCARD
__Must be formatted with FAT32 before use__

|ESP32 pin|SPI pin|Notes|
|:-:|:-:|:--|
|GPIO14|SCK|10k pull up if can't mount|
|GPIO15|MOSI|10k pull up if can't mount|
|GPIO2|MISO|10k pull up if can't mount|
|GPIO13|CS|10k pull up if can't mount|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

|ESP32-S2 pin|SPI pin|Notes|
|:-:|:-:|:--|
|GPIO14|SCK|10k pull up if can't mount|
|GPIO15|MOSI|10k pull up if can't mount|
|GPIO2|MISO|10k pull up if can't mount|
|GPIO13|CS|10k pull up if can't mount|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

Note: This example doesn't utilize card detect (CD) and write protect (WP) signals from SD card slot.

# Using FAT file system on SDMMC peripheral SDCARD
__Must be formatted with FAT32 before use__

|ESP32 pin|SD card pin|Notes|
|:-:|:-:|:--|
|GPIO14|CLK|10k pullup|
|GPIO15|CMD|10k pullup|
|GPIO2|D0|10k pullup|
|GPIO4|D1|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO12|D2|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO13|D3|not used in 1-line SD mode, but card's D3 pin must have a 10k pullup
|N/C|CD|optional, not used in the example|
|N/C|WP|optional, not used in the example|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

|ESP32-S2 pin|SD card pin|Notes|
|:-:|:-:|:--|
|GPIO14|CLK|10k pullup|
|GPIO15|CMD|10k pullup|
|GPIO2|D0|10k pullup|
|GPIO13|D3|not used in 1-line SD mode, but card's D3 pin must have a 10k pullup
|N/C|CD|optional, not used in the example|
|N/C|WP|optional, not used in the example|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

Note: that ESP32-S2 doesn't include SD Host peripheral and only supports SD over SPI. Therefore only SCK, MOSI, MISO, CS and ground pins need to be connected.

# Note about GPIO2 (ESP32 only)   
GPIO2 pin is used as a bootstrapping pin, and should be low to enter UART download mode.   
One way to do this is to connect GPIO0 and GPIO2 using a jumper, and then the auto-reset circuit on most development boards will pull GPIO2 low along with GPIO0, when entering download mode.

# Note about GPIO12 (ESP32 only)   
GPIO12 is used as a bootstrapping pin to select output voltage of an internal regulator which powers the flash chip (VDD_SDIO).   
This pin has an internal pulldown so if left unconnected it will read low at reset (selecting default 3.3V operation).   
When adding a pullup to this pin for SD card operation, consider the following:
- For boards which don't use the internal regulator (VDD_SDIO) to power the flash, GPIO12 can be pulled high.
- For boards which use 1.8V flash chip, GPIO12 needs to be pulled high at reset. This is fully compatible with SD card operation.
- On boards which use the internal regulator and a 3.3V flash chip, GPIO12 must be low at reset. This is incompatible with SD card operation.
    * In most cases, external pullup can be omitted and an internal pullup can be enabled using a `gpio_pullup_en(GPIO_NUM_12);` call. Most SD cards work fine when an internal pullup on GPIO12 line is enabled. Note that if ESP32 experiences a power-on reset while the SD card is sending data, high level on GPIO12 can be latched into the bootstrapping register, and ESP32 will enter a boot loop until external reset with correct GPIO12 level is applied.
    * Another option is to burn the flash voltage selection efuses. This will permanently select 3.3V output voltage for the internal regulator, and GPIO12 will not be used as a bootstrapping pin. Then it is safe to connect a pullup resistor to GPIO12. This option is suggested for production use.

# Limitation   
- The server does not support multiple connections.   
- The server does not support active connection.
- The server can only process these commands.
   * SYST
   * CDUP
   * CWD
   * PWD
   * XPWD(Same as PWD)
   * SIZE
   * MDTM(Always GMT)
   * TYPE
   * USER
   * PASS
   * PASV
   * LIST
   * RETR
   * STOR
   * DELE
   * RMD
   * MKD
   * RNFR(Rename From)
   * RNTO(Rename To)
   * NOOP
   * QUIT
   * APPE
   * NLST

# Using LilyGo ESP32-S2
The LilyGo ESP32-S2 development board has a micro SD card slot on the board.   
It is connected to the ESP32 by SPI, and the peripheral power is supplied from GPIO14.   
With this, you can easily build an FTP server.   
__No equipment other than the development board is required.__   
It works very stably.   

![LilyGo-esp32-s2-1](https://user-images.githubusercontent.com/6020549/107864770-00f96100-6ea3-11eb-8549-6885ae398111.JPG)

![LilyGo-esp32-s2-2](https://user-images.githubusercontent.com/6020549/107864771-03f45180-6ea3-11eb-8dda-283e7a1407ab.jpg)

# Screen Shot   
![ftp-srver-1](https://user-images.githubusercontent.com/6020549/107865100-9d713280-6ea6-11eb-94d2-4a249bd36a7b.jpg)
![ftp-srver-2](https://user-images.githubusercontent.com/6020549/107848057-fce22a80-6e33-11eb-8c67-471deb16e190.jpg)


# Troubleshooting   
I sometimes get this error when using external SPI SD card readers.   
Requires a PullUp resistor.   
![sd-card-1](https://user-images.githubusercontent.com/6020549/107848058-fe135780-6e33-11eb-9eac-7ce160571276.jpg)
