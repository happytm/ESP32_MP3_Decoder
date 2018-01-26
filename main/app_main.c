
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "http.h"
#include "driver/i2s.h"

#include "vector.h"
#include "ui.h"
#include "spiram_fifo.h"
#include "audio_renderer.h"
#include "web_radio.h"
#include "playerconfig.h"
#include "wifi.h"
#include "app_main.h"
#ifdef CONFIG_BT_SPEAKER_MODE
#include "bt_speaker.h"
#endif
#ifdef CONFIG_NVS_PLAYLIST
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/tcp.h"
#include "nvpls.h"
#define TEST_URL "http://wbgo.streamguys.net/wbgo96"
#else
#include "playlist.h"
#define WIFI_LIST_NUM   10
#endif

#ifdef CONFIG_OLED_DISPLAY
#include "oled.h"
#endif

#define TAG "main"

//Priorities of the reader and the decoder thread. bigger number = higher prio
#define PRIO_READER configMAX_PRIORITIES -3
#define PRIO_MQTT configMAX_PRIORITIES - 3
#define PRIO_CONNECT configMAX_PRIORITIES -1

const static char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const static char http_t[] = "<html><head><title>ESP32 PCM5102A webradio</title></head><body><h1>ESP32 PCM5102A webradio</h1><h2>Station list</h2><ul>";
const static char http_e[] = "</ul><a href=\"P\">prev</a>&nbsp;<a href=\"N\">next</a></body></html>";

static web_radio_t *radio_config = NULL;

static void init_hardware()
{
#ifdef CONFIG_NVS_PLAYLIST
    nvs_flash_init();
#endif

    // init UI
    // ui_init(GPIO_NUM_32);

    //Initialize the SPI RAM chip communications and see if it actually retains some bytes. If it
    //doesn't, warn user.
    if (!spiRamFifoInit()) {
        printf("\n\nSPI RAM chip fail!\n");
        while(1);
    }

    ESP_LOGI(TAG, "hardware initialized");
}

static void start_wifi()
{
    ESP_LOGI(TAG, "starting network");

    /* FreeRTOS event group to signal when we are connected & ready to make a request */
    EventGroupHandle_t wifi_event_group = xEventGroupCreate();

    /* init wifi */
    ui_queue_event(UI_CONNECTING);
    initialise_wifi(wifi_event_group);

    /* Wait for the callback to set the CONNECTED_BIT in the event group. */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);

    ui_queue_event(UI_CONNECTED);
}


#ifdef CONFIG_NVS_PLAYLIST

/*
 * web interface
 */

static void http_server_netconn_serve(struct netconn *conn)
{
  struct netbuf *inbuf;
  char *buf;
  u16_t buflen;
  err_t err;

  int np = 0;
  extern void software_reset();

  /* Read the data from the port, blocking if nothing yet there.
   We assume the request (the part we care about) is in one netbuf */
  err = netconn_recv(conn, &inbuf);

  if (err == ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);

    /* Is this an HTTP GET command? (only check the first 5 chars, since
    there are other formats for GET, and we're keeping it very simple )*/
    if (buflen>=5 &&
        buf[0]=='G' &&
        buf[1]=='E' &&
        buf[2]=='T' &&
        buf[3]==' ' &&
        buf[4]=='/' ) {
      printf("%c\n", buf[5]);
      /* Send the HTML header
       * subtract 1 from the size, since we dont send the \0 in the string
       * NETCONN_NOCOPY: our data is const static, so no need to copy it
       */
      if (buflen > 5) {
        switch (buf[5]) {
        case 'N':
          np = 1; break;
        case 'P':
          np = -1; break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          {
            int i = buf[5] - '0';
            if (i > nvpls_stno_max(0)) i = nvpls_stno_max(0);
            if (buf[6] == '+') {
              if (strncmp(buf + 7, "http://", 7) == 0) {
                np = i - nvpls_stno(0);
                if (i == nvpls_stno_max(0)) nvpls_stno_max(1);
                char *p = strchr(buf + 7, ' ');
                *p = '\0';
                nvpls_set(i, buf + 7);
              }
            } else if (buf[6] == '-') {
              nvpls_erase(i);
              np = -1;
            } else {
              np = i - nvpls_stno();
            }
          }
        default:
          break;
        }
      }

      netconn_write(conn, http_html_hdr, sizeof(http_html_hdr)-1, NETCONN_NOCOPY);
      if (np != 0) nvpls_init(np);
      /* Send our HTML page */
      netconn_write(conn, http_t, sizeof(http_t)-1, NETCONN_NOCOPY);
      for (int i = 0; i < nvpls_stno_max(0); i++) {
        char buf[MAXURLLEN];
        int length = MAXURLLEN;
        sprintf(buf, "<li><a href=\"/%d\">", i);
        netconn_write(conn, buf, strlen(buf), NETCONN_NOCOPY);
        nvpls_get(i, buf, length);
        if (i == nvpls_stno()) netconn_write(conn, "<b>", 3, NETCONN_NOCOPY);
        netconn_write(conn, buf, strlen(buf), NETCONN_NOCOPY);
        if (i == nvpls_stno()) netconn_write(conn, "</b> - now playing", 19, NETCONN_NOCOPY);
        netconn_write(conn, "</a></li>", 9, NETCONN_NOCOPY);
      }
      netconn_write(conn, http_e, sizeof(http_e)-1, NETCONN_NOCOPY);
    }
  }
  /* Close the connection (server closes in HTTP) */
  netconn_close(conn);

  /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
  netbuf_delete(inbuf);

  if (np != 0) {
    vTaskDelay(1000/portTICK_RATE_MS);
    web_radio_stop(radio_config);
    ESP_LOGW(TAG, "next track: %s", radio_config->url);
    while(radio_config->player_config->decoder_status != STOPPED) {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    radio_config->url = nvpls_init(0);
    web_radio_start(radio_config);

    // netconn_delete(conn);
    // vTaskDelay(3000/portTICK_RATE_MS);
    // software_reset();
  }
}

static void http_server(void *pvParameters)
{
  struct netconn *conn, *newconn;
  err_t err;
  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn, NULL, 80);
  netconn_listen(conn);
  do {
     err = netconn_accept(conn, &newconn);
     if (err == ERR_OK) {
       http_server_netconn_serve(newconn);
       netconn_delete(newconn);
     }
   } while(err == ERR_OK);
   netconn_close(conn);
   netconn_delete(conn);
}

#endif /* CONFIG_NVS_PLAYLIST */

/* */

static renderer_config_t *create_renderer_config()
{
    renderer_config_t *renderer_config = calloc(1, sizeof(renderer_config_t));

    renderer_config->bit_depth = I2S_BITS_PER_SAMPLE_16BIT;
    renderer_config->i2s_num = I2S_NUM_0;
    renderer_config->sample_rate = 44100;
    renderer_config->sample_rate_modifier = 1.0;
    renderer_config->output_mode = AUDIO_OUTPUT_MODE;

    if(renderer_config->output_mode == I2S_MERUS) {
        renderer_config->bit_depth = I2S_BITS_PER_SAMPLE_32BIT;
    }

    if(renderer_config->output_mode == DAC_BUILT_IN) {
        renderer_config->bit_depth = I2S_BITS_PER_SAMPLE_16BIT;
    }

    return renderer_config;
}

static void start_web_radio()
{
    // init web radio
    radio_config = calloc(1, sizeof(web_radio_t));
#ifdef CONFIG_NVS_PLAYLIST
    radio_config->url = nvpls_init(0); // URL
    xTaskCreate(&http_server, "http_server", 2048, NULL, 5, NULL);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
    io_conf.pin_bit_mask = GPIO_SEL_16;
    io_conf.mode = GPIO_MODE_INPUT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    if (gpio_get_level(GPIO_NUM_16) == 0) {
      // enter maintenance mode
      while (1) 
        vTaskDelay(200/portTICK_RATE_MS);
    }
#else
    radio_config->playlist = playlist_create();
    playlist_load_pls(radio_config->playlist);
#endif

    // init player config
    radio_config->player_config = calloc(1, sizeof(player_t));
    radio_config->player_config->command = CMD_NONE;
    radio_config->player_config->decoder_status = UNINITIALIZED;
    radio_config->player_config->decoder_command = CMD_NONE;
    radio_config->player_config->buffer_pref = BUF_PREF_SAFE;
    radio_config->player_config->media_stream = calloc(1, sizeof(media_stream_t));

    // init renderer
    renderer_init(create_renderer_config());

    // start radio
    web_radio_init(radio_config);
    web_radio_start(radio_config);
}

/**
 * entry point
 */
void app_main()
{
    ESP_LOGI(TAG, "starting app_main()");
    ESP_LOGI(TAG, "RAM left: %u", esp_get_free_heap_size());

    init_hardware();

#ifdef CONFIG_OLED_DISPLAY
    oled_init();
#endif
#ifdef CONFIG_BT_SPEAKER_MODE
#ifdef CONFIG_OLED_DISPLAY
    oled_test(0, "");
#endif
    bt_speaker_start(create_renderer_config());
#else
    start_wifi();
    start_web_radio();
#endif

    ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());
    // ESP_LOGI(TAG, "app_main stack: %d\n", uxTaskGetStackHighWaterMark(NULL));

    while (1) {
      vTaskDelay(25/portTICK_RATE_MS);
#ifdef CONFIG_OLED_DISPLAY
      oled_scroll();
#endif
    }
}
