# esp-idf-ftpServer
FTP Server for esp-idf using FATFS/LittleFS.   
I found [this](https://www.esp32.com/viewtopic.php?f=13&t=5013#p21738) information.   
So, I ported from [here](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/blob/master/MicroPython_BUILD/components/micropython/esp32/libs/ftp.c).   
Directory operations are possible because FATFS/LittlsFS is used.   

# Software requirements
ESP-IDF V5.0 or later.   
ESP-IDF V4.4 release branch reached EOL in July 2024.   
ESP-IDF V5.1 is required when using ESP32-C6.   

# Installation
```
git clone https://github.com/nopnop2002/esp-idf-ftpServer
cd esp-idf-ftpServer/
idf.py set-target {esp32/esp32s2/esp32s3/esp32c2/esp32c3/esp32c6}
idf.py menuconfig
idf.py flash monitor
```

# Partition table for Builtin SPI Flash Memory
If you select FATFS on the built-in SPI flash memory, this project will use partitions_example_fatfs.csv.   
If you select LittleFS on the built-in SPI flash memory, this project will use partitions_example_littlefs.csv.
If your board has 4M SPI Flash Memory, you can get more space by changing this.   
The maximum partition size that can be specified for the 4M flash model is 0x2F0000(=2,960K).   
If you use 4M SPI Flash Memory, you need to change this.   
![config_flash_size](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/81926a15-2d4e-466f-a889-d118b92eba0d)

# Configuration
![config-main](https://user-images.githubusercontent.com/6020549/107847756-a4aa2900-6e31-11eb-9525-6fd82bead5a3.jpg)
![config-app](https://user-images.githubusercontent.com/6020549/127939325-1b565ef7-9045-4800-95ad-0153342b5fc1.jpg)

## File System Selection
This project supports the following file systems.   
You can select any one using menuconfig.   
- LittleFS on Builtin SPI Flash Memory   
- FATFS on Builtin SPI Flash Memory   
- FATFS on SD card with SDSPI Interface   
- FATFS on SD card with SDMMC Interface (Valid only for ESP32/ESP32S3)   

Besides this, ESP32 supports SPIFFS, but this project will not use SPIFFS because it cannot handle directories.
![Image](https://github.com/user-attachments/assets/b6438577-6bf3-40d2-b700-b81f269fba37)
![Image](https://github.com/user-attachments/assets/32b224fe-dfc0-4aee-b46e-ac05362e8f89)

When using SD card with SDMMC, you can select 1 Line mode or 4 Line mode.   
![config-filesystem-3](https://user-images.githubusercontent.com/6020549/222020427-d1dd2c40-955d-46ca-b32f-3e8b8439778a.jpg)
![config-filesystem-4](https://user-images.githubusercontent.com/6020549/222020434-e54cd185-1b1c-45eb-bdf8-44d2f627fe5f.jpg)


Note:   
The connection when using SDSPI, SDMMC will be described later.   

Note:   
LittleFS requires ESP-IDF V5.2 or later.   

## WiFi Setting for Station-MODE

![config-wifi-sta](https://user-images.githubusercontent.com/6020549/222010634-3c6736ff-0d35-4982-9e6f-d56c7b7dc870.jpg)

You can connect using the mDNS hostname instead of the IP address.   
![config-wifi-2](https://user-images.githubusercontent.com/6020549/127940382-d431c962-746e-45d7-9693-3f844c0b01d3.jpg)

You can use static IP.   
![config-wifi-3](https://user-images.githubusercontent.com/6020549/127940390-3edfb3ea-6545-4709-9786-3e8a944e5ac7.jpg)

## WiFi Setting for AccessPoint-MODE

![config-wifi-ap](https://user-images.githubusercontent.com/6020549/222010670-4d0ab08f-857c-4828-98e4-3c42e39f6f2a.jpg)


## FTP Server Setting
![config-server](https://user-images.githubusercontent.com/6020549/127940653-0d54f2ca-5dee-4c97-a7e7-276299237a41.jpg)



# Using FATFS on SD card with SDSPI Interface

|ESP32|ESP32S2/S3|ESP32C2/C3/C6|SPI card pin|Notes|
|:-:|:-:|:-:|:-:|:--|
|GPIO23|GPIO35|GPIO01|MOSI|10k pullup if can't mount|
|GPIO19|GPIO37|GPIO03|MISO||
|GPIO18|GPIO36|GPIO02|SCK||
|GPIO14|GPIO34|GPIO04|CS|||
|3.3V|3.3V|3.3V|VCC|Don't use 5V supply|
|GND|GND|GND|GND||

![config-filesystem-SDSPI](https://user-images.githubusercontent.com/6020549/165686735-21461822-f19e-47d5-aedc-b91401670098.jpg)

__You can change it to any pin using menuconfig.__   

Note:   
This project doesn't utilize card detect (CD) and write protect (WP) signals from SD card slot.   

Click [here](https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdspi) for details.   

# Using FATFS on SD card with SDMMC Interface

On ESP32, SDMMC peripheral is connected to specific GPIO pins using the IO MUX.   
__GPIO pins cannot be customized.__   
GPIO2 and GPIO12 cannot be changed.   
Since these GPIOs are BootStrap, it is very tricky to use 4-line SD mode with ESP32.   
Click [here](https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdmmc) for details.

|ESP32 pin|SD card pin|Notes|
|:-:|:-:|:--|
|GPIO14|CLK|10k pullup|
|GPIO15|CMD|10k pullup|
|GPIO2|D0|10k pullup or connect to GPIO0|
|GPIO4|D1|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO12|D2|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO13|D3|not used in 1-line SD mode, but card's D3 pin must have a 10k pullup
|N/C|CD|not used in this project|
|N/C|WP|not used in this project|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

![config-filesystem-SDMMC-ESP32](https://user-images.githubusercontent.com/6020549/222021514-2e6bf65c-ccdb-49a4-b47d-b3c04367a7e3.jpg)


On ESP32-S3, SDMMC peripheral is connected to GPIO pins using GPIO matrix.   
__This allows arbitrary GPIOs to be used to connect an SD card.__   
Click [here](https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdmmc) for details.   
The default settings are as follows. But you can change it.   

|ESP32-S3 pin|SD card pin|Notes|
|:-:|:-:|:--|
|GPIO36|CLK|10k pullup|
|GPIO35|CMD|10k pullup|
|GPIO37|D0|10k pullup|
|GPIO38|D1|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO33|D2|not used in 1-line SD mode; 10k pullup in 4-line SD mode|
|GPIO34|D3|not used in 1-line SD mode, but card's D3 pin must have a 10k pullup
|N/C|CD|not used in this project|
|N/C|WP|not used in this project|
|3.3V|VCC|Don't use 5V supply|
|GND|GND||

![config-filesystem-SDMMC-ESP32S3](https://user-images.githubusercontent.com/6020549/222021554-d882563c-5a27-48f7-80c6-8caf1c41c544.jpg)

# Using long file name support   
By default, FATFS file names can be up to 8 characters long.   
If you use filenames longer than 8 characters, you need to change the values below.   
![config_long_file_name_support-1](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/dbae8910-74c9-4702-bdd4-881246e3fb95)
![config_long_file_name_support-2](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/75d1ea99-86ff-40c0-8ffc-8bd30b9fc32e)
![config_long_file_name_support-3](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/6e381627-e6f4-494d-ad40-04a73b49727b)

Long File Name on FLASH.   
![config_long_file_name_support-4](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/9fad16b1-0be4-4394-9bf0-347cd408152c)

Long File Name on SDCARD.    
![config_long_file_name_support-5](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/7a5912ae-c6e5-4fcd-8466-421d331d38f3)

Short File Name on SDCARD
![config_long_file_name_support-6](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/078c40fd-3ed2-44ec-9bf6-cfcc24bf1265)

# Changing sector size when using wear levelling library   
By default, 4096-byte sectors are used.   
You can change it to 512-byte sectors using menuconfig.   
The 512-byte sector has Performance mode and Safety mode.   

![config-serctor-seize-1](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/a3b6bfe8-8f54-4b5b-9521-54f81391efcf)
![config-serctor-seize-2](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/57806d78-0cb5-4f16-bdc0-0b72f28e0acf)
![config-serctor-seize-3](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/31ca5556-b3bf-41ab-b566-17849acd2837)
![config-serctor-seize-4](https://github.com/nopnop2002/esp-idf-ftpServer/assets/6020549/5bc89f87-16b6-487d-a400-ebcb0fb94573)

# Performance of each mode   
Using ESP32 and SanDisk Ultra 16GB Micro SD CARD.   
Enable Long filename support (Long filename buffer in heap).   
|File Syetem|Sector Size|Mode|Write Speed|Read Speed|
|:-:|:-:|:-:|:-:|:-:|
|LittlsFS on Builtin SPI Flash Memory|||51 kB/s|1678 kB/s|
|FATFS on Builtin SPI Flash Memory|512|Safety|2 kB/s|1735 kB/s|
|FATFS on Builtin SPI Flash Memory|512|Performance|5 kB/s|1735 kB/s|
|FATFS on Builtin SPI Flash Memory|4096||46 kB/s|2625 kB/s|
|FATFS on SD card with SDSPI Interface|4096||398 kB/s|610 kB/s|
|FATFS on SD card with SDMMC Interface|4096|1Line|499 kB/s|875kB/s|
|FATFS on SD card with SDMMC Interface|4096|4Line|572 kB/s|1190 kB/s|


# Limitations   
- The server does not support multiple connections.   
- The server does not support active connection.    
 __Only passive connections are supported.__   
 Unfortunately, Windows standard ftp.exe does not allow passive mode (PASV) connections.   
 If you have to make a passive mode connection On Windows, you need to use another software such as FFFTP / WinSCP / FileZilla to connect in passive mode.   
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
![LilyGo-esp32-s2-2](https://user-images.githubusercontent.com/6020549/127941181-63e48570-88ab-4d5b-8be7-343168079c4a.jpg)

# Windows ftp client
I tested these client.   
__You need to set the connection type to Passive Mode.__   
- WinSCP   
![WinScp](https://github.com/user-attachments/assets/c3e26311-74ee-42a7-a316-c100a8575434)

- FileZilla   
You need to make this setting when using FileZilla.
![FileZilla-2](https://user-images.githubusercontent.com/6020549/131819541-0ad0aacb-ed79-45c3-a536-bd239463ef02.jpg)
![FileZilla-1](https://github.com/user-attachments/assets/686a10ba-ef2d-4b15-be0f-80d321991f71)

- FFFTP   
![ffftp](https://github.com/user-attachments/assets/37ea0130-6847-4e93-8d83-669b4774be28)

- Windows standard ftp.exe   
Unfortunately, Windows standard ftp.exe does not allow passive mode (PASV) connections. 

# Troubleshooting   
I sometimes get this error when using external SPI SD card readers.   
Requires a PullUp resistor.   
![sd-card-1](https://user-images.githubusercontent.com/6020549/107848058-fe135780-6e33-11eb-9eac-7ce160571276.jpg)


You can see all the logging on the server side by commenting it out here.   
```
void ftp_task (void *pvParameters)
{
  ESP_LOGI(FTP_TAG, "ftp_task start");
  //esp_log_level_set(FTP_TAG, ESP_LOG_WARN); ------------> Comment out
  strcpy(ftp_user, CONFIG_FTP_USER);
  strcpy(ftp_pass, CONFIG_FTP_PASSWORD);
  ESP_LOGI(FTP_TAG, "ftp_user:[%s] ftp_pass:[%s]", ftp_user, ftp_pass);
```


# Reference
https://github.com/nopnop2002/esp-idf-ftpClient
