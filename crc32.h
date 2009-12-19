#ifndef _CRC32_H_
#define _CRC32_H_

unsigned long crc_partial(unsigned long crc, const unsigned char *data, unsigned long len);
unsigned long crc_start(void);
unsigned long crc_finish(unsigned long crc);

#endif
