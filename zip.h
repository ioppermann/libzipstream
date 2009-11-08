#ifndef _ZIP_H_
#define _ZIP_H_

#include "zipstream.h"

void zs_build_lfheader(ZSFile *zsf);
void zs_build_descriptor(ZSFile *zsf);
void zs_build_offsets(ZSDirectory *zs);
void zs_build_eocd(ZSDirectory *zs);
void zs_build_cdheader(ZSFile *zsf);

int zs_write_directory(ZSDirectory *zs, char *buf, int sbuf);

size_t zs_get_cdoffset(ZSDirectory *zs);
size_t zs_get_cdsize(ZSDirectory *zs);

void zs_stager(ZS *zs);

#endif