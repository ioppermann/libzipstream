#include <stdio.h>

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

int main(int argc, char **argv) {
	int i;

	crc_init();

	printf("unsigned long crclookup[256] = {\n");

	for(i = 0; i < (256 - 8); i += 8)
		printf("%#010x, %#010x, %#010x, %#010x, %#010x, %#010x, %#010x, %#010x,\n", crclookup[i], crclookup[i + 1], crclookup[i + 2], crclookup[i + 3], crclookup[i + 4], crclookup[i + 5], crclookup[i + 6], crclookup[i + 7]);

	printf("%#010x, %#010x, %#010x, %#010x, %#010x, %#010x, %#010x, %#010x\n", crclookup[248], crclookup[249], crclookup[250], crclookup[251], crclookup[252], crclookup[253], crclookup[254], crclookup[255]);

	printf("};\n");

	return 0;
}

