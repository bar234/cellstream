/*
	Copyright 2009 Benjamin Rose (bar234@vt.edu)
	This file is part of CellStream

	CellStream is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CellStream is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CellStream.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <libmisc.h>
#include <sys/time.h>

#include "MMGP.h"

#define BUFFERSIZE 16777216

#define ELAPSED_TIME(start, end) \
	end.tv_sec - start.tv_sec + 0.000001*(end.tv_usec - start.tv_usec)

int g_Myrank = 0;
int g_Size = 0;
void *bufs[3];
unsigned int blockSize;

pthread_mutex_t bufs_lock[3];

// Determine when each thread can access each buffer
bool read_ready[3]= {true, true, true};
bool offload_ready[3] = {false, false, false};
bool write_ready[3] = {false, false, false};

// Conditional variables
pthread_cond_t read_cond;
pthread_cond_t offload_cond;
pthread_cond_t write_cond;

// Input and Output File locations
const char file_location[] = "/tmp/192M_file";
const char output_file_loc[] = "/tmp/outputfile";

// Offload the current buffer parameter to the SPUs
void offloadBuffer(void* buffer, int size, int spes) {
	int i;
	int speids[6];										// Store the IDs of the SPEs generate by MMGP
	
	unsigned int bufferChunk = size / spes;				// Break the buffer into one piece per SPE

	unsigned int chunkOffset = bufferChunk % 128;		// Make each piece a multiple of 128 to make sure SPEs
														// read whole cache lines.

	unsigned int bufferOffset;							// Size of the last SPE's work to compensate for the
														// adjusted piece size in the other SPEs
	
	//char *charBuf = (char *)buffer;					// Used when error checking

	// Adjust the chunksize to be a multiple of 4k
	bufferChunk -= chunkOffset;

	// Determine how much data will be left over and give it to the last SPE
	bufferOffset = size - bufferChunk*(spes-1);

	//memset(buffer, 0, size);
	// Tell each SPE what their parameters are
	for(i = 0; i < spes; i++) {
		// Get an SPE reserved
		speids[i] = MMGP_get_SPE();
		
		// Pass the starting address
		((struct pass *)Pass[speids[i]])->buffer = (unsigned long long)(buffer + bufferChunk*i);
		
		// The size is either the predetermined chunk size or the offset in the case of the last SPE
		if(i != spes-1)
			((struct pass *)Pass[speids[i]])->size = bufferChunk;
		else
			((struct pass *)Pass[speids[i]])->size = bufferOffset;
		
		// Block size that DMA transfers should use
		((struct pass *)Pass[speids[i]])->blockSize = blockSize;
	}
	
	// Fire off all the SPEs
	for(i = spes-1; i >= 0; i--)
		MMGP_start_SPE(speids[i],1);

	// Wait for the SPEs to finish and give the SPE back to MMGP
	for(i = 0; i < spes; i++) {
		MMGP_wait_SPE(speids[i]);
		MMGP_put_SPE(speids[i]);
	}

	// Error checking
	/*for(i = 0; i < size; i++) {
		if(charBuf[i] != '1')
			printf("%d: Error (%c)\n", i, charBuf[i]);
	}*/
}

// The thread assigned to reading in the data from file to the memory buffers
void* reading(void * arg)
{
	int input;
	struct timeval start, end;
	double read_wait = 0.0;
	unsigned int totalbufs = 0;
	int readcount = 0;
	unsigned int filesize = *((int *)arg);

	int i = 0;
	
	// Open the input file
	if ((int) (input = open(file_location, O_RDONLY)) == 0) {
		perror("Opening input file");
		exit(1);
	}
	
	// Loop until all of the file's data has been read
	while ((unsigned int)readcount < filesize) {

		// Make sure I have permission to fill buffer i
		pthread_mutex_lock(&bufs_lock[i]);
		while (!read_ready[i]) {
			pthread_cond_wait(&read_cond, &bufs_lock[i]);
		}
		read_ready[i] = false;

		// Read in 1 buffer's worth of data from disk
		gettimeofday(&start, NULL);
		unsigned int oneread = read(input, bufs[i], BUFFERSIZE);
		gettimeofday(&end, NULL);
		read_wait += ELAPSED_TIME(start, end);

		if (readcount < 0) {
			printf("Error (%d): %s\n", errno, strerror(errno));
			exit(1);
		}
		else if (oneread < BUFFERSIZE) {
			printf("Error: tried to read %d, read %d.\n", BUFFERSIZE, oneread);
			exit(1);
		}
		readcount += oneread;
		
		// Signal the buffer is ready to be offloaded to the SPEs by the offloading thread
		offload_ready[i] = true;
		pthread_cond_signal(&offload_cond);
		pthread_mutex_unlock(&bufs_lock[i]);

		// Increment the buffers read counter
		totalbufs++;

		i = ((i+1) % 3);
	}
	close(input);

	//printf("Read %d bytes from disk at %lfMB/sec\n",
	//       readcount, (readcount / 1024.0 / 1024.0) / read_wait);

	return 0;
}

// Thread assigned to manage SPE work on a buffer
void* offloading(void *arg)
{
	struct timeval start, end;
	double offload_wait = 0;
	unsigned int totalbufs = 0;
	unsigned int filesize = *((int *)arg);

	int i = 0, j=0;
	
	// Loop until the whole file has been offloaded
	while ((totalbufs * BUFFERSIZE) < filesize) {
		// Wait for the buffer to be free and make sure its our turn to process it
		pthread_mutex_lock(&bufs_lock[i]);
		while (!offload_ready[i]) {
			pthread_cond_wait(&offload_cond, &bufs_lock[i]);
		}
		offload_ready[i] = false;

		// Send the buffer to the SPEs to be processed
		gettimeofday(&start, NULL);
		offloadBuffer(bufs[i], BUFFERSIZE, SPE_threads);
		gettimeofday(&end, NULL);
		offload_wait += ELAPSED_TIME(start, end);

		totalbufs++;

		// Notify the sending thread that this buffer can be sent to the network
		write_ready[i] = true;
		pthread_cond_signal(&write_cond);
		pthread_mutex_unlock(&bufs_lock[i]);

		i = ((i+1) % 3);
	}

	for(j=0; j < SPE_threads; j++) {
		MMGP_start_SPE(j,-5);
		MMGP_wait_SPE(j);
	}
	
	/*printf("Streamed %d bytes through the SPUs at %lfMB/sec (%lfGB/sec)\n",
	       totalbufs * BUFFERSIZE,
	       ((totalbufs * BUFFERSIZE) / 1024.0 / 1024.0) / (offload_wait), 
	       ((totalbufs * BUFFERSIZE) / 1024.0 / 1024.0 / 1024.0) / (offload_wait));*/
	// Multiplied by two since there are two DMA operations on each buffer, a GET and a PUT.
	printf("%lf", ((totalbufs * BUFFERSIZE * 2) / 1024.0 / 1024.0) / (offload_wait));

	return 0;
}

// Thread assigned to writing the output of the SPE work to an output file
void* writing(void *arg)
{
	int output;
	struct timeval start, end;
	double write_wait = 0;
	unsigned int totalbufs = 0;
	unsigned int writecount = 0;
	unsigned int filesize = *((int *)arg);

	int i = 0;

	// Open the output file
	if ((int) (output = open(output_file_loc, O_CREAT | O_WRONLY)) == 0) {
		perror("Opening output file");
		exit(1);
	}

	// Loop until every buffer has been written to disk
	while ((totalbufs * BUFFERSIZE) < filesize) {

		// Make sure the buffer is free and its our turn to use it
		pthread_mutex_lock(&bufs_lock[i]);
		while (!write_ready[i]) {
			pthread_cond_wait(&write_cond, &bufs_lock[i]);
		}
		write_ready[i] = false;
	
		// Write the buffer back to disk
		gettimeofday(&start, NULL);
		int onewrite = write(output, bufs[i], BUFFERSIZE);
		gettimeofday(&end, NULL);
		write_wait += ELAPSED_TIME(start, end);
		
		if (onewrite < 0) {
			printf("Writing error(%d): %s\n", errno, strerror(errno));
			exit(1);
		}
		else if (onewrite < BUFFERSIZE) {
			printf("Error: tried to write %u, only wrote %d\n", BUFFERSIZE, onewrite);
			exit(1);
		}
		writecount += onewrite;
		
		//printf("Buffer %u wrote to disk. (%uMB/%uMB)\n", totalbufs, writecount / 1024 / 1024,
		//       filesize / 1024 / 1024);

		// Notify the reading thread that this buffer is now free
		read_ready[i] = true;
		pthread_cond_signal(&read_cond);
		pthread_mutex_unlock(&bufs_lock[i]);

		totalbufs++;
		i = ((i+1) % 3);
	}

	//printf("Wrote %u bytes to disk at %lfMB/sec\n",
	//       writecount, (writecount / 1024.0 / 1024.0) / (write_wait));

	return 0;
}

int main(int argc, char *argv[])
{		
	// Buffer sizes
	int input;
	int i = 0;
	pthread_t threads[3];
	
	// Read in the command line parameters
	if(argc < 3) {
		printf("Missing command line arguments!\n");
		exit(1);
	}
	
	// Get the number of SPEs to use and DMA blocksize
	SPE_threads = atoi(argv[1]);
	blockSize = atoi(argv[2]);
	
	// Check for at least 1 SPE. MMGP will make sure not too many are requested
	if(SPE_threads < 1) {
		printf("Error: Too few SPEs requested. Need at least 1!\n");
		exit(-1);
	}
	
	// Check that the blocksize is a multiple of 16 but not larger than 16k
	if(blockSize < 16) {
		printf("Error: Block size too small! Must be at least 16 bytes and a multiple of 16 up to 16384 bytes.\n");
		exit(-1);
	}
	else if(blockSize > 16384) {
		printf("Error: Block size too large! Must be at least 16 bytes and a multiple of 16 up to 16384 bytes.\n");
		exit(-1);
	}
	else if(blockSize % 16 != 0) {
		printf("Error: Block size not a multiple of 16! Must be at least 16 bytes and a multiple of 16 up to 16384 bytes.\n");
		exit(-1);
	}
	
	// Open the input file
	if ((int) (input = open(file_location, O_RDONLY)) == 0) {
		perror("Opening input file");
		exit(1);
	}

	// Determine its size
	unsigned int filesize = (unsigned int) lseek(input, 0, SEEK_END);
	lseek(input, 0, SEEK_SET);
	close(input);

	// Init MMGP
	MMGP_init();

	// Create the SPE threads
	MMGP_create_threads(SPE_threads);
	
	// Allocate the three buffers that are passed between the pthreads.
	// Align them to page sizes (4k = 2^12)
	bufs[0] = (void *)malloc_align(BUFFERSIZE, 12);
	bufs[1] = (void *)malloc_align(BUFFERSIZE, 12);
	bufs[2] = (void *)malloc_align(BUFFERSIZE, 12);

	// Initialize the mutex locks
	for (i = 0; i < 3; ++i) {
		pthread_mutex_init(&bufs_lock[i], NULL);
	}

	// Initialize the conditional variables (to determine whos turn it is to work on each buffer)
	pthread_cond_init(&read_cond, NULL);
	pthread_cond_init(&offload_cond, NULL);
	pthread_cond_init(&write_cond, NULL);

	// Spawn the threads
	pthread_create(&threads[0], NULL, reading, &filesize);
	pthread_create(&threads[1], NULL, offloading, &filesize);
	pthread_create(&threads[2], NULL, writing, &filesize);

	// Wait for them to finish
	for (i = 0; i < 3; ++i) {
		pthread_join(threads[i], NULL);
	}
		
	// Free the buffers
	free_align(bufs[0]);
	free_align(bufs[1]);
	free_align(bufs[2]);
	
	return 0;
}
