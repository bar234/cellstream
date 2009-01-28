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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <libsync.h>
#include "MMGP.h"
#include <stdio.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <cbe_mfc.h>
#include <malloc_align.h>
#include <linux/unistd.h>

#include "atomic.h"

#define MAX_NUM_SPUS        16

extern spe_program_handle_t diskio_spu;

spe_context_ptr_t   spe_id[MAX_NUM_SPUS];
spe_mfc_command_area_t *mfc_ps_area[MAX_NUM_SPUS];
spe_spu_control_area_t *mbox_ps_area[MAX_NUM_SPUS];
spe_sig_notify_1_area_t *sig_notify_ps_area[MAX_NUM_SPUS];
spe_mssync_area_t *mssync_ps_area[MAX_NUM_SPUS];
int myhead;

typedef struct ppu_pthread_data {
	spe_context_ptr_t spuid;
	pthread_t pthread;
} ppu_pthread_data_t;

void *ppu_pthread_function(void *arg) {
	ppu_pthread_data_t *datap = (ppu_pthread_data_t *)arg;
	int rc;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	if ((rc = spe_context_run(datap->spuid, &entry, 0, NULL, NULL, NULL)) < 0) {
		fprintf (stderr, "Failed spe_context_run(rc=%d, errno=%d, strerror=%s)\n", rc, errno, strerror(errno));
		exit (1);
	}
	pthread_exit(NULL);
}


/* Sending mail to an SPE. Parameters:
 * id - id of the targeting SPE,
 * data - 32 bit data that is sent. */
inline void send_mail(int i, unsigned int data){
	_spe_in_mbox_write(mbox_ps_area[i],data);
}

/* Creates SPE trheads, parameters:
 * 1. SPE_trheads, number of SPE threads to be created */
void _create_threads(int SPE_threads){

	int i,j,rc;
	ppu_pthread_data_t data[MAX_NUM_SPUS];

	// If the number SPE threads is larger than the number of SPEs 
	if (SPE_threads>NUM_SPE){
		printf("Error: Too many SPEs requested (%d). Only %d are available.\n",SPE_threads,NUM_SPE);
		exit(0);
	}
	
	/* Forking SPE threads */
	for(i=0; i<SPE_threads; i++){

		/* Create context */
		if ((data[i].spuid = spe_context_create (SPE_MAP_PS, NULL)) == NULL) {
			fprintf (stderr, "Failed spe_context_create(errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
      
		/* Load program */
		if ((rc = spe_program_load (data[i].spuid, &diskio_spu)) != 0) {
			fprintf (stderr, "Failed spe_program_load(errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
      
		/* Create thread */
		if ((rc = pthread_create (&data[i].pthread, NULL, &ppu_pthread_function, &data[i])) != 0) {
			fprintf (stderr, "Failed pthread_create(errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
		
		/* Get the direct problem state addresses */
		mfc_ps_area[i] = spe_ps_area_get(data[i].spuid, SPE_MFC_COMMAND_AREA);
		if (mfc_ps_area[i] == NULL) {
			fprintf (stderr, "Failed spe_ps_area_get - MFC_COMMAND (errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
		mbox_ps_area[i] = spe_ps_area_get(data[i].spuid, SPE_CONTROL_AREA);
		if (mbox_ps_area[i] == NULL) {
			fprintf (stderr, "Failed spe_ps_area_get - CONTROL (errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
		sig_notify_ps_area[i] = spe_ps_area_get(data[i].spuid, SPE_SIG_NOTIFY_1_AREA);
		if (sig_notify_ps_area[i] == NULL) {
			fprintf (stderr, "Failed spe_ps_area_get - SIG_NOTIFY (errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}
		mssync_ps_area[i] = spe_ps_area_get(data[i].spuid, SPE_MSSYNC_AREA);
		if (mssync_ps_area[i] == NULL) {
			fprintf (stderr, "Failed spe_ps_area_get - MSSYNC (errno=%d strerror=%s)\n", errno, strerror(errno));
			exit (1);
		}

		send_mail(i,(unsigned int) SPE_threads);
		send_mail(i,i);
	}
		
	/* Getting the LS addresses of all SPE threads */
	for(i=0; i<SPE_threads; i++)
		ls_addr[i] = (unsigned long long) spe_ls_area_get(data[i].spuid);
	
	/* Getting the addresses of the communication parameters of the SPE threads */
	for(i=0; i<SPE_threads; i++){
		Pass[i] = _spe_out_mbox_read(mbox_ps_area[i]);
		Pass[i] += ls_addr[i];
		
		signal[i] = _spe_out_mbox_read(mbox_ps_area[i]);
		signal[i] += ls_addr[i];
	}
	
	// Distribute the pass locations to all spes
	for(i=0; i<SPE_threads; i++) {
		for(j=0; j<SPE_threads; j++) {
			((struct pass *)Pass[i])->freeBuffLocs[j] = (unsigned long long)&((struct pass *)Pass[j])->freebuffer;
			((struct pass *)Pass[i])->canSendLocs[j] = (unsigned long long)&((struct pass *)Pass[j])->cansend;
			((struct pass *)Pass[i])->speLocs[j] = ls_addr[j];
		}
	}
}

/* Signal an SPE to start performing work, parameters:
 * 1. num - the SPE number (the work can be distributed 
 *          across multiple SPEs) ,
 * 2. value - values sent to an SPE in order to specify
 *            which function should be executed (multiple
 *            SPE functions can reside in the same SPE
 *            module) */
inline void _start_SPE(int num, int value){

    /* Send starting signal to an SPE,
     * before that set signal.stop to 0 */
    ((struct signal *)signal[num])->stop=0;
    _sync;
    ((struct signal *)signal[num])->start=value; 

}

inline void yield(){    
    sched_yield();
}

/* The function called before each off-loading region.
 * This function is used to measure the off-loading time */
inline void _offload(){

}


/* The same as _wait_SPET(), just without the 
 * timing instructions */
inline void _wait_SPE(int num){
	while (((struct signal *)signal[num])->stop==0)
		yield();
}

// A thread safe way to grab an SPE for use 
int MMGP_get_SPE() {
	int i = 0;
	
	for(i=0; i<SPE_threads; i++) {
		if(__xchg_u32(&taken[i], 1)==0)
			return i;
	}
	
	return -1;
}

// Free up an SPE for use by the same or another thread
void MMGP_put_SPE(int num){
	taken[num] = 0;
}

inline void _empty(){}

void MMGP_init(){

    MMGP_pid = getpid();

	NUM_SPE = spe_cpu_info_get(SPE_COUNT_PHYSICAL_SPES, -1);

    /* In the sampling phase use the functions
     * with the timing instructions, othervise use the
     * functions without the timing instructions 
     * (to avoid the overhead) */
        MMGP_offload = &_empty;
        MMGP_prediction = &_empty;
        MMGP_wait_SPE = &_wait_SPE;
        MMGP_start_SPE = &_start_SPE;
        MMGP_create_threads = &_create_threads;
        
}

