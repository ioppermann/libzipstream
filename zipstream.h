#ifndef _ZIPSTREAM_H_
#define _ZIPSTREAM_H_

#include <stdio.h>
#include <time.h>
#include <zlib.h>
#include <bzlib.h>

#define ZS_STAGE_LENGTH_MAX		46

#define ZS_COMPRESS_NONE		0
#define ZS_COMPRESS_DEFLATE		8
#define ZS_COMPRESS_BZIP2		12

#define ZS_COMPRESS_LEVEL_DEFAULT	0
#define ZS_COMPRESS_LEVEL_SPEED		1
#define ZS_COMPRESS_LEVEL_SIZE		9

#define ZS_COMPRESS_BUFFER_DEFLATE	4096
#define ZS_COMPRESS_BUFFER_BZIP2	4096

#define ZSE_OK				0

typedef struct ZSFile {
	char *fpath;
	char *fname;

	size_t lfname;

	time_t ftime;
	size_t fsize;
	size_t fsize_compressed;

	int completed;

	unsigned long crc32;

	size_t offset;

	int compression;
	int level;

	int version;

	struct ZSFile *prev;
	struct ZSFile *next;
} ZSFile;

typedef struct {
	int nfiles;
	ZSFile *files;
} ZSDirectory;

typedef enum {NONE = 0, LF_HEADER, LF_NAME, LF_DATA, LF_DESCRIPTOR, CD_HEADER, CD_NAME, EOCD, FIN, ERROR} stages;

typedef struct ZS {
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

	// File data writer
	int (*write_filedata)(struct ZS *, char *, int);

	struct {
		z_stream strm;
		int init;
		int avail_in;
		int flush;
		int level;
		char in[ZS_COMPRESS_BUFFER_DEFLATE];
	} deflate;

	struct {
		bz_stream strm;
		int init;
		int avail_in;
		int flush;
		int level;
		char in[ZS_COMPRESS_BUFFER_BZIP2];
	} bzip2;
} ZS;

ZS *zs_init(void);
int zs_add_file(ZS *zs, const char *targetpath, const char *sourcepath, int compression, int level);
void zs_finalize(ZS *zs);
int zs_read(ZS *zs, char *buf, int sbuf);
void zs_free(ZS *zs);

#endif
