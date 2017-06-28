/*
 * nvpls.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nvs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nvpls.h"

static const char *preset_url = "http://wbgo.streamguys.net/wbgo96"; // preset station URL

static uint8_t stno; // current station index no 
static uint8_t stno_max; // number of stations registered
static char sturl[MAXURLLEN]; // current station URL

static const char *key_i = "i"; // NVS key for current station index no 
static const char *key_n = "n"; // NVS key for number of stations registered

uint8_t nvpls_stno() { return stno; }

uint8_t nvpls_stno_max(int i) {
  stno_max += i;
  return stno_max;
}

/*
 * nvpls_init - get station URL
 * d: offset to current URL number
 */
char *nvpls_init(int d) {
  nvs_handle h;
  char index[2] = { '0', '\0' }; // NVS key for station name ('0'..'9')
  size_t length = MAXURLLEN;
  esp_err_t e;

  nvs_open(NVSNAME, NVS_READWRITE, &h);

  if (nvs_get_u8(h, key_n, &stno_max) != ESP_OK) {
	// initial station info
    stno = 0;
    stno_max = 1;
    nvs_set_u8(h, key_i, stno); // set current station index no 
    nvs_set_u8(h, key_n, stno_max); // set number of stations registered
    nvs_set_str(h, index, preset_url); // set default station
  }

  nvs_get_u8(h, key_i, &stno); // get current station index no 

  // get station URL
  while (1) {
    if (stno + d >= 0)
      stno = (stno + d) % stno_max;
    else
      stno = (stno + d + stno_max) % stno_max;
    index[0] = '0' + stno;
    e = nvs_get_str(h, index, sturl, &length);
    if (e == ESP_OK) break;
    if (abs(d) > 1) d = d / abs(d);
  }

  // update station number
  if (d != 0) nvs_set_u8(h, key_i, stno);

  // commit
  nvs_commit(h);
  nvs_close(h);

  printf("init_url(%d) stno=%d, stno_max=%d, sturl=%s\n", d, stno, stno_max, sturl);

  return sturl;
}

/*
 * nvpls_set - set station URL(url) to index(d)
 */
char *nvpls_set(int d, char *url) {
  nvs_handle h;
  char index[2] = { '0', '\0' }; // NVS key for station name ('0'..'9')
  size_t length = MAXURLLEN;

  printf("set_url(%d, %s) stno_max=%d\n", d, url, stno_max);

  // illegal URL text length
  if (strlen(url) >= MAXURLLEN) return NULL;

  if (d > stno_max || d < 0) d = stno_max;
  if (d == stno_max) stno_max++;
  if (stno_max > MAXSTATION) return NULL; // error

  nvs_open(NVSNAME, NVS_READWRITE, &h);

  stno = d;
  index[0] = '0' + stno;
  nvs_set_u8(h, key_n, stno_max);
  nvs_set_str(h, index, url);
  nvs_commit(h);
  nvs_get_str(h, index, sturl, &length);

  nvs_commit(h);
  nvs_close(h);

  return sturl;
}

/*
 * nvpls_getcurent - get current station URL 
 */
char *nvpls_getcurrent() {
  return sturl;
}

/*
 * get_nvurl - get station URL(index=n) from NVS
 */
char *nvpls_get(int n, char *buf, size_t length) {
  nvs_handle h;
  char index[2] = { '0', '\0' };
  // length = MAXURLLEN;

  n %= stno_max;

  nvs_open(NVSNAME, NVS_READWRITE, &h);
  index[0] += n;
  if (nvs_get_str(h, index, buf, &length) != OK) {
    buf[0] = '\0';
  }
  nvs_close(h);

  return buf;
}

/*
 * nvpls_erase - erase station URL(index=n) and renumber
 */
void nvpls_erase(int n) {
  nvs_handle h;
  char index[2] = { '0', '\0' };
  
  n %= stno_max;
  nvs_open(NVSNAME, NVS_READWRITE, &h);
  index[0] += n;
  nvs_erase_key(h, index);

  stno_max--;
  nvs_set_u8(h, key_n, stno_max);

  for (;n < stno_max; n++) {
    char buf[MAXURLLEN];
    size_t length = MAXURLLEN;

    index[0] = '0' + n + 1;
    nvs_get_str(h, index, buf, &length);
    index[0]--;
    nvs_set_str(h, index, buf);
  }

  nvs_commit(h);
  nvs_close(h);
}

// EOF
