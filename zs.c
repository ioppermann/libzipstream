#include <stdio.h>

#include "zipstream.h"

int main(int argc, char **argv) {
	int bytes;
	char buf[1024];
	ZS zs;

	zs_init(&zs);

	zs_add_file(&zs, "bla/foobar.mp4", "data/foobar.mp4", ZS_COMPRESS_NONE, ZS_COMPRESS_LEVEL_DEFAULT);
	zs_add_file(&zs, "bla/1171032474.mpg", "data/1171032474.mpg", ZS_COMPRESS_BZIP2, ZS_COMPRESS_LEVEL_SIZE);
	zs_add_file(&zs, "bla/asnumber.zip", "data/asnumber.zip", ZS_COMPRESS_DEFLATE, ZS_COMPRESS_LEVEL_SIZE);

	while((bytes = zs_read(&zs, buf, sizeof(buf))) > 0)
		fwrite(buf, 1, bytes, stdout);

	zs_free(&zs);

	return 0;
}
