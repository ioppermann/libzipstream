#ifndef _ZIPSTREAM_H_
#define _ZIPSTREAM_H_

#include <stdio.h>
#include <time.h>

#define ZS_SIGNATURE_LFH	0x04034b50
#define ZS_SIGNATURE_LFD	0x08074b50
#define ZS_SIGNATURE_CDH	0x02014b50
#define ZS_SIGNATURE_EOCD	0x06054b50

#define ZS_LENGTH_LFH		30
#define ZS_LENGTH_LFD		16
#define ZS_LENGTH_CDH		46
#define ZS_LENGTH_EOCD		22

#define ZS_LENGTH_MAX		ZS_LENGTH_CDH

typedef struct ZSFile {
	char *fpath;
	char *fname;

	size_t lfname;

	time_t ftime;
	size_t fsize;

	unsigned long crc32;

	size_t offset;

	struct ZSFile *next;
} ZSFile;

typedef struct {
	int nfiles;
	ZSFile *files;
} ZSDirectory;

enum stages {NONE = 0, LF_HEADER, LF_NAME, LF_DATA, LF_DESCRIPTOR, CD_HEADER, CD_NAME, EOCD, FIN, ERROR};

typedef struct {
	// Current file
	ZSFile *zsf;

	// Current file pointer
	FILE *fp;

	// Stage
	stages stage;

	// Stage data
	char stage_data[ZS_LENGTH_MAX];

	// Stage position
	size_t stage_pos;

	// Finalized
	int finalized;

	// Directory
	ZSDirectory zsd;
} ZS;

ZS *zs_init(void);
int zs_add_file(ZS *zs, const char *path);
void zs_finalize(ZS *zs);
int zs_write(ZS *zs, char *buf, size_t sbuf);
void zs_free(ZS *zs);

#endif

