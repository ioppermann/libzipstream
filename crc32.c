#include <stdio.h>
#include "crc32.h"

unsigned long crclookup[256];

unsigned long crc_reflect(unsigned long reflect, const char c);

void crc_init(void) {
	int i, pos;
	unsigned long polynomial = 0x04C11DB7;

	for(i = 0; i <= 256; i++) {
		crclookup[i] = crc_reflect(i, 8) << 24;

		for(pos = 0; pos < 8; pos++)
			crclookup[i] = (crclookup[i] << 1) ^ ((crclookup[i] & (1 << 31)) ? polynomial : 0);

		crclookup[i] = crc_reflect(crclookup[i], 32);
	}

	return;
}

unsigned long crc_reflect(unsigned long reflect, const char c) {
	int pos;
	unsigned long value = 0;

	// Swap bit 0 for bit 7, bit 1 For bit 6, etc....
	for(pos = 1; pos < (c + 1); pos++) {
		if(reflect & 1)
			value |= (1 << (c - pos));

		reflect >>= 1;
	}

	return value;
}

unsigned long crc_partial(unsigned long crc, const unsigned char *data, unsigned long len) {
	while(len--)
		crc = (crc >> 8) ^ crclookup[(crc & 0xFF) ^ *data++];

	return crc;
}

unsigned long crc_start(void) {
	return 0xFFFFFFFF;
}

unsigned long crc_finish(unsigned long crc) {
	return (crc ^ 0xFFFFFFFF);
}

