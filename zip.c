#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef WITH_DEFLATE
	#include <zlib.h>
#endif
#ifdef WITH_BZIP2
	#include <bzlib.h>
#endif

#include "zipstream.h"
#include "zip.h"
#include "crc32.h"

void zs_init(ZS *zs) {
	if(zs == NULL)
		return;

	memset(zs, 0, sizeof(ZS));

	return;
}

void zs_free(ZS *zs) {
	ZSFile *zsf, *pzsf;

	if(zs == NULL)
		return;

	if(zs->fp != NULL)
		fclose(zs->fp);

	zsf = zs->zsd.files;

	while(zsf != NULL) {
		free(zsf->fpath);
		free(zsf->fname);

		pzsf = zsf;
		zsf = zsf->next;

		free(pzsf);
	}

	zs_init(zs);

	return;
}

int zs_add_file(ZS *zs, const char *targetpath, const char *sourcepath, int compression, int level) {
	ZSFile *zsf, *pzsf;
	struct stat sb;

	if(zs == NULL)
		return -1;

	if(zs->finalized == 1)
		return -1;

	if(level < ZS_COMPRESS_LEVEL_DEFAULT || level > ZS_COMPRESS_LEVEL_SIZE)
		level = ZS_COMPRESS_LEVEL_DEFAULT;

	switch(compression) {
		case ZS_COMPRESS_NONE:
			break;
#ifdef WITH_DEFLATE
		case ZS_COMPRESS_DEFLATE:
			if(level == ZS_COMPRESS_LEVEL_SPEED)
				level = Z_BEST_SPEED;
			else if(level == ZS_COMPRESS_LEVEL_SIZE)
				level = Z_BEST_COMPRESSION;
			else
				level = Z_DEFAULT_COMPRESSION;

			break;
#endif
#ifdef WITH_BZIP2
		case ZS_COMPRESS_BZIP2:
			if(level == ZS_COMPRESS_LEVEL_SPEED)
				level = 1;
			else if(level == ZS_COMPRESS_LEVEL_SIZE)
				level = 9;
			else
				level = 6;

			break;
#endif
		default:
			return -1;
	}

	if(stat(sourcepath, &sb) == -1)
		return -1;

	if(!S_ISREG(sb.st_mode))
		return -1;

	zsf = (ZSFile *)calloc(1, sizeof(ZSFile));
	if(zsf == NULL)
		return -1;

	zsf->fpath = strdup(sourcepath);
	if(zsf->fpath == NULL) {
		free(zsf);

		return -1;
	}

	zsf->fname = strdup(targetpath);
	if(zsf->fname == NULL) {
		free(zsf->fpath);
		free(zsf);

		return -1;
	}

	zsf->lfname = strlen(zsf->fname);

	zsf->ftime = sb.st_mtime;
	zsf->fsize = sb.st_size;
	zsf->fsize_compressed = 0;

	zsf->compression = compression;
	switch(zsf->compression) {
		case ZS_COMPRESS_NONE:
			zsf->version = 10;
			break;
#ifdef WITH_DEFLATE
		case ZS_COMPRESS_DEFLATE:
			zsf->level = level;
			zsf->version = 20;
			break;
#endif
#ifdef WITH_BZIP2
		case ZS_COMPRESS_BZIP2:
			zsf->level = level;
			zsf->version = 46;
			break;
#endif
	}

	if(zs->zsd.nfiles != 0) {
		pzsf = zs->zsd.files;

		while(pzsf->next != NULL)
			pzsf = pzsf->next;

		pzsf->next = zsf;
		zsf->prev = pzsf;
	}
	else
		zs->zsd.files = zsf;

	zs->zsd.nfiles++;

	return 0;
}

int zs_read(ZS *zs, char *buf, int sbuf) {
	int bytes;

	if(zs == NULL)
		return -1;

	zs->finalized = 1;

	bytes = 0;

	do {
		zs_stager(zs);

		if(zs->stage == ERROR)
			return -1;

		if(zs->stage == FIN)
			return bytes;

		switch(zs->stage) {
			case LF_HEADER:
				bytes += zs_write_stagedata(zs, &buf[bytes], sbuf - bytes, ZS_LENGTH_LFH);
				break;
			case LF_DESCRIPTOR:
				bytes += zs_write_stagedata(zs, &buf[bytes], sbuf - bytes, ZS_LENGTH_LFD);
				break;
			case CD_HEADER:
				bytes += zs_write_stagedata(zs, &buf[bytes], sbuf - bytes, ZS_LENGTH_CDH);
				break;
			case EOCD:
				bytes += zs_write_stagedata(zs, &buf[bytes], sbuf - bytes, ZS_LENGTH_EOCD);
				break;
			case LF_NAME:
			case CD_NAME:
				bytes += zs_write_filename(zs, &buf[bytes], sbuf - bytes);
				break;
			case LF_DATA:
				bytes += zs->write_filedata(zs, &buf[bytes], sbuf - bytes);
				break;
			default:
				return -1;
		}
	} while(bytes != sbuf);

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

int zs_write_filedata_none(ZS *zs, char *buf, int sbuf) {
	int bytesread;

	bytesread = fread(buf, 1, sbuf, zs->fp);
	zs->stage_pos += bytesread;

	zs->zsf->crc32 = crc_partial(zs->zsf->crc32, buf, bytesread);

	zs->zsf->fsize_compressed += bytesread;

	if(ferror(zs->fp) || feof(zs->fp)) {	// ERROR or EOF
		zs->zsf->fsize = zs->stage_pos;
		zs->zsf->fsize_compressed = zs->stage_pos;

		zs->zsf->completed = 1;
	}

	return bytesread;
}

#ifdef WITH_DEFLATE
int zs_write_filedata_deflate(ZS *zs, char *buf, int sbuf) {
	int bytesread;

	if(zs->deflate.init == 0) {
		zs->deflate.init = 1;
		zs->deflate.avail_in = 0;
		zs->deflate.flush = Z_NO_FLUSH;

		zs->deflate.strm.zalloc = Z_NULL;
		zs->deflate.strm.zfree = Z_NULL;
		zs->deflate.strm.opaque = Z_NULL;

		deflateInit2(&zs->deflate.strm, zs->deflate.level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
	}

	bytesread = 0;

	zs->deflate.strm.avail_out = sbuf;
	zs->deflate.strm.next_out = buf;

	do {
		deflate(&zs->deflate.strm, zs->deflate.flush);

		bytesread = sbuf - zs->deflate.strm.avail_out;

		if(bytesread == 0) {
			zs->stage_pos += zs->deflate.avail_in;

			if(zs->deflate.flush == Z_FINISH) {
				deflateEnd(&zs->deflate.strm);

				zs->zsf->fsize = zs->stage_pos;
				zs->zsf->completed = 1;

				zs->deflate.init = 0;

				break;
			}

			zs->deflate.avail_in = fread(zs->deflate.in, 1, sizeof(zs->deflate.in), zs->fp);

			zs->zsf->crc32 = crc_partial(zs->zsf->crc32, zs->deflate.in, zs->deflate.avail_in);

			zs->deflate.strm.avail_in = zs->deflate.avail_in;
			zs->deflate.strm.next_in = zs->deflate.in;

			zs->deflate.flush = feof(zs->fp) ? Z_FINISH : Z_NO_FLUSH;
		}
	} while(bytesread == 0);

	zs->zsf->fsize_compressed += bytesread;

	return bytesread;
}
#endif

#ifdef WITH_BZIP2
int zs_write_filedata_bzip2(ZS *zs, char *buf, int sbuf) {
	int bytesread;

	if(zs->bzip2.init == 0) {
		zs->bzip2.init = 1;
		zs->bzip2.avail_in = 0;
		zs->bzip2.flush = Z_NO_FLUSH;

		zs->bzip2.strm.bzalloc = Z_NULL;
		zs->bzip2.strm.bzfree = Z_NULL;
		zs->bzip2.strm.opaque = Z_NULL;

		BZ2_bzCompressInit(&zs->bzip2.strm, zs->bzip2.level, 0, 30);
	}

	bytesread = 0;

	zs->bzip2.strm.avail_out = sbuf;
	zs->bzip2.strm.next_out = buf;

	do {
		BZ2_bzCompress(&zs->bzip2.strm, zs->bzip2.flush);

		bytesread = sbuf - zs->bzip2.strm.avail_out;

		if(bytesread == 0) {
			zs->stage_pos += zs->bzip2.avail_in;

			if(zs->bzip2.flush == BZ_FINISH) {
				BZ2_bzCompressEnd(&zs->bzip2.strm);

				zs->zsf->fsize = zs->stage_pos;
				zs->zsf->completed = 1;

				zs->bzip2.init = 0;

				break;
			}

			zs->bzip2.avail_in = fread(zs->bzip2.in, 1, sizeof(zs->bzip2.in), zs->fp);

			zs->zsf->crc32 = crc_partial(zs->zsf->crc32, zs->bzip2.in, zs->bzip2.avail_in);

			zs->bzip2.strm.avail_in = zs->bzip2.avail_in;
			zs->bzip2.strm.next_in = zs->bzip2.in;

			zs->bzip2.flush = feof(zs->fp) ? BZ_FINISH : BZ_RUN;
		}
	} while(bytesread == 0);

	zs->zsf->fsize_compressed += bytesread;

	return bytesread;
}
#endif

void zs_stager(ZS *zs) {
	if(zs->stage == NONE) {
		zs->zsf = zs->zsd.files;

		zs->stage = LF_HEADER;
		zs->stage_pos = 0;
	}

stager_top:
	if(zs->stage == LF_HEADER) {
		if(zs->zsf == NULL) {
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

			switch(zs->zsf->compression) {
				case ZS_COMPRESS_NONE:
					zs->write_filedata = zs_write_filedata_none;
					break;
#ifdef WITH_DEFLATE
				case ZS_COMPRESS_DEFLATE:
					zs->deflate.level = zs->zsf->level;
					zs->write_filedata = zs_write_filedata_deflate;
					break;
#endif
#ifdef WITH_BZIP2
				case ZS_COMPRESS_BZIP2:
					zs->bzip2.level = zs->zsf->level;
					zs->write_filedata = zs_write_filedata_bzip2;
					break;
#endif
			}
		}
	}

	if(zs->stage == LF_DATA) {
		if(zs->zsf->completed == 1) {
			zs->stage = LF_DESCRIPTOR;
			zs->stage_pos = 0;

			fclose(zs->fp);

			zs->zsf->crc32 = crc_finish(zs->zsf->crc32);

			if(zs->zsf->prev != NULL) {
				zs->zsf->offset = zs->zsf->prev->offset;

				zs->zsf->offset += ZS_LENGTH_LFH;
				zs->zsf->offset += zs->zsf->prev->lfname;
				zs->zsf->offset += zs->zsf->prev->fsize_compressed;
				zs->zsf->offset += ZS_LENGTH_LFD;
			}
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

			goto stager_top;
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

			goto stager_top;
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
	zs->stage_data[ 0] = 0x50;
	zs->stage_data[ 1] = 0x4b;
	zs->stage_data[ 2] = 0x03;
	zs->stage_data[ 3] = 0x04;

	// Version
	zs->stage_data[ 4] = ((zs->zsf->version >>  0) & 0xFF);
	zs->stage_data[ 5] = ((zs->zsf->version >>  8) & 0xFF);

	// General Purpose
	zs->stage_data[ 6] = 0x08;	// Bit3 : CRC32, file sizes unknown at this time
	zs->stage_data[ 7] = 0x00;

	// Compression Method
	zs->stage_data[ 8] = ((zs->zsf->compression >>  0) & 0xFF);
	zs->stage_data[ 9] = ((zs->zsf->compression >>  8) & 0xFF);

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
	zs->stage_data[ 0] = 0x50;
	zs->stage_data[ 1] = 0x4b;
	zs->stage_data[ 2] = 0x07;
	zs->stage_data[ 3] = 0x08;

	// CRC32
	zs->stage_data[ 4] = ((zs->zsf->crc32 >>  0) & 0xFF);
	zs->stage_data[ 5] = ((zs->zsf->crc32 >>  8) & 0xFF);
	zs->stage_data[ 6] = ((zs->zsf->crc32 >> 16) & 0xFF);
	zs->stage_data[ 7] = ((zs->zsf->crc32 >> 24) & 0xFF);

	// Compressed Size
	zs->stage_data[ 8] = ((zs->zsf->fsize_compressed >>  0) & 0xFF);
	zs->stage_data[ 9] = ((zs->zsf->fsize_compressed >>  8) & 0xFF);
	zs->stage_data[10] = ((zs->zsf->fsize_compressed >> 16) & 0xFF);
	zs->stage_data[11] = ((zs->zsf->fsize_compressed >> 24) & 0xFF);

	// Uncompressed Size
	zs->stage_data[12] = ((zs->zsf->fsize >>  0) & 0xFF);
	zs->stage_data[13] = ((zs->zsf->fsize >>  8) & 0xFF);
	zs->stage_data[14] = ((zs->zsf->fsize >> 16) & 0xFF);
	zs->stage_data[15] = ((zs->zsf->fsize >> 24) & 0xFF);

	return;
}

void zs_build_cdh(ZS *zs) {
	struct tm *ltime;
	int tmp;

	if(zs == NULL)
		return;

	// Signature
	zs->stage_data[ 0] = 0x50;
	zs->stage_data[ 1] = 0x4b;
	zs->stage_data[ 2] = 0x01;
	zs->stage_data[ 3] = 0x02;

	// Version Made By
	zs->stage_data[ 4] = 0x14;
	zs->stage_data[ 5] = 0x00;

	// Version To Extract
	zs->stage_data[ 6] = ((zs->zsf->version >>  0) & 0xFF);
	zs->stage_data[ 7] = ((zs->zsf->version >>  8) & 0xFF);

	// General Purpose
	zs->stage_data[ 8] = 0x08;	// Bit3 : CRC32, file sizes unknown at this time (ignored here)
	zs->stage_data[ 9] = 0x00;

	// Compression Method
	zs->stage_data[10] = ((zs->zsf->compression >>  0) & 0xFF);
	zs->stage_data[11] = ((zs->zsf->compression >>  8) & 0xFF);

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
	zs->stage_data[20] = ((zs->zsf->fsize_compressed >>  0) & 0xFF);
	zs->stage_data[21] = ((zs->zsf->fsize_compressed >>  8) & 0xFF);
	zs->stage_data[22] = ((zs->zsf->fsize_compressed >> 16) & 0xFF);
	zs->stage_data[23] = ((zs->zsf->fsize_compressed >> 24) & 0xFF);

	// Uncompressed Size
	zs->stage_data[24] = ((zs->zsf->fsize >>  0) & 0xFF);
	zs->stage_data[25] = ((zs->zsf->fsize >>  8) & 0xFF);
	zs->stage_data[26] = ((zs->zsf->fsize >> 16) & 0xFF);
	zs->stage_data[27] = ((zs->zsf->fsize >> 24) & 0xFF);

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
	zs->stage_data[ 0] = 0x50;
	zs->stage_data[ 1] = 0x4b;
	zs->stage_data[ 2] = 0x05;
	zs->stage_data[ 3] = 0x06;

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
	offset += zsf->fsize_compressed;
	offset += ZS_LENGTH_LFD;

	return offset;
}
