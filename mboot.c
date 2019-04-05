/* mboot.c - unpack and repack Intel boot.img for Android
**
** Based on https://github.com/osm0sis/mboot_py/blob/master/mboot.py
**
** Copyright 2014 Jocelyn Falempe (Intel Corporation)
** Copyright 2019 Chris Renshaw (osm0sis @ xda-developers)
**                Shaka Huang (shakalaca @ xda-developers / ASUS ZenTalk)
**
** This program is free software; you can redistribute it and/or modify it
** under the terms and conditions of the GNU General Public License,
** version 2, as published by the Free Software Foundation.
**
** This program is distributed in the hope it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
** more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

char *directory = "./";
char *filename = "boot.img";

int usage(int val)
{
	fprintf(stderr,
		"Usage: mboot.py [-u] [-f FILE] [-d DIR]\n\n"
		"Unpack an Intel boot image into separate files, OR,\n"
		"pack a directory with kernel/ramdisk/bootstub into an Intel boot image.\n\n"
		"Options:\n"
		"  -h, --help            show this help message and exit\n"
		"  -u, --unpack          split boot image into kernel, ramdisk, bootstub, etc.\n"
		"  -f, --file FILE       use FILE to unpack/repack (default: boot.img)\n"
		"  -d, --dir DIR         use DIR to unpack/repack (default: ./)\n"
	);
	return val;
}

int check_byte(FILE *f, int size, int min)
{
	int origsize = size;
	unsigned char *tmp = malloc(size);
	unsigned char *skip = malloc(1);

	// try skipping first byte if \x00 to hopefully avoid false positives with isalnum()
	if (size > 1) {
		if (fread(skip, 1, 1, f)) {};
		if (!memcmp(skip, "\x00", 1)) {
			size = size - 1;
		} else {
			fseek(f, -1, SEEK_CUR);
		}
	}

	if (fread(tmp, size, 1, f)) {};
	fseek(f, -origsize, SEEK_CUR);

	int bytes = isalnum(*tmp);
	//printf("%4d: %d\n", ftell(f), bytes);

	// add custom fault tolerance to try and avoid false positives with isalnum()
	if (bytes > min && bytes < origsize) {
		return 1;
	} else {
		return 0;
	}
}

void write_buffer(FILE *f, int size, char *name)
{
	char outpath[PATH_MAX];
	unsigned char *buffer = malloc(size);

	sprintf(outpath, "%s/%s", directory, name);
	FILE *t = fopen(outpath, "wb");

	if (fread(buffer, size, 1, f)) {};
	fwrite(buffer, size, 1, t);
	fclose(t);
}

void write_string(char *string, char *name)
{
	char outpath[PATH_MAX];

	sprintf(outpath, "%s/%s", directory, name);
	FILE *t = fopen(outpath, "w");

	fwrite(string, strlen(string), 1, t);
	fclose(t);
}

int unpack() 
{
	FILE *f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "mboot: cannot open input file '%s': %s\n", filename, strerror(errno));
		return 1;
	}

	// header is 512 bytes but may rarely not exist on some devices
	if (!check_byte(f, 4, 1)) {
		write_buffer(f, 512, "hdr");
	}
	int hdr_size = ftell(f);
	printf("header size   %d\n", hdr_size);

	// header may have 480, 728 or 1024 bytes of signature appended on some devices
	int sig_deltas[] = { 0, 480, 248, 296 };
	int i;
	for (i = 0; i < (sizeof(sig_deltas) / sizeof(sig_deltas[0])); i++) {
		fseek(f, sig_deltas[i], SEEK_CUR);
		if (check_byte(f, 4, 1)) {
			break;
		}
	}
	int sig_size = ftell(f) - hdr_size;
	if (sig_size > 0) {
		fseek(f, -sig_size, SEEK_CUR);
		write_buffer(f, sig_size, "sig");
	}
	printf("sig size      %d\n", sig_size);

	// cmdline is up to 1024 bytes padded with \x00
	char *cmdline[1024];
	if (fread(cmdline, 1024, 1, f)) {};
	write_string((char *)cmdline, "cmdline.txt");

	// image info is the next 16 bytes padded out to 3072 bytes
	uint8_t kernel_size_buffer[4];
	if (fread(kernel_size_buffer, 4, 1, f)) {};
	uint8_t ramdisk_size_buffer[4];
	if (fread(ramdisk_size_buffer, 4, 1, f)) {};
	write_buffer(f, 8, "parameter");
	fseek(f, 3072 - 16, SEEK_CUR);

	// bootstub is 4096 bytes but can be 8192 bytes on some devices
	fseek(f, 4096, SEEK_CUR);
	if (check_byte(f, 2, 0)) {
		fseek(f, 4096, SEEK_CUR);
	}
	int bootstub_size = ftell(f) - hdr_size - sig_size - 4096;
	fseek(f, -bootstub_size, SEEK_CUR);
	write_buffer(f, bootstub_size, "bootstub");
	printf("bootstub size %d\n", bootstub_size);

	uint32_t kernel_size = *(uint32_t *)kernel_size_buffer;
	if (kernel_size < 500000 || kernel_size > 15000000) {
		fprintf(stderr, "mboot: unpacking error: kernel size likely wrong");
		return 1;
	}
	write_buffer(f, kernel_size, "kernel");
	printf("kernel size   %d\n", kernel_size);

	uint32_t ramdisk_size = *(uint32_t *)ramdisk_size_buffer;
	if (ramdisk_size < 10000 || ramdisk_size > 300000000) {
		fprintf(stderr, "mboot: unpacking error: ramdisk size likely wrong");
		return 1;
	}
	write_buffer(f, ramdisk_size, "ramdisk.cpio.gz");
	printf("ramdisk size  %d\n", ramdisk_size);

	fclose(f);
	return 0;
}

void *read_file(char *name, unsigned *_size)
{
	char inpath[PATH_MAX];

	sprintf(inpath, "%s/%s", directory, name);
	FILE *t = fopen(inpath, "rb");
	if (!t) {
		return 0;
	}
	fseek(t, 0, SEEK_END);
	int size = ftell(t);

	unsigned char *data = malloc(size);
	fseek(t, 0, SEEK_SET);
	if (fread(data, size, 1, t)) {};
	fwrite(data, size, 1, t);
	fclose(t);

	if (_size) {
		*_size = size;
	}
	return data;
}

int pack() 
{
	uint32_t hdr_size = 0;
	void *hdr_data = read_file("hdr", &hdr_size);

	uint32_t sig_size = 0;
	void *sig_data = read_file("sig", &sig_size);

	char *required_file[] = { "cmdline.txt", "parameter", "bootstub", "kernel", "ramdisk.cpio.gz" };
	uint32_t required_size[5];
	void *required_data[5] = { 0, 0, 0, 0, 0 };
	int i;
	for (i = 0; i < (sizeof(required_file) / sizeof(required_file[0])); i++) {
		required_data[i] = read_file(required_file[i], &required_size[i]);
		if (!required_data[i]) {
			fprintf(stderr, "mboot: cannot open input file '%s': %s\n", required_file[i], strerror(errno));
			return 1;
		}
	}

	FILE *f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "mboot: cannot open output file '%s': %s\n", filename, strerror(errno));
		return 1;
	}

	// calculate image size and size of padding to next full 512 byte sector
	int img_size = hdr_size + sig_size + 4096 + required_size[2] + required_size[3] + required_size[4];
	int padding_size = 512 - (img_size % 512) < 512 ? 512 - (img_size % 512) : 0;

	unsigned char *bootimg = malloc(img_size + padding_size);

	// add header if present
	if (hdr_data) {
		memcpy(bootimg, hdr_data, hdr_size);
	}

	// add signature if present and add parameter padding magic for signed image
	if (sig_data) {
		memcpy(bootimg + hdr_size, sig_data, sig_size);
		memcpy(bootimg + (hdr_size + sig_size + 1024 + 16), "\xBD\x02\xBD\x02\xBD\x12\xBD\x12", 8);
	//  adjust header imgtype based on signature presence
	} else if (hdr_data) {
		unsigned char *imgtype_buffer = malloc(4);
		memcpy(imgtype_buffer, bootimg + 52, 4);
		uint32_t imgtype = *(uint32_t *)imgtype_buffer + 1;
		memcpy(bootimg + 52, &imgtype, 4);
	}

	// add cmdline, image info (kernel and ramdisk sizes), and parameter to their 4096 byte block
	memcpy(bootimg + (hdr_size + sig_size), required_data[0], required_size[0]);
	memcpy(bootimg + (hdr_size + sig_size + 1024), &required_size[3], sizeof(required_size[3]));
	memcpy(bootimg + (hdr_size + sig_size + 1024 + 4), &required_size[4], sizeof(required_size[4]));
	memcpy(bootimg + (hdr_size + sig_size + 1024 + 8), required_data[1], required_size[1]);

	// add bootstub, kernel and ramdisk
	memcpy(bootimg + (hdr_size + sig_size + 4096), required_data[2], required_size[2]);
	memcpy(bootimg + (hdr_size + sig_size + 4096 + required_size[2]), required_data[3], required_size[3]);
	memcpy(bootimg + (hdr_size + sig_size + 4096 + required_size[2] + required_size[3]), required_data[4], required_size[4]);

	// add trailing padding
	memset(bootimg + img_size, (int)'\xFF', padding_size);

	// update sector count and xor checksum in header
	if (hdr_data) {
		uint32_t sectors = ((img_size + padding_size) / 512 - 1);
		memcpy(bootimg + 48, &sectors, 4);

		uint8_t xor = 0;
		unsigned char *hdr_calc = malloc(56);
		memcpy(hdr_calc, bootimg, 56);
		memcpy(hdr_calc + 7, "\x00", 1);
		for (i = 0; i < 56; i++) {
			xor ^= hdr_calc[i];
		}
		memcpy(bootimg + 7, &xor, 1);
	}

	fwrite(bootimg, img_size + padding_size, 1, f);
	fclose(f);
	free(bootimg);
	return 0;
}

int main(int argc, char **argv)
{
	int unpackbool = 0;

	argc--;
	argv++;
	while (argc > 0) {
		char *arg = argv[0];
		if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			return usage(0);
		} else if (!strcmp(arg, "-u") || !strcmp(arg, "--unpack")) {
			unpackbool = 1;
			argc -= 1;
			argv += 1;
		} else if (argc >= 2) {
			char *val = argv[1];
			argc -= 2;
			argv += 2;
			if (!strcmp(arg, "-f") || !strcmp(arg, "--file")) {
				filename = val;
			} else if (!strcmp(arg, "-d") || !strcmp(arg, "--dir")) {
				directory = val;
			} else {
				return usage(1);
			}
		} else {
			return usage(1);
		}
	}

	struct stat st;
	if (stat(directory, &st) == (-1)) {
		fprintf(stderr, "mboot: cannot access '%s': %s\n", directory, strerror(errno));
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "mboot: cannot access '%s': Is not a directory\n", directory);
		return 1;
	}

	if (unpackbool) {
		return unpack();
	} else {
		return pack();
	}
}