#ifndef _ZIP_H_
#define _ZIP_H_

#include "zipstream.h"

#define ZS_LENGTH_LFH		30
#define ZS_LENGTH_LFD		16
#define ZS_LENGTH_CDH		46
#define ZS_LENGTH_EOCD		22

void zs_build_lfh(ZS *zs);
void zs_build_lfd(ZS *zs);
void zs_build_cdh(ZS *zs);
void zs_build_eocd(ZS *zs);

int zs_write_stagedata(ZS *zs, char *buf, int sbuf, int size);
int zs_write_filename(ZS *zs, char *buf, int sbuf);
int zs_write_filedata(ZS *zs, char *buf, int sbuf);
int zs_write_filedata_deflate(ZS *zs, char *buf, int sbuf);

size_t zs_get_cdoffset(ZS *zs);
size_t zs_get_cdsize(ZS *zs);

void zs_stager(ZS *zs);

#endif
