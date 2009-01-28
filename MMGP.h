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

#include <libsync.h>
#include <asm/unistd.h>
#include <unistd.h>
#include <malloc_align.h>

#define align_128 __attribute__((aligned(128)));
#define align_16 __attribute__((aligned(16)));
#define align_4 __attribute__((aligned(4)));

volatile unsigned int *shared;

void MMGP_init();

void (*MMGP_offload)(void);
void (*MMGP_start_SPE)(int i, int value);
void (*MMGP_wait_SPE)(int num);
void (*MMGP_reduction)(double *cont, int num);
void (*MMGP_prediction)(void);
void (*MMGP_create_threads)(int SPE_threads);
void (*MMGP_prediction)(void);

inline void send_mail(int id, unsigned int data);
int MMGP_get_SPE();
void MMGP_put_SPE(int num);

#define _sync    __asm__ __volatile("sync")

volatile unsigned long long ls_addr[20];

/* Structure used for PPE<->SPE signaling */
struct signal{

    int start, stop;
    unsigned long long total_time, loop_time;
    double result;
    int result_int;
};

// Structure used for PPE<->SPE parameter passing
struct pass{
	volatile unsigned long long freebuffer align_16;
    volatile unsigned long long buffer;

	volatile unsigned long long cansend align_16;
	volatile unsigned int size;
	volatile unsigned int blockSize;

	volatile unsigned long long freeBuffLocs[6];
	volatile unsigned long long canSendLocs[6];
	volatile unsigned long long speLocs[6];

	volatile double bandwidth;

} align_128;

volatile unsigned long long Pass[20];		// Parameters
volatile unsigned long long signal[20];		// Signals
volatile unsigned int taken[20];		// SPE Locks

int SPE_threads, NUM_SPE;
pid_t MMGP_pid;
int NumProc;
