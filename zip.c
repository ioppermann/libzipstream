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

	while((bytes = zs_write(zs, buf, sizeof(buf))) > -1) {
		fprintf(stderr, "%d\n", bytes);
		fwrite(buf, 1, bytes, stdout);
	}

	zs_free(zs);

	return 0;
}

ZS *zs_init(void) {
	ZS *zs;

	zs = (ZS *)calloc(1, sizeof(ZS));

	crc_init();

	return zs;
}

void zs_free(ZSDirectory *zs) {
	ZSFile *zsf, *pzsf;

	if(zs == NULL)
		return;

	zsf = zs->files;

	while(zsf != NULL) {
		free(zsf->path);
		free(zsf->fname);

		pzsf = zsf;
		zsf = zsf->next;

		free(pzsf);
	}

	free(zs);

	return;
}

int zs_add_file(ZS *zs, const char *path) {
	char *fname;
	ZSFile *zsf, *pzsf;
	struct stat sb;

	if(zs == NULL)
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

	zsf->crc32 = crc_start();

	if(zs->zsd.nfiles != 0) {
		pzsf = zs->zsd.files;

		while(pzsf->next != NULL) {
			pzsf = pzsf->next;
		}

		pzsf->next = zsf;
	}
	else
		zs->zsd.files = zsf;

	zs->zsd.nfiles++;

	return 0;
}

int zs_write_file(ZSFile *zsf, char *buf, int sbuf) {
	int i;
	int bytesread;
	int bufpos;

	if(zsf == NULL)
		return -1;

	bufpos = 0;

	// Local File Header
	if(zsf->c_lfheader != sizeof(zsf->lfheader)) {
		bytesread = sizeof(zsf->lfheader) - zsf->c_lfheader;
		if(sbuf < bytesread)
			bytesread = sbuf;

		for(i = 0; i < bytesread; i++) {
			buf[bufpos] = zsf->lfheader[i + zsf->c_lfheader];

			bufpos++;
		}

		zsf->c_lfheader += bytesread;

		return bufpos;
	}

	// Filename
	if(zsf->c_name != zsf->lfname) {
		bytesread = zsf->lfname - zsf->c_name;
		if(sbuf < bytesread)
			bytesread = sbuf;

		for(i = 0; i < bytesread; i++) {
			buf[bufpos] = zsf->fname[i + zsf->c_name];

			bufpos++;
		}

		zsf->c_name += bytesread;

		return bufpos;
	}

	// Uncompressed file data
	if(zsf->fsize == 0) {
		if(zsf->c_data == 0) {
			zsf->fp = fopen(zsf->path, "rb");
			if(zsf->fp == NULL)
				return -1;
		}

		bytesread = fread(buf, 1, sbuf, zsf->fp);
		zsf->c_data += bytesread;

		zsf->crc32 = crc_partial(zsf->crc32, buf, bytesread);

		if(bytesread < sbuf) {
			fclose(zsf->fp);
			zsf->fsize = zsf->c_data;

			zsf->crc32 = crc_finish(zsf->crc32);

			zs_build_descriptor(zsf);
		}

		return bytesread;
	}

	// Descriptor
	if(zsf->c_lfdescriptor != sizeof(zsf->lfdescriptor)) {
		bytesread = sizeof(zsf->lfdescriptor) - zsf->c_lfdescriptor;;
		if(sbuf < bytesread)
			bytesread = sbuf;

		for(i = 0; i < bytesread; i++) {
			buf[bufpos] = zsf->lfdescriptor[i + zsf->c_lfdescriptor];

			bufpos++;
		}

		zsf->c_lfdescriptor += bytesread;

		return bufpos;
	}

	return -1;
}

void zs_build_lfheader(ZSFile *zsf) {
	struct tm *ltime;
	int tmp;

	if(zsf == NULL)
		return;

	// Signature
	zsf->lfheader[ 0] = 0x50;
	zsf->lfheader[ 1] = 0x4b;
	zsf->lfheader[ 2] = 0x03;
	zsf->lfheader[ 3] = 0x04;

	// Version
	zsf->lfheader[ 4] = 0x0A;
	zsf->lfheader[ 5] = 0x00;

	// General Purpose
	zsf->lfheader[ 6] = 0x08;
	zsf->lfheader[ 7] = 0x00;

	// Compression Method
	zsf->lfheader[ 8] = 0x00;	// Store
	zsf->lfheader[ 9] = 0x00;

	// Modification Time
	ltime = localtime(&zsf->ftime);
	tmp = 0;
	tmp |= (ltime->tm_hour << 11);
	tmp |= (ltime->tm_min << 5);
	tmp |= (ltime->tm_sec / 2);
	zsf->lfheader[10] = ((tmp >> 0) & 0xFF);
	zsf->lfheader[11] = ((tmp >> 8) & 0xFF);

	// Modification Date
	tmp = 0;
	tmp |= ((ltime->tm_year - 80) << 9);
	tmp |= ((ltime->tm_mon + 1) << 5);
	tmp |= ltime->tm_mday;
	zsf->lfheader[12] = ((tmp >> 0) & 0xFF);
	zsf->lfheader[13] = ((tmp >> 8) & 0xFF);

	// CRC32
	zsf->lfheader[14] = 0x00;
	zsf->lfheader[15] = 0x00;
	zsf->lfheader[16] = 0x00;
	zsf->lfheader[17] = 0x00;

	// Compressed Size
	zsf->lfheader[18] = 0x00;
	zsf->lfheader[19] = 0x00;
	zsf->lfheader[20] = 0x00;
	zsf->lfheader[21] = 0x00;

	// Uncompressed Size
	zsf->lfheader[22] = 0x00;
	zsf->lfheader[23] = 0x00;
	zsf->lfheader[24] = 0x00;
	zsf->lfheader[25] = 0x00;

	// Filename Length
	zsf->lfheader[26] = ((zsf->lfname >> 0) & 0xFF);
	zsf->lfheader[27] = ((zsf->lfname >> 8) & 0xFF);

	// Extra Field Length
	zsf->lfheader[28] = 0x00;
	zsf->lfheader[29] = 0x00;

	return;
}

void zs_build_descriptor(ZSFile *zsf) {
	if(zsf == NULL)
		return;

	// Signature
	zsf->lfdescriptor[ 0] = 0x50;
	zsf->lfdescriptor[ 1] = 0x4b;
	zsf->lfdescriptor[ 2] = 0x07;
	zsf->lfdescriptor[ 3] = 0x08;

	// CRC32
	zsf->lfdescriptor[ 4] = ((zsf->crc32 >>  0) & 0xFF);
	zsf->lfdescriptor[ 5] = ((zsf->crc32 >>  8) & 0xFF);
	zsf->lfdescriptor[ 6] = ((zsf->crc32 >> 16) & 0xFF);
	zsf->lfdescriptor[ 7] = ((zsf->crc32 >> 24) & 0xFF);

	// Compressed Size
	zsf->lfdescriptor[ 8] = ((zsf->fsize >>  0) & 0xFF);
	zsf->lfdescriptor[ 9] = ((zsf->fsize >>  8) & 0xFF);
	zsf->lfdescriptor[10] = ((zsf->fsize >> 16) & 0xFF);
	zsf->lfdescriptor[11] = ((zsf->fsize >> 24) & 0xFF);

	// Uncompressed Size
	zsf->lfdescriptor[12] = zsf->lfdescriptor[ 8];
	zsf->lfdescriptor[13] = zsf->lfdescriptor[ 9];
	zsf->lfdescriptor[14] = zsf->lfdescriptor[10];
	zsf->lfdescriptor[15] = zsf->lfdescriptor[11];

	return;
}

void zs_build_cdheader(ZSFile *zsf) {
	struct tm *ltime;
	int tmp;

	if(zsf == NULL)
		return;

	// Signature
	zsf->cdheader[ 0] = 0x50;
	zsf->cdheader[ 1] = 0x4b;
	zsf->cdheader[ 2] = 0x01;
	zsf->cdheader[ 3] = 0x02;

	// Version Made By
	zsf->cdheader[ 4] = 0x14;
	zsf->cdheader[ 5] = 0x00;

	// Version To Extract
	zsf->cdheader[ 6] = 0x0A;
	zsf->cdheader[ 7] = 0x00;

	// General Purpose
	zsf->cdheader[ 8] = 0x08;
	zsf->cdheader[ 9] = 0x00;

	// Compression Method
	zsf->cdheader[10] = 0x00;
	zsf->cdheader[11] = 0x00;

	// Modification Time
	ltime = localtime(&zsf->ftime);
	tmp = 0;
	tmp |= (ltime->tm_hour << 11);
	tmp |= (ltime->tm_min << 5);
	tmp |= (ltime->tm_sec / 2);
	zsf->cdheader[12] = ((tmp >> 0) & 0xFF);
	zsf->cdheader[13] = ((tmp >> 8) & 0xFF);

	// Modification Date
	tmp = 0;
	tmp |= ((ltime->tm_year - 80) << 9);
	tmp |= ((ltime->tm_mon + 1) << 5);
	tmp |= ltime->tm_mday;
	zsf->cdheader[14] = ((tmp >> 0) & 0xFF);
	zsf->cdheader[15] = ((tmp >> 8) & 0xFF);

	// CRC32
	zsf->cdheader[16] = ((zsf->crc32 >>  0) & 0xFF);
	zsf->cdheader[17] = ((zsf->crc32 >>  8) & 0xFF);
	zsf->cdheader[18] = ((zsf->crc32 >> 16) & 0xFF);
	zsf->cdheader[19] = ((zsf->crc32 >> 24) & 0xFF);

	// Compressed Size
	zsf->cdheader[20] = ((zsf->fsize >>  0) & 0xFF);
	zsf->cdheader[21] = ((zsf->fsize >>  8) & 0xFF);
	zsf->cdheader[22] = ((zsf->fsize >> 16) & 0xFF);
	zsf->cdheader[23] = ((zsf->fsize >> 24) & 0xFF);

	// Uncompressed Size
	zsf->cdheader[24] = zsf->cdheader[20];
	zsf->cdheader[25] = zsf->cdheader[21];
	zsf->cdheader[26] = zsf->cdheader[22];
	zsf->cdheader[27] = zsf->cdheader[23];

	// Filename Length
	zsf->cdheader[28] = ((zsf->lfname >> 0) & 0xFF);
	zsf->cdheader[29] = ((zsf->lfname >> 8) & 0xFF);

	// Extra Field Length
	zsf->cdheader[30] = 0x00;
	zsf->cdheader[31] = 0x00;

	// File Comment Length
	zsf->cdheader[32] = 0x00;
	zsf->cdheader[33] = 0x00;

	// Disk Number Start
	zsf->cdheader[34] = 0x00;
	zsf->cdheader[35] = 0x00;

	// Internal File Attributes
	zsf->cdheader[36] = 0x00;
	zsf->cdheader[37] = 0x00;

	// External File Attributes
	zsf->cdheader[38] = 0x00;
	zsf->cdheader[39] = 0x00;
	zsf->cdheader[40] = 0x00;
	zsf->cdheader[41] = 0x00;

	// Relative Offset Of LH
	zsf->cdheader[42] = ((zsf->offset >>  0) & 0xFF);
	zsf->cdheader[43] = ((zsf->offset >>  8) & 0xFF);
	zsf->cdheader[44] = ((zsf->offset >> 16) & 0xFF);
	zsf->cdheader[45] = ((zsf->offset >> 24) & 0xFF);

	return;
}

void zs_build_eocd(ZSDirectory *zs) {
	size_t size, offset;

	if(zs == NULL)
		return;

	// Signature
	zs->eocd[ 0] = 0x50;
	zs->eocd[ 1] = 0x4b;
	zs->eocd[ 2] = 0x05;
	zs->eocd[ 3] = 0x06;

	// Number Of This Disk
	zs->eocd[ 4] = 0x00;
	zs->eocd[ 5] = 0x00;

	// #Disc With CD
	zs->eocd[ 6] = 0x00;
	zs->eocd[ 7] = 0x00;

	// #Entries Of This Disk
	zs->eocd[ 8] = ((zs->nfiles >> 0) & 0xFF);
	zs->eocd[ 9] = ((zs->nfiles >> 8) & 0xFF);

	// #Entries
	zs->eocd[10] = zs->eocd[ 8];
	zs->eocd[11] = zs->eocd[ 9];

	// Size Of The CD
	size = zs_get_cdsize(zs);
	zs->eocd[12] = ((size >>  0) & 0xFF);
	zs->eocd[13] = ((size >>  8) & 0xFF);
	zs->eocd[14] = ((size >> 16) & 0xFF);
	zs->eocd[15] = ((size >> 24) & 0xFF);

	// Offset Of The CD
	offset = zs_get_cdoffset(zs);
	zs->eocd[16] = ((offset >>  0) & 0xFF);
	zs->eocd[17] = ((offset >>  8) & 0xFF);
	zs->eocd[18] = ((offset >> 16) & 0xFF);
	zs->eocd[19] = ((offset >> 24) & 0xFF);

	// ZIP File Comment Length
	zs->eocd[20] = 0x00;
	zs->eocd[21] = 0x00;

	return;
}

size_t zs_get_cdsize(ZSDirectory *zs) {
	int size = 0;
	ZSFile *zsf;

	if(zs == NULL)
		return 0;

	zsf = zs->files;

	while(zsf != NULL) {
		size += zsf->lfname;
		size += sizeof(zsf->cdheader);

		zsf = zsf->next;
	}

	return size;
}

size_t zs_get_cdoffset(ZSDirectory *zs) {
	size_t offset = 0;
	ZSFile *zsf;

	if(zs == NULL)
		return 0;

	zsf = zs->files;

	while(zsf != NULL) {
		offset += sizeof(zsf->lfheader);
		offset += zsf->lfname;
		offset += zsf->fsize;
		offset += sizeof(zsf->lfdescriptor);

		zsf = zsf->next;
	}

	return offset;
}

void zs_build_offsets(ZSDirectory *zs) {
	size_t offset = 0;
	ZSFile *zsf;

	if(zs == NULL)
		return;

	zsf = zs->files;

	while(zsf != NULL) {
		zsf->offset = offset;

		offset += sizeof(zsf->lfheader);
		offset += zsf->lfname;
		offset += zsf->fsize;
		offset += sizeof(zsf->lfdescriptor);

		zs_build_cdheader(zsf);

		zsf = zsf->next;
	}

	return;
}

int zs_write_directory(ZSDirectory *zs, char *buf, int sbuf) {
	int bytesread;
	int bufpos;
	int nfiles;
	int i;
	ZSFile *zsf;

	zs_build_offsets(zs);
	zs_build_eocd(zs);

	bufpos = 0;

	// Local File Headers
	if(zs->c_nfiles != zs->nfiles) {
		nfiles = 0;
		zsf = zs->files;

		while(nfiles != zs->c_nfiles) {
			zsf = zsf->next;
			nfiles++;
		}

		if(zsf->c_cdheader != sizeof(zsf->cdheader)) {
			zsf->c_name = 0;
			bytesread = sizeof(zsf->cdheader) - zsf->c_cdheader;
			if(sbuf < bytesread)
				bytesread = sbuf;

			for(i = 0; i < bytesread; i++) {
				buf[bufpos] = zsf->cdheader[i + zsf->c_cdheader];

				bufpos++;
			}

			zsf->c_cdheader += bytesread;

			return bufpos;
		}

		// Filename
		if(zsf->c_name != zsf->lfname) {
			bytesread = zsf->lfname - zsf->c_name;
			if(sbuf < bytesread)
				bytesread = sbuf;

			for(i = 0; i < bytesread; i++) {
				buf[bufpos] = zsf->fname[i + zsf->c_name];

				bufpos++;
			}

			zsf->c_name += bytesread;

			if(zsf->c_name == zsf->lfname)
				zs->c_nfiles++;

			return bufpos;
		}
	}

	// End Of Central Directory
	if(zs->c_eocd != sizeof(zs->eocd)) {
		bytesread = sizeof(zs->eocd) - zs->c_eocd;
		if(sbuf < bytesread)
			bytesread = sbuf;

		for(i = 0; i < bytesread; i++) {
			buf[bufpos] = zs->eocd[i + zs->c_eocd];

			bufpos++;
		}

		zs->c_eocd += bytesread;

		return bufpos;
	}

	return -1;
}

