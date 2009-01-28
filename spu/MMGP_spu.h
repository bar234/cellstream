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

#include <spu_mfcio.h>

#define align_128 __attribute__((aligned(128)))

/* Macro used for main memory <-> LS synchronization */
#define DMA_Wait(TAG_ID)                    \
{                                           \
    mfc_write_tag_mask(1<<(TAG_ID));        \
    mfc_read_tag_status_all();              \
}                     


/* User defined structure used for exchanging data
 * between the PPE and this SPE thread */
struct pass{
	volatile unsigned long long freebuffer;
    volatile unsigned long long buffer;

	volatile unsigned long long cansend;
	volatile unsigned int size;
	volatile unsigned int blockSize;
	
	volatile unsigned long long freeBuffLocs[6];
	volatile unsigned long long speLocs[6];

	volatile double	bandwidth;
	
} __attribute__((aligned(128)));


/* Structure used for PPE <-> SPE communication */
struct signal{
    int start, stop;
    unsigned long long total_time,loop_time;
    double result;
};
volatile struct signal signal __attribute__((aligned(128)));

int ID; // SPE thread id
int SPE_threads; // Each SPE trhead knows the total number of SPE threads in use
volatile struct pass Pass __attribute__((aligned(128))); // User defined structure used for PPE <-> SPE comm.


/* Function used for establishing the memory
 * regions in LS, used for PPE <-> SPE 
 * communication */
inline void MMGP_exchange(){

    SPE_threads = spu_read_in_mbox();
    ID = spu_read_in_mbox();
    
    spu_write_out_mbox((unsigned int)&Pass);
    spu_write_out_mbox((unsigned int)&signal);

}

/* Function used for waiting for the PPE signal */
inline int MMGP_SPE_wait(){

        while (signal.start==0);

        return signal.start;
}

/* Function used for signaling the PPE */
inline void MMGP_SPE_stop(){
    signal.start=0;
    spu_dsync();
    signal.stop=1;
}


