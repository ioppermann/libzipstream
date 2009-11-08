#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libgen.h>	// basename(3)
#include <sys/types.h>
#include <sys/stat.h>

#include "zipstream.h"
#include "zip.h"
#include "crc32.h"

int main(int argc, char **argv) {
	int bytes;
	char buf[256];
	ZS *zs;

	zs = zs_init();

	zs_add_file(zs, "foobar.mp4");
	zs_add_file(zs, "1171032474.mpg");
	zs_add_file(zs, "asnumber.zip");

	zs_finalize(zs);

	while((bytes = zs_write(zs, buf, sizeof(buf))) > 0) {
		fprintf(stderr, "%d\n", bytes);
		fwrite(buf, 1, bytes, stdout);
	}

	zs_free(zs);

	return 0;
}

ZS *zs_init(void) {
	ZS *zs;

	zs = (ZS *)calloc(1, sizeof(ZS));
	if(zs == NULL)
		return NULL;

	crc_init();

	return zs;
}

void zs_free(ZS *zs) {
	ZSFile *zsf, *pzsf;

	if(zs == NULL)
		return;

	zsf = zs->zsd.files;

	while(zsf != NULL) {
		free(zsf->fpath);
		free(zsf->fname);

		pzsf = zsf;
		zsf = zsf->next;

		free(pzsf);
	}

	free(zs);

	return;
}

int zs_add_file(ZS *zs, const char *path) {
	ZSFile *zsf, *pzsf;
	struct stat sb;

	if(zs == NULL)
		return -1;

	if(zs->finalized == 1)
		return -1;

	if(stat(path, &sb) == -1)
		return -1;

	if(!S_ISREG(sb.st_mode))
		return -1;

	zsf = (ZSFile *)calloc(1, sizeof(ZSFile));
	if(zsf == NULL)
		return -1;

	zsf->fpath = strdup(path);
	if(zsf->fpath == NULL) {
		free(zsf);

		return -1;
	}

	zsf->fname = basename(zsf->fpath);
	if(zsf->fname == NULL) {
		free(zsf->fpath);
		free(zsf);

		return -1;
	}

	zsf->lfname = strlen(zsf->fname);

	zsf->ftime = sb.st_mtime;
	zsf->fsize = sb.st_size;

	if(zs->zsd.nfiles != 0) {
		pzsf = zs->zsd.files;

		while(pzsf->next != NULL) {
			pzsf = pzsf->next;
		}

		pzsf->next = zsf;
		zsf->prev = pzsf;
	}
	else
		zs->zsd.files = zsf;

	zs->zsd.nfiles++;

	return 0;
}

void zs_finalize(ZS *zs) {
	if(zs == NULL)
		return;

	zs->finalized = 1;

	return;
}

int zs_write(ZS *zs, char *buf, int sbuf) {
	int bytes;

	if(zs == NULL)
		return -1;

	if(zs->finalized == 0)
		return -1;

	zs_stager(zs);

	if(zs->stage == ERROR)
		return -1;

	if(zs->stage == FIN)
		return 0;

	switch(zs->stage) {
		case LF_HEADER:
			bytes = zs_write_stagedata(zs, buf, sbuf, ZS_LENGTH_LFH);
			break;
		case LF_DESCRIPTOR:
			bytes = zs_write_stagedata(zs, buf, sbuf, ZS_LENGTH_LFD);
			break;
		case CD_HEADER:
			bytes = zs_write_stagedata(zs, buf, sbuf, ZS_LENGTH_CDH);
			break;
		case EOCD:
			bytes = zs_write_stagedata(zs, buf, sbuf, ZS_LENGTH_EOCD);
			break;
		case LF_NAME:
		case CD_NAME:
			bytes = zs_write_filename(zs, buf, sbuf);
			break;
		case LF_DATA:
			bytes = zs_write_filedata(zs, buf, sbuf);
			break;
		default:
			return -1;
	}

	return bytes;
}

int zs_write_stagedata(ZS *zs, char *buf, int sbuf, int size) {
	int i;
	int bytes, bytesread;

	bytes = 0;

	bytesread = size - zs->stage_pos;
	if(sbuf < bytesread)
		bytesread = sbuf;

	for(i = 0; i < bytesread; i++) {
		buf[bytes] = zs->stage_data[i + zs->stage_pos];

		bytes++;
	}

	zs->stage_pos += bytesread;

	return bytes;
}

int zs_write_filename(ZS *zs, char *buf, int sbuf) {
	int i;
	int bytes, bytesread;

	bytes = 0;

	bytesread = zs->zsf->lfname - zs->stage_pos;
	if(sbuf < bytesread)
		bytesread = sbuf;

	for(i = 0; i < bytesread; i++) {
		buf[bytes] = zs->zsf->fname[i + zs->stage_pos];

		bytes++;
	}

	zs->stage_pos += bytesread;

	return bytes;
}

int zs_write_filedata(ZS *zs, char *buf, int sbuf) {
	int bytesread;

	bytesread = fread(buf, 1, sbuf, zs->fp);
	zs->stage_pos += bytesread;

	zs->zsf->crc32 = crc_partial(zs->zsf->crc32, buf, bytesread);

	if(bytesread < sbuf)	// EOF
		zs->zsf->fsize = zs->stage_pos;

	return bytesread;
}

void zs_stager(ZS *zs) {
	if(zs->stage == NONE) {
		// select first file
		zs->zsf = zs->zsd.files;

		zs->stage = LF_HEADER;
		zs->stage_pos = 0;
	}

	if(zs->stage == LF_HEADER) {
		if(zs->zsf == NULL) {
			// select first file
			zs->zsf = zs->zsd.files;

			zs->stage = CD_HEADER;
			zs->stage_pos = 0;
		}
		else {
			if(zs->stage_pos == 0) {
				zs_build_lfh(zs);
			}
			else if(zs->stage_pos == ZS_LENGTH_LFH) {
				zs->stage = LF_NAME;
				zs->stage_pos = 0;
			}
		}
	}

	if(zs->stage == LF_NAME) {
		if(zs->stage_pos == zs->zsf->lfname) {
			zs->stage = LF_DATA;
			zs->stage_pos = 0;

			zs->zsf->crc32 = crc_start();

			zs->fp = fopen(zs->zsf->fpath, "rb");
			if(zs->fp == NULL)
				zs->stage = ERROR;
		}
	}

	if(zs->stage == LF_DATA) {
		if(zs->stage_pos == zs->zsf->fsize) {
			zs->stage = LF_DESCRIPTOR;
			zs->stage_pos = 0;

			fclose(zs->fp);

			zs->zsf->crc32 = crc_finish(zs->zsf->crc32);

			if(zs->zsf->prev != NULL)
				zs->zsf->offset = zs->zsf->prev->offset;

			zs->zsf->offset += ZS_LENGTH_LFH;
			zs->zsf->offset += zs->zsf->lfname;
			zs->zsf->offset += zs->zsf->fsize;
			zs->zsf->offset += ZS_LENGTH_LFD;
		}
	}

	if(zs->stage == LF_DESCRIPTOR) {
		if(zs->stage_pos == 0) {
			zs_build_lfd(zs);
		}
		else if(zs->stage_pos == ZS_LENGTH_LFD) {
			zs->zsf = zs->zsf->next;

			zs->stage = LF_HEADER;
			zs->stage_pos = 0;
		}
	}

	if(zs->stage == CD_HEADER) {
		if(zs->zsf == NULL) {
			zs->stage = EOCD;
			zs->stage_pos = 0;
		}
		else {
			if(zs->stage_pos == 0) {
				zs_build_cdh(zs);
			}
			else if(zs->stage_pos == ZS_LENGTH_CDH) {
				zs->stage = CD_NAME;
				zs->stage_pos = 0;
			}
		}
	}

	if(zs->stage == CD_NAME) {
		if(zs->stage_pos == zs->zsf->lfname) {
			zs->zsf = zs->zsf->next;

			zs->stage = CD_HEADER;
			zs->stage_pos = 0;
		}
	}

	if(zs->stage == EOCD) {
		if(zs->stage_pos == 0) {
			zs_build_eocd(zs);
		}
		else if(zs->stage_pos == ZS_LENGTH_EOCD) {
			zs->stage = FIN;
			zs->stage_pos = 0;
		}
	}

	if(zs->stage == FIN)
		zs->stage = FIN;

	return;
}

void zs_build_lfh(ZS *zs) {
	struct tm *ltime;
	int tmp;

	if(zs == NULL)
		return;

	// Signature
	zs->stage_data[ 0] = ((ZS_SIGNATURE_LFH >>  0) && 0xFF);
	zs->stage_data[ 1] = ((ZS_SIGNATURE_LFH >>  8) && 0xFF);
	zs->stage_data[ 2] = ((ZS_SIGNATURE_LFH >> 16) && 0xFF);
	zs->stage_data[ 3] = ((ZS_SIGNATURE_LFH >> 24) && 0xFF);

	// Version
	zs->stage_data[ 4] = 0x0A;
	zs->stage_data[ 5] = 0x00;

	// General Purpose
	zs->stage_data[ 6] = 0x08;
	zs->stage_data[ 7] = 0x00;

	// Compression Method
	zs->stage_data[ 8] = 0x00;	// Store
	zs->stage_data[ 9] = 0x00;

	// Modification Time
	ltime = localtime(&zs->zsf->ftime);
	tmp = 0;
	tmp |= (ltime->tm_hour << 11);
	tmp |= (ltime->tm_min << 5);
	tmp |= (ltime->tm_sec / 2);
	zs->stage_data[10] = ((tmp >>  0) & 0xFF);
	zs->stage_data[11] = ((tmp >>  8) & 0xFF);

	// Modification Date
	tmp = 0;
	tmp |= ((ltime->tm_year - 80) << 9);
	tmp |= ((ltime->tm_mon + 1) << 5);
	tmp |= ltime->tm_mday;
	zs->stage_data[12] = ((tmp >>  0) & 0xFF);
	zs->stage_data[13] = ((tmp >>  8) & 0xFF);

	// CRC32
	zs->stage_data[14] = 0x00;
	zs->stage_data[15] = 0x00;
	zs->stage_data[16] = 0x00;
	zs->stage_data[17] = 0x00;

	// Compressed Size
	zs->stage_data[18] = 0x00;
	zs->stage_data[19] = 0x00;
	zs->stage_data[20] = 0x00;
	zs->stage_data[21] = 0x00;

	// Uncompressed Size
	zs->stage_data[22] = 0x00;
	zs->stage_data[23] = 0x00;
	zs->stage_data[24] = 0x00;
	zs->stage_data[25] = 0x00;

	// Filename Length
	zs->stage_data[26] = ((zs->zsf->lfname >>  0) & 0xFF);
	zs->stage_data[27] = ((zs->zsf->lfname >>  8) & 0xFF);

	// Extra Field Length
	zs->stage_data[28] = 0x00;
	zs->stage_data[29] = 0x00;

	return;
}

void zs_build_lfd(ZS *zs) {
	if(zs == NULL)
		return;

	// Signature
	zs->stage_data[ 0] = ((ZS_SIGNATURE_LFD >>  0) && 0xFF);
	zs->stage_data[ 1] = ((ZS_SIGNATURE_LFD >>  8) && 0xFF);
	zs->stage_data[ 2] = ((ZS_SIGNATURE_LFD >> 16) && 0xFF);
	zs->stage_data[ 3] = ((ZS_SIGNATURE_LFD >> 24) && 0xFF);

	// CRC32
	zs->stage_data[ 4] = ((zs->zsf->crc32 >>  0) & 0xFF);
	zs->stage_data[ 5] = ((zs->zsf->crc32 >>  8) & 0xFF);
	zs->stage_data[ 6] = ((zs->zsf->crc32 >> 16) & 0xFF);
	zs->stage_data[ 7] = ((zs->zsf->crc32 >> 24) & 0xFF);

	// Compressed Size
	zs->stage_data[ 8] = ((zs->zsf->fsize >>  0) & 0xFF);
	zs->stage_data[ 9] = ((zs->zsf->fsize >>  8) & 0xFF);
	zs->stage_data[10] = ((zs->zsf->fsize >> 16) & 0xFF);
	zs->stage_data[11] = ((zs->zsf->fsize >> 24) & 0xFF);

	// Uncompressed Size
	zs->stage_data[12] = zs->stage_data[ 8];
	zs->stage_data[13] = zs->stage_data[ 9];
	zs->stage_data[14] = zs->stage_data[10];
	zs->stage_data[15] = zs->stage_data[11];

	return;
}

void zs_build_cdh(ZS *zs) {
	struct tm *ltime;
	int tmp;

	if(zs == NULL)
		return;

	// Signature
	zs->stage_data[ 0] = ((ZS_SIGNATURE_CDH >>  0) && 0xFF);
	zs->stage_data[ 1] = ((ZS_SIGNATURE_CDH >>  8) && 0xFF);
	zs->stage_data[ 2] = ((ZS_SIGNATURE_CDH >> 16) && 0xFF);
	zs->stage_data[ 3] = ((ZS_SIGNATURE_CDH >> 24) && 0xFF);

	// Version Made By
	zs->stage_data[ 4] = 0x14;
	zs->stage_data[ 5] = 0x00;

	// Version To Extract
	zs->stage_data[ 6] = 0x0A;
	zs->stage_data[ 7] = 0x00;

	// General Purpose
	zs->stage_data[ 8] = 0x08;
	zs->stage_data[ 9] = 0x00;

	// Compression Method
	zs->stage_data[10] = 0x00;
	zs->stage_data[11] = 0x00;

	// Modification Time
	ltime = localtime(&zs->zsf->ftime);
	tmp = 0;
	tmp |= (ltime->tm_hour << 11);
	tmp |= (ltime->tm_min << 5);
	tmp |= (ltime->tm_sec / 2);
	zs->stage_data[12] = ((tmp >>  0) & 0xFF);
	zs->stage_data[13] = ((tmp >>  8) & 0xFF);

	// Modification Date
	tmp = 0;
	tmp |= ((ltime->tm_year - 80) << 9);
	tmp |= ((ltime->tm_mon + 1) << 5);
	tmp |= ltime->tm_mday;
	zs->stage_data[14] = ((tmp >>  0) & 0xFF);
	zs->stage_data[15] = ((tmp >>  8) & 0xFF);

	// CRC32
	zs->stage_data[16] = ((zs->zsf->crc32 >>  0) & 0xFF);
	zs->stage_data[17] = ((zs->zsf->crc32 >>  8) & 0xFF);
	zs->stage_data[18] = ((zs->zsf->crc32 >> 16) & 0xFF);
	zs->stage_data[19] = ((zs->zsf->crc32 >> 24) & 0xFF);

	// Compressed Size
	zs->stage_data[20] = ((zs->zsf->fsize >>  0) & 0xFF);
	zs->stage_data[21] = ((zs->zsf->fsize >>  8) & 0xFF);
	zs->stage_data[22] = ((zs->zsf->fsize >> 16) & 0xFF);
	zs->stage_data[23] = ((zs->zsf->fsize >> 24) & 0xFF);

	// Uncompressed Size
	zs->stage_data[24] = zs->stage_data[20];
	zs->stage_data[25] = zs->stage_data[21];
	zs->stage_data[26] = zs->stage_data[22];
	zs->stage_data[27] = zs->stage_data[23];

	// Filename Length
	zs->stage_data[28] = ((zs->zsf->lfname >>  0) & 0xFF);
	zs->stage_data[29] = ((zs->zsf->lfname >>  8) & 0xFF);

	// Extra Field Length
	zs->stage_data[30] = 0x00;
	zs->stage_data[31] = 0x00;

	// File Comment Length
	zs->stage_data[32] = 0x00;
	zs->stage_data[33] = 0x00;

	// Disk Number Start
	zs->stage_data[34] = 0x00;
	zs->stage_data[35] = 0x00;

	// Internal File Attributes
	zs->stage_data[36] = 0x00;
	zs->stage_data[37] = 0x00;

	// External File Attributes
	zs->stage_data[38] = 0x00;
	zs->stage_data[39] = 0x00;
	zs->stage_data[40] = 0x00;
	zs->stage_data[41] = 0x00;

	// Relative Offset Of LH
	zs->stage_data[42] = ((zs->zsf->offset >>  0) & 0xFF);
	zs->stage_data[43] = ((zs->zsf->offset >>  8) & 0xFF);
	zs->stage_data[44] = ((zs->zsf->offset >> 16) & 0xFF);
	zs->stage_data[45] = ((zs->zsf->offset >> 24) & 0xFF);

	return;
}

void zs_build_eocd(ZS *zs) {
	size_t size, offset;

	if(zs == NULL)
		return;

	// Signature
	zs->stage_data[ 0] = ((ZS_SIGNATURE_EOCD >>  0) && 0xFF);
	zs->stage_data[ 1] = ((ZS_SIGNATURE_EOCD >>  8) && 0xFF);
	zs->stage_data[ 2] = ((ZS_SIGNATURE_EOCD >> 16) && 0xFF);
	zs->stage_data[ 3] = ((ZS_SIGNATURE_EOCD >> 24) && 0xFF);

	// Number Of This Disk
	zs->stage_data[ 4] = 0x00;
	zs->stage_data[ 5] = 0x00;

	// #Disc With CD
	zs->stage_data[ 6] = 0x00;
	zs->stage_data[ 7] = 0x00;

	// #Entries Of This Disk
	zs->stage_data[ 8] = ((zs->zsd.nfiles >>  0) & 0xFF);
	zs->stage_data[ 9] = ((zs->zsd.nfiles >>  8) & 0xFF);

	// #Entries
	zs->stage_data[10] = zs->stage_data[ 8];
	zs->stage_data[11] = zs->stage_data[ 9];

	// Size Of The CD
	size = zs_get_cdsize(zs);
	zs->stage_data[12] = ((size >>  0) & 0xFF);
	zs->stage_data[13] = ((size >>  8) & 0xFF);
	zs->stage_data[14] = ((size >> 16) & 0xFF);
	zs->stage_data[15] = ((size >> 24) & 0xFF);

	// Offset Of The CD
	offset = zs_get_cdoffset(zs);
	zs->stage_data[16] = ((offset >>  0) & 0xFF);
	zs->stage_data[17] = ((offset >>  8) & 0xFF);
	zs->stage_data[18] = ((offset >> 16) & 0xFF);
	zs->stage_data[19] = ((offset >> 24) & 0xFF);

	// ZIP File Comment Length
	zs->stage_data[20] = 0x00;
	zs->stage_data[21] = 0x00;

	return;
}

size_t zs_get_cdsize(ZS *zs) {
	int size = 0;
	ZSFile *zsf;

	if(zs == NULL)
		return 0;

	zsf = zs->zsd.files;

	while(zsf != NULL) {
		size += ZS_LENGTH_CDH;
		size += zsf->lfname;

		zsf = zsf->next;
	}

	return size;
}

size_t zs_get_cdoffset(ZS *zs) {
	size_t offset = 0;
	ZSFile *zsf;

	if(zs == NULL)
		return 0;

	zsf = zs->zsd.files;

	while(zsf != NULL) {
		if(zsf->next == NULL) {
			offset = zsf->offset;
			break;
		}

		zsf = zsf->next;
	}

	offset += ZS_LENGTH_LFH;
	offset += zsf->lfname;
	offset += zsf->fsize;
	offset += ZS_LENGTH_LFD;

	return offset;
}
