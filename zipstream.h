#ifndef _ZIPSTREAM_H_
#define _ZIPSTREAM_H_

#include <stdio.h>

#define ZS_SIGNATURE_FILE		0x04034b50
#define ZS_SIGNATURE_DESCRIPTOR		0x08074b50
#define ZS_SIGNATURE_DIRECTORY		0x02014b50
#define ZS_SIGNATURE_ENDDIRECTORY	0x06054b50

typedef struct ZSFile {
	time_t ftime;
	size_t fsize;
	unsigned long crc32;
	unsigned int lfname;
	char *fname;
	char *path;
	FILE *fp;

	char lfheader[30];
	char cdheader[46];
	char lfdescriptor[16];

	size_t offset;

	struct ZSFile *next;

	int c_lfheader;
	int c_name;
	int c_data;
	int c_lfdescriptor;
	int c_cdheader;
} ZSFile;

typedef struct {
	int nfiles;
	int c_nfiles;
	ZSFile *files;

	unsigned char eocd[22];

	int c_eocd;
} ZSDirectory;

#endif

