#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include "csnappy.h"
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define MAX_INPUT_SIZE 10 * 1024 * 1024

static int do_decompress(FILE *ifile, FILE *ofile)
{
	char *ibuf, *obuf;
	uint32_t ilen, olen;
	int status, retval = 0;

	if (!(ibuf = (char *)malloc(MAX_INPUT_SIZE))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(ifile);
		retval = 4;
		goto out;
	}

	ilen = fread(ibuf, 1, MAX_INPUT_SIZE, ifile);
	if (!feof(ifile)) {
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(ibuf);
		fclose(ifile);
		retval = 5;
		goto out;
	}
	fclose(ifile);

	if ((status = csnappy_get_uncompressed_length(ibuf, ilen, &olen)) < 0) {
		fprintf(stderr, "snappy_get_uncompressed_length returned %d.\n", status);
		free(ibuf);
		retval = 6;
		goto out;
	}

	if (!(obuf = (char *)malloc(olen))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", (int)olen);
		free(ibuf);
		retval = 4;
		goto out;
	}

	status = csnappy_decompress(ibuf, ilen, obuf, olen);
	free(ibuf);
	if (status != CSNAPPY_E_OK) {
		fprintf(stderr, "snappy_decompress returned %d.\n", status);
		free(obuf);
		retval = 7;
		goto out;
	}

	fwrite(obuf, 1, olen, ofile);
	free(obuf);
out:
	fclose(ofile);
	return retval;
}

static int do_compress(FILE *ifile, FILE *ofile)
{
	char *ibuf, *obuf;
	void *working_memory;
	uint32_t ilen, olen, max_compressed_len;

	if (!(ibuf = (char *)malloc(MAX_INPUT_SIZE))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(ifile);
		fclose(ofile);
		return 4;
	}

	ilen = fread(ibuf, 1, MAX_INPUT_SIZE, ifile);
	if (!feof(ifile)) {
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(ibuf);
		fclose(ifile);
		fclose(ofile);
		return 5;
	}
	fclose(ifile);

	max_compressed_len = csnappy_max_compressed_length(ilen);
	if (!(obuf = (char*)malloc(max_compressed_len))) {
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", (int)max_compressed_len);
		free(ibuf);
		fclose(ofile);
		return 4;
	}

	if (!(working_memory = malloc(CSNAPPY_WORKMEM_BYTES))) {
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", CSNAPPY_WORKMEM_BYTES);
		free(ibuf);
		fclose(ofile);
		return 4;
	}

	csnappy_compress(ibuf, ilen, obuf, &olen,
			working_memory, CSNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	free(ibuf);
	free(working_memory);

	fwrite(obuf, 1, olen, ofile);
	fclose(ofile);
	free(obuf);
	return 0;
}

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)


static void segfault_handler(int signum) {
	if (signum == SIGSEGV) {
		printf("compression overwrites out buffer\n");
		exit(EXIT_SUCCESS);
	}
}

int do_selftest_compression(void)
{
	struct sigaction sa;
	char *obuf, *ibuf, *workmem;
	FILE *ifile;
	long PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	uint32_t olen = 0;
	uint32_t ilen = PAGE_SIZE + 100;

	obuf = (char*)mmap(NULL, PAGE_SIZE * 2,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (obuf == MAP_FAILED)
		handle_error("mmap");
	if (mprotect(obuf + PAGE_SIZE, PAGE_SIZE, PROT_NONE))
		handle_error("mprotect");
	if (!(ibuf = (char*)malloc(ilen)))
		handle_error("malloc");
	if (!(ifile = fopen("/dev/urandom", "rb")))
		handle_error("fopen");
	if (fread(ibuf, 1, ilen, ifile) < ilen)
		handle_error("fread");
	if (fclose(ifile))
		handle_error("fclose");
	if (!(workmem = (char*)malloc(CSNAPPY_WORKMEM_BYTES)))
		handle_error("malloc");
	sa.sa_handler = segfault_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGSEGV, &sa, NULL);
	csnappy_compress(ibuf, ilen, obuf, &olen,
			workmem, CSNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	fprintf(stderr, "ERROR: csnappy_compress did not segfault when should have!\n");
	if (munmap(obuf, PAGE_SIZE * 2))
		handle_error("munmap");
	free(workmem);
	free(ibuf);
	return EXIT_FAILURE;
}

static const char fake[] = "\x32\xc4\x66\x6f\x6f\x6f\x6f\x6f\x6f";
int do_selftest_decompression(void)
{
	char *obuf, *ibuf, *workmem;
	FILE *ifile;
	int ret;
	long PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	uint32_t ilen = PAGE_SIZE + 100;
	uint32_t olen = csnappy_max_compressed_length(ilen);
	if (!(obuf = (char*)malloc(olen)))
		handle_error("malloc");
	if (!(ibuf = (char*)malloc(ilen)))
		handle_error("malloc");
	if (!(ifile = fopen("/dev/urandom", "rb")))
		handle_error("fopen");
	if (fread(ibuf, 1, ilen, ifile) < ilen)
		handle_error("fread");
	if (fclose(ifile))
		handle_error("fclose");
	if (!(workmem = (char*)malloc(CSNAPPY_WORKMEM_BYTES)))
		handle_error("malloc");
	csnappy_compress(ibuf, ilen, obuf, &olen,
			workmem, CSNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	free(workmem);
	free(ibuf);
	ibuf = obuf;
	ilen = olen;
	olen = PAGE_SIZE;
	obuf = (char*)mmap(NULL, PAGE_SIZE * 2,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (obuf == MAP_FAILED)
		handle_error("mmap");
	if (mprotect(obuf + PAGE_SIZE, PAGE_SIZE, PROT_NONE))
		handle_error("mprotect");
	ret = csnappy_decompress(ibuf, ilen, obuf, olen);
	if (ret != CSNAPPY_E_OUTPUT_INSUF) {
		fprintf(stderr, "snappy_decompress returned %d.\n", ret);
		exit(EXIT_FAILURE);
	}
	ret = csnappy_decompress_noheader(ibuf + 2, ilen - 2, obuf, &olen);
	if (ret != CSNAPPY_E_OUTPUT_OVERRUN) {
		fprintf(stderr, "csnappy_decompress_noheader returned %d.\n", ret);
		exit(EXIT_FAILURE);
	}
	free(ibuf);
	if (munmap(obuf, PAGE_SIZE * 2))
		handle_error("munmap");

	olen = 50;
	if (!(obuf = (char*)malloc(olen)))
		handle_error("malloc");
	ret = csnappy_decompress(fake, 9, obuf, olen);
	if (ret == CSNAPPY_E_OK) {
		fprintf(stderr, "csnappy_decompress, stream cut off mid literal: %d\n", ret);
		exit(EXIT_FAILURE);
	}
	ret = csnappy_decompress_noheader(fake + 1, 8, obuf, &olen);
	if (ret == CSNAPPY_E_OK) {
		fprintf(stderr, "csnappy_decompress_noheader, stream cut off mid literal: %d\n", ret);
		exit(EXIT_FAILURE);
	}
	free(obuf);
	return 0;
}

int main(int argc, char * const argv[])
{
	int c;
	int decompress = 0, files = 1;
	int selftest_compression = 0, selftest_decompression = 0;
	const char *ifile_name, *ofile_name;
	FILE *ifile, *ofile;

	while((c = getopt(argc, argv, "S:dc")) != -1) {
		switch (c) {
		case 'S':
			switch (optarg[0]) {
			case 'c':
				selftest_compression = 1;
				break;
			case 'd':
				selftest_decompression = 1;
				break;
			default:
				goto usage;
			}
			break;
		case 'd':
			decompress = 1;
			break;
		case 'c':
			files = 0;
			break;
		default:
			goto usage;
		}
	}
	if (selftest_compression)
		return do_selftest_compression();
	if (selftest_decompression)
		return do_selftest_decompression();
	ifile = stdin;
	ofile = stdout;
	if (files) {
		if (optind > argc - 2)
			goto usage;
		ifile_name = argv[optind];
		ofile_name = argv[optind + 1];
		if (!(ifile = fopen(ifile_name, "rb"))) {
			perror("fopen of ifile_name");
			return 2;
		}
		if (!(ofile = fopen(ofile_name, "wb"))) {
			perror("fopen of ofile_name");
			return 3;
		}
	}
	if (decompress)
		return do_decompress(ifile, ofile);
	else
		return do_compress(ifile, ofile);
usage:
	fprintf(stderr,
	"Usage:\n"
	"cl_tester [-d] infile outfile\t-\t[de]compress infile to outfile.\n"
	"cl_tester [-d] -c\t\t-\t[de]compress stdin to stdout.\n"
	"cl_tester -S c\t\t\t-\tSelf-test compression.\n"
	"cl_tester -S d\t\t\t-\tSelf-test decompression.\n");
	return 1;
}
