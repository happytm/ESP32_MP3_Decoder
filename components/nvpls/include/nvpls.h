/*
 * nvpls.h
 *
 * Created on: 06.27.2017
 * Author: n24bass@gmail.com
 *
 */

#ifndef _INCLUDE_NVSPLS_H_
#define _INCLUDE_NVSPLS_H_

#define NVSNAME "STATION"
#define MAXURLLEN 256
#define MAXSTATION 10

uint8_t nvpls_stno();
uint8_t nvpls_stno_max(int i);

char *nvpls_init(int d);
char *nvpls_set(int d, char *url);
char *nvpls_getcurrent();
char *nvpls_get(int n, char *buf, size_t length);
void nvpls_erase(int n);


#endif /* _INCLUDE_NVSPLS_H_ */
