#ifndef _ZIPSTREAM_H_
#define _ZIPSTREAM_H_

#include <stdio.h>
#include <time.h>

#define ZS_STAGE_LENGTH_MAX		46

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
	char stage_data[ZS_STAGE_LENGTH_MAX];

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
int zs_write(ZS *zs, char *buf, int sbuf);
void zs_free(ZS *zs);

#endif
