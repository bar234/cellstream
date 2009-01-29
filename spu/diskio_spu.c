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


// This SPE code was inspired by the dmabench benchmark 
// that was included with the Cell SDK.

/* SPE code for the bandwidth benchmark. */
#include <spu_timer.h>
#include <stdio.h>
#include <string.h>
#include "MMGP_spu.h"

// Use the spu_timer clock read function
#define GET_TIME spu_clock_read()

// Used when the mask is already given
#define DMA_Wait2(mask)                    \
{                                           \
    mfc_write_tag_mask(mask);        \
    mfc_read_tag_status_all();              \
}

// Processes a chunk read from main memory, and immediately commits it back to main memory
#define	PROCESS_ONE_CHUNK(dmaid, localbuffer, remotebufferput, remotebufferget)		\
	commWaitStart = GET_TIME;														\
	DMA_Wait(dmaid);																\
	commWait += GET_TIME - commWaitStart;											\
	compStart = GET_TIME;														\
	outputbytes += extrawork(&localbuffer, e_size);									\
	comp += GET_TIME - compStart;											\
	mfc_put(&localbuffer, remotebufferput, e_size, dmaid, 0, 0);					\
	mfc_getf(&localbuffer, remotebufferget, e_size, dmaid, 0, 0);

#define MAX_BUFFERS 		3
#define MAX_BUF_SIZE		16384
#define TB 			79800000U
#define DO_WORK		1;

// Timing variables
unsigned long long time = 0, commWait = 0, comp = 0;
unsigned long long timeStart = 0, commWaitStart = 0, compStart = 0;

// The local SPU buffers
static char area[MAX_BUFFERS][MAX_BUF_SIZE] __attribute__ ((aligned (128)));

// Extra work to keep the SPE busy.
inline int extrawork(char *buffer, int number) {
#ifdef DO_WORK
	memset(buffer, '1', number);
#endif

	return number;
}

// Read in the buffer, set each integer to this node's
// rank, and then push it back out to the buffer
unsigned int FromMainToMain(unsigned long long buffer, int size, int blockSize){

	// offset variables bX for local destination vector
	unsigned int b0,b1;
	
	// variables offset oXX for remote data source 
	unsigned long long o0, o1, o2, o3;
	
	// offset for next iteration
	unsigned int offset;
	
	// number of bytes processed
	unsigned int outputbytes = 0;
	int offsetBytes = 0;
	
	// number of iterations
	int max_iterations, i;
		
	// Determine an appropriate block size
	int e_size = blockSize;
		
	// Initialize the local offsets
	b0=0;
	b1=e_size;
	
	// Initialize the remote offsets
	o0=0;
	o1=e_size;
	o2=e_size*2;
	o3=e_size*3;

	offset = o2;
	
	// Determine the maximum number of iterations
	max_iterations = (size) / (2*e_size);

	// Determine how many bytes are left over
	offsetBytes = size - (max_iterations-1)*e_size*2;
	
	
	// Start the timing
	spu_clock_start();
	timeStart = GET_TIME;
	
	mfc_get(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
	mfc_get(&area[0][b1], buffer+o1, e_size, 1, 0, 0);

	// The PROCESS_ONE_CHUNK macro uses 4 parameters...
	// 	0: DMA ID. Since this is double buffering we have a different ID for each buffer read/write
	// 	1: Local buffer to perform "work" on
	// 	2: Main memory address to commit the processed local buffer to.
	// 	3: Main memory address to read the next local buffer from, once the commit of the previous
	// 		buffer completes
	
	for(i = 0; i < max_iterations-1; i++) {

		PROCESS_ONE_CHUNK(0, area[0][b0], buffer+o0, buffer+o2);
		PROCESS_ONE_CHUNK(1, area[0][b1], buffer+o1, buffer+o3);
		
		// Move all the offsets forward for the next iteration
		o0=o0+offset;
	    o1=o1+offset;
	    o2=o2+offset;
	    o3=o3+offset;
	}
	
	for(i = 0; offsetBytes > e_size; offsetBytes -= e_size) {
		commWaitStart = GET_TIME;
		DMA_Wait(i)
		commWait += commWaitStart - GET_TIME;
		
		compStart = GET_TIME;
		outputbytes += extrawork(&area[0][b0], e_size);
		comp += compStart - GET_TIME;
		
		mfc_put(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
		
		if(offsetBytes - e_size*2 < e_size && offsetBytes - e_size*2 > 0) {
			mfc_getf(&area[0][b0], buffer+o2, offsetBytes - e_size*2, 0, 0, 0);
		}
		else if(offsetBytes - e_size*2 > 0) {
			mfc_getf(&area[0][b0], buffer+o2, e_size, 0, 0, 0);
		}
		
		
		i = i^1;
		
		if(b0 == b1) b0 = 0;
		else b0 = b1;
		
		o0 += e_size;
		o2 += e_size;
	}
	
	commWaitStart = GET_TIME;
	DMA_Wait(i)
	commWait += commWaitStart - GET_TIME;
	
	compStart = GET_TIME;
	outputbytes += extrawork(&area[0][b0], offsetBytes);
	comp += compStart - GET_TIME;
	
	mfc_put(&area[0][b0], buffer+o0, offsetBytes, 0, 0, 0);
	
	// Ensure all DMA operations are done
	commWaitStart = GET_TIME;
	DMA_Wait2(0xFF);
	commWait += commWaitStart - GET_TIME;
	
	time += GET_TIME - timeStart;
	
	return outputbytes;
}


int main(){

    int received;
    unsigned int totalsize = 0;
   
    /* MMGP call necessary for establishing 
     * all communication parameters between
     * this SPE trhead and the PPE */
    MMGP_exchange();

    spu_slih_register (MFC_DECREMENTER_EVENT, spu_clock_slih);
    
    do{

        /* MMGP call used for receiving the PPE starting signal */
        received = MMGP_SPE_wait();
        
       
        if (received == 1){
			Pass.cansend = 0;
			Pass.freebuffer = 0;
           
			totalsize += FromMainToMain(Pass.buffer, Pass.size, Pass.blockSize);

            MMGP_SPE_stop();
        }

        else if (received == -5) {
			Pass.bandwidth = (2*((double)totalsize)/1024.0/1024.0)/((double)time/(double)TB);
			//printf("SPU %d: %lf (%lf bytes, %lf seconds)\n", ID, Pass.bandwidth, 2.0*(double)totalsize, ((double)time/(double)TB));
			//printf("%lf,", Pass.bandwidth);
            /*printf("SPU: %f secs, %u bytes (%lfMB/sec   %lfGB/sec)\n",
					(double)time/TB, totalsize, 
					(2*((double)totalsize)/1024.0/1024.0)/((double)time/TB), 
					(2*((double)totalsize)/1024.0/1024.0/1024.0)/((double)time/TB));

			printf("SPU: Total Time: %lf seconds (%lf communication, %lf computation)\n", 
					(double)time/TB, 
					(double)commWait/TB, 
					(double)comp/TB);*/

            MMGP_SPE_stop();
            break;
        }
 
    }while(1);


    return 0;
}

