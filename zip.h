#ifndef _ZIP_H_
#define _ZIP_H_

#include "zipstream.h"

#define ZS_SIGNATURE_LFH	0x04034b50
#define ZS_SIGNATURE_LFD	0x08074b50
#define ZS_SIGNATURE_CDH	0x02014b50
#define ZS_SIGNATURE_EOCD	0x06054b50

#define ZS_LENGTH_LFH		30
#define ZS_LENGTH_LFD		16
#define ZS_LENGTH_CDH		46
#define ZS_LENGTH_EOCD		22

void zs_build_lfh(ZSFile *zsf);
void zs_build_lfd(ZSFile *zsf);
void zs_build_cdh(ZSFile *zsf);
void zs_build_eocd(ZS *zs);

void zs_build_offsets(ZSDirectory *zs);

int zs_write_stagedata(ZS *zs, char *buf, int sbuf, int size);
int zs_write_filename(ZS *zs, char *buf, int sbuf);
int zs_write_filedata(ZS *zs, char *buf, int sbuf);

size_t zs_get_cdoffset(ZS *zs);
size_t zs_get_cdsize(ZS *zs);

void zs_stager(ZS *zs);

#endif