#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#define BLOCK_SIZE	16384
static char buf[BLOCK_SIZE];

static double time_sub_us(struct timespec *new, struct timespec *old)
{
	double a = old->tv_sec * 1000000.0 + old->tv_nsec / 1000.0;
	double b = new->tv_sec * 1000000.0 + new->tv_nsec / 1000.0;
	return (b - a);
}

int main(int argc, char **argv)
{
	int i, j;
	int fd;
	char *mem;
	int write = 0;
	unsigned int mem_addr = 0;
	unsigned int mem_size = BLOCK_SIZE;
	unsigned int test_pattern = 0x12345a5a;
	unsigned int test_size = BLOCK_SIZE;
	struct timespec old, new;
	double speed = 0.0;

	if (argc < 5) {
		printf("Usage:\n");
		printf("\t%s fill MemAddr MemSize TestPattern\n", argv[0]);
		printf("\t%s read MemAddr MemSize TestTotalSize\n", argv[0]);
		return 0;
	}

	sscanf(argv[2], "%x", &mem_addr);
	sscanf(argv[3], "%x", &mem_size);
	if (strncmp(argv[1], "fill", 4) == 0) {
		write = 1;
		sscanf(argv[4], "%x", &test_pattern);
		printf("write to %#x, %#x bytes, by %#x\n",
				mem_addr, mem_size, test_pattern);
		test_size = mem_size;
	} else {
		sscanf(argv[4], "%x", &test_size);
		printf("read from %#x, %#x bytes, total %#x bytes\n",
				mem_addr, mem_size, test_size);
	}

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) {
		perror("open /dev/mem error\n");
		return -1;
	}

	mem = mmap(0, mem_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, mem_addr);
	if (mem == MAP_FAILED) {
		perror("mmap failed\n");
		return -1;
	}

	clock_gettime(CLOCK_REALTIME, &old);
	if (write) {
		int count = mem_size / sizeof(unsigned int);
		for (i = 0; i < count; i += sizeof(unsigned int)) {
			unsigned int *p = (unsigned int *)(mem+i);
			*p = (test_pattern + i);
		}
	} else {
		int count = test_size / BLOCK_SIZE;
		unsigned int offset = 0;
		for (i = 0; i < count; i++) {
			//printf("%4d: read %#x, %#x bytes\n", i, offset, BLOCK_SIZE);
			memcpy(buf, mem+offset, BLOCK_SIZE);
			offset += BLOCK_SIZE;
			if (offset >= mem_size)
				offset = 0;
#if 0
			// dump
			for(j = 0; j < 128; j+=4) {
				unsigned int *p = (unsigned int *)&buf[j];
				if (j % 16 == 0)
					printf("\n");
				printf("%08x ", *p);
			}
			printf("\n");
#endif
		}
	}
	clock_gettime(CLOCK_REALTIME, &new);
	speed = (double)test_size * 1000.0 * 1000.0 / time_sub_us(&new, &old);
	speed = speed / 1024.0 / 1024.0;
	printf("process %#x bytes, speed %.2fM/s\n", test_size, speed);

	munmap(mem, BLOCK_SIZE);
	close(fd);
	return 0;
}

