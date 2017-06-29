/*
  oled.c
  Author: n24bass@gmail.com
  created: 2017.06.27
 */

#include "esp_wifi.h"
#include "esp_log.h"

#include "xi2c.h"
#include "fonts.h"
#include "ssd1306.h"

#define TAG "oled"

#define I2C_EXAMPLE_MASTER_SCL_IO    14    // gpio number for I2C master clock
#define I2C_EXAMPLE_MASTER_SDA_IO    13    // gpio number for I2C master data
#define I2C_EXAMPLE_MASTER_NUM I2C_NUM_1   // I2C port number for master dev 
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE   0   // I2C master do not need buffer
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE   0   // I2C master do not need buffer
#define I2C_EXAMPLE_MASTER_FREQ_HZ    100000    // I2C master clock frequency

void oled_init(void)
{
  // printf("oled_init\n");
  int i2c_master_port = I2C_EXAMPLE_MASTER_NUM;
  i2c_config_t conf;
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = I2C_EXAMPLE_MASTER_SDA_IO;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = I2C_EXAMPLE_MASTER_SCL_IO;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = I2C_EXAMPLE_MASTER_FREQ_HZ;
  i2c_param_config(i2c_master_port, &conf);
  i2c_driver_install(i2c_master_port, conf.mode,
                     I2C_EXAMPLE_MASTER_RX_BUF_DISABLE,
                     I2C_EXAMPLE_MASTER_TX_BUF_DISABLE, 0);
  SSD1306_Init();
}

void oled_test(int mode, char *url)
{
  ESP_LOGI(TAG, "oled_test(mode=%d,url='%s')", mode, url);

  SSD1306_Fill(SSD1306_COLOR_BLACK); // clear screen

  SSD1306_GotoXY(40, 4);
  SSD1306_Puts("ESP32", &Font_11x18, SSD1306_COLOR_WHITE);
    
  SSD1306_GotoXY(2, 20);
#ifdef CONFIG_BT_SPEAKER_MODE /////bluetooth speaker mode/////
  SSD1306_Puts("PCM5102 BT speaker", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(2, 30);
  SSD1306_Puts("my device name is", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(2, 39);
  SSD1306_Puts(CONFIG_BT_NAME, &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(16, 53);
  SSD1306_Puts("Yeah! Speaker!", &Font_7x10, SSD1306_COLOR_WHITE);
#else ////////for webradio mode display////////////////
  SSD1306_Puts("PCM5102A webradio", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(2, 30);
  if (mode) {
    SSD1306_Puts("web server is up.", &Font_7x10, SSD1306_COLOR_WHITE);
  } else {
    SSD1306_Puts(url, &Font_7x10, SSD1306_COLOR_WHITE);
    if (strlen(url) > 18)  {
      SSD1306_GotoXY(2, 39);
      SSD1306_Puts(url + 18, &Font_7x10, SSD1306_COLOR_WHITE);
    }
    SSD1306_GotoXY(16, 53);
  }

  tcpip_adapter_ip_info_t ip_info;
  tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
  
  SSD1306_GotoXY(2, 53);
  SSD1306_Puts("IP:", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_Puts(ip4addr_ntoa(&ip_info.ip), &Font_7x10, SSD1306_COLOR_WHITE);    
#endif

  /* Update screen, send changes to LCD */
  SSD1306_UpdateScreen();
}

// EOF
