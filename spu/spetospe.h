// Old code used for SPE to SPE communications. Out of date so it has been thrown in this header file


// Processes a chunk read from main memory, and passes it on to the next SPE. The next SPE will set my Pass.cansend
// variable to an address on the next SPE where I can pass the buffer to when it is ready. Once I finish sending the
// buffer, I set the next SPE's Pass.freebuffer value to 0 so it knows the buffer has been filled.
#define	PROCESS_ONE_CHUNK_FROM_MEM_TO_SPE(dmaid, localbuffer, localbufferget, localbufferput, remotebufferget)		\
	DMA_Wait(dmaid);												\
	mfc_get(&localbufferget, remotebufferget, e_size, dmaid, 0, 0);	\
	SEND_ONE_CHUNK_TO_NEXT_SPE(0, localbufferput)						\
	outputbytes += extrawork(&localbuffer, e_size);								

// Sends localbuffer to the next SPE (ordered by rank ID)
#define SEND_ONE_CHUNK_TO_NEXT_SPE(dmaid, localbuffer)	\
	DMA_Wait(dmaid);												\
	while(Pass.cansend == 0);										\
	speBuffer = Pass.cansend;									\
	Pass.cansend = 0;												\
	mfc_put(&localbuffer, speBuffer, e_size, dmaid, 0, 0);			\
	mfc_putf(&zero, Pass.freeBuffLocs[ID+1], 8, dmaid, 0, 0);

// Send the address of localbuffer to the previous SPE so it knows where to send data
#define START_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid, localbuffer) \
	Pass.freebuffer = (unsigned long)&localbuffer;									\
	Pass.freebuffer += Pass.speLocs[ID];								\
	mfc_putf(&Pass.freebuffer, Pass.freeBuffLocs[ID-1] + 0x10, 8, dmaid, 0, 0);

// Wait for the previous SPE to finish sending to the buffer assigned in the
// last call to START_GET_ONE_CHUNK_FROM_PREV_SPE
#define WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid) \
	while(Pass.freebuffer != 0);									\
	DMA_Wait(dmaid)
	
// Processes a chunk of data from the previous SPE and then commits it back to main memory
#define	PROCESS_ONE_CHUNK_FROM_SPE_TO_MEM(dmaid, localbuffer, localbufferget, localbufferput, remotebufferput)		\
	WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid);												\
	mfc_put(&localbufferput, remotebufferput, e_size, dmaid, 0, 0);	\
	START_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid, localbufferget)	\
	outputbytes += extrawork(&localbuffer, e_size);

// Processes a chunk of data from the previous SPE and then passes it on to the next SPE
#define	PROCESS_ONE_CHUNK_FROM_SPE_TO_SPE(dmaid, localbuffer, localbufferget, localbufferput)		\
	WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid);									\
	SEND_ONE_CHUNK_TO_NEXT_SPE(dmaid, localbufferput);							\
	START_GET_ONE_CHUNK_FROM_PREV_SPE(dmaid, localbufferget);					\
	outputbytes += extrawork(&localbuffer, e_size);								\



unsigned int FromMainToSPE(unsigned int buffer, int size){

	// offset variables bX for local destination vector
	unsigned int b0,b1,b2,b3;
	// variables offset oXX for remote data source 
	unsigned int o0, o1;
	// offset for next iteration
	unsigned int offset;
	
	// number of bytes processed
	unsigned int outputbytes = 0;
	
	// number of iterations
	int max_iterations, offsetbytes, i;
	
	// Used when sending data to another SPE
	unsigned long long speBuffer = 0;
	unsigned long long zero = 0;
	
	// Determine the block size
	int e_size = 16384;
		
	// Initialize the local offsets
	b0=0;   
	b1=e_size;
	b2=e_size*2;   
		
	// Initialize the remote offsets
	o0=0;
    o1=e_size;

	// Set the offset
	offset = e_size*2;
	
	// Determine the maximum number of iterations
	max_iterations = (size) / (2*e_size) - 1;
	
	// Determine how many bytes are left over
	offsetbytes = size - (max_iterations+1)*e_size*2;
		
	// Do one iteration, which includes filling 16 buffers,
	// processing 15 buffers, and sending 14 buffers to the next SPE
	// (the last processing and two put operations are taken
	// care of after the main loop)
	
	// Start filling the buffers
	if(max_iterations > 0) {
		// Since anything not memory-to-memory uses triple buffering, we have to
		// process the first buffer while reading in the second during this
		// initialization stage
		mfc_get(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
		DMA_Wait(0);
	
		mfc_get(&area[0][b1], buffer+o1, e_size, 0, 0, 0);
		outputbytes += extrawork(&area[0][b0], e_size);
	
		o0=o0+offset;
	    o1=o1+offset;
	}
		
	for(i = 0; i < max_iterations; i++) {
		
		// This macro uses 5 parameters...
		// 	0: DMA ID. For triple buffering everyone uses the same DMA ID
		// 	1: Local buffer to perform "work" on
		// 	2: Local buffer to read data into from main memory
		// 	3: Local buffer to send to the SPE with a rank one higher than this one's
		// 	4: Main memory address to read into the local buffer specified in parameter 2
	
		PROCESS_ONE_CHUNK_FROM_MEM_TO_SPE(0, area[0][b1], area[0][b2], area[0][b0], buffer+o0);
		
		PROCESS_ONE_CHUNK_FROM_MEM_TO_SPE(0, area[0][b2], area[0][b0], area[0][b1], buffer+o1);

		o0=o0+offset;
	    o1=o1+offset;

		// Update the local buffer pointers
		b3 = b0;
		b0 = b2;
		b2 = b1;
		b1 = b3;
	}
	
	if(max_iterations > 0) {
		// Process and put the last 2 buffers back into memory
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0]);
			
		outputbytes += extrawork(&area[0][b1], e_size);

		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b1]);
	}
	
	if(offsetbytes > 16384) {
		// If we have more than 16384 extra bytes to process, read in the first
		// 16384, and then while we read in the last part of the extra bytes
		// process the first 16384
		mfc_get(&area[0][b0], buffer+o0, e_size, 1, 0, 0);
		DMA_Wait(1)
		mfc_get(&area[0][b1], buffer+o1, e_size, 1, 0, 0);
		outputbytes += extrawork(&area[0][b0], e_size);

		// Send the first 16384 extra bytes to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0]);
	
		DMA_Wait(1)
		
		// Process the last extra bytes
		outputbytes += extrawork(&area[0][b1], offsetbytes - 16384);
		
		// Send the last extra bytes to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b1]);
	}
	else if(offsetbytes > 0) {
		// We have less than 16384 extra bytes, so just read in 16384
		mfc_get(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
		
		DMA_Wait(0)
		
		// Process these extra bytes
		outputbytes += extrawork(&area[0][b0], offsetbytes);
		
		// Send them to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0]);
		
	}
	
	// Ensure all DMA operations are done
	DMA_Wait2(0xFF);
	
	return outputbytes;
}
unsigned int FromSPEToSPE(int size){

	// offset variables bX for local destination vector
	unsigned int b0,b1,b2,b3;
	// variables offset oXX for remote data source 
	unsigned int o0, o1;
	// offset for next iteration
	unsigned int offset;
	
	// number of bytes processed
	unsigned int outputbytes = 0;
	
	// number of iterations
	int max_iterations, offsetbytes, i;

	// Used when sending data to another SPE
	unsigned long long speBuffer;
	unsigned long long zero = 0;

	// Determine the block size
	int e_size = 16384;
		
	// Initialize the local offsets
	b0=e_size*0;   
	b1=e_size*1;
	b2=e_size*2;   
	
	// Initialize the remote offsets
	o0=0;
    o1=e_size;

	// Initialize the offset incrementer
	offset = e_size*2;
	
	// Determine the maximum number of iterations
	max_iterations = (size) / (2*e_size) - 1;
	
	// Determine how many bytes are left over
	offsetbytes = size - (max_iterations+1)*e_size*2;
	
	// Start filling the buffers
	if(max_iterations > 0) {
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);

		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b1]);
	
		outputbytes += extrawork(&area[0][b0], e_size);
	
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	
		o0=o0+offset;
	    o1=o1+offset;
	}
	
	for(i = 0; i < max_iterations; i++) {
		// This macro uses 4 parameters...
		// 	0: DMA ID. For triple buffering everyone uses the same DMA ID
		// 	1: Local buffer to perform "work" on
		// 	2: Local buffer to read data into from the SPE with a rank one lower than this one's
		// 	3: Local buffer to send to the SPE with a rank one higher than this one's
		PROCESS_ONE_CHUNK_FROM_SPE_TO_SPE(0, area[0][b1], area[0][b2], area[0][b0]);
		PROCESS_ONE_CHUNK_FROM_SPE_TO_SPE(0, area[0][b2], area[0][b0], area[0][b1]);

		o0=o0+offset;
	    o1=o1+offset;

		b3 = b0;
		b0 = b2;
		b2 = b1;
		b1 = b3;
	}
	
	if(max_iterations > 0) {
		// Process and put the last 2 buffers back into memory
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0])
	
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
		outputbytes += extrawork(&area[0][b1], e_size);
	
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b1]);
	}
	
	if(offsetbytes > 16384) {
		
		// Signal the previous SPE to start sending
		// the first 16384 extra bytes
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);

		// Wait for the first 16384 extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	
		// Start getting the last extra bytes from the previous SPE
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b1]);
	
		// Process the first 16384 extra bytes
		outputbytes += extrawork(&area[0][b0], e_size);
	
		// Wait for the last extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
		
		// Send the first 16384 extra bytes to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0]);

		// Process the last extra bytes
		outputbytes += extrawork(&area[0][b1], offsetbytes - 16384);

		// Send the last extra bytes to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b1]);

	}
	else if(offsetbytes > 0) {
		// Signal the previous SPE to start sending
		// the extra bytes
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);

		// Wait for the extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
		
		// Process the extra bytes
		outputbytes += extrawork(&area[0][b0], offsetbytes);

		// Send the extra bytes to the next SPE
		SEND_ONE_CHUNK_TO_NEXT_SPE(0, area[0][b0]);
	}
	
	
	// Ensure all DMA operations are done
	DMA_Wait2(0xFF);
	
	return outputbytes;
}
unsigned int FromSPEToMain(unsigned int buffer, int size){

	// offset variables bX for local destination vector
	unsigned int b0,b1,b2,b3;
	// variables offset oXX for remote data source 
	unsigned int o0, o1;
	
	// offset for next iteration
	unsigned int offset;
	
	// number of bytes processed
	unsigned int outputbytes = 0;
	
	// number of iterations
	int max_iterations, offsetbytes, i;
	
	// Determine the block size
	int e_size = 16384;
		
	// Initialize the local offsets
	b0=e_size*0;
	b1=e_size*1;
	b2=e_size*2;   
	
	// Initialize the remote offsets
	o0=0;
    o1=e_size;
  
	// Determine the offset incrementer
	offset = e_size*2;
	
	// Determine the maximum number of iterations
	max_iterations = (size) / (2*e_size) - 1;
	
	// Determine how many bytes are left over
	offsetbytes = size - (max_iterations+1)*e_size*2;
		
	if(max_iterations > 0) {
		// Start filling the buffers
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);
		
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b1]);
	
		outputbytes += extrawork(&area[0][b0], e_size);
	
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	}
	
	
	for(i = 0; i < max_iterations; i++) {

		// This macro uses 5 parameters...
		// 	0: DMA ID. For triple buffering everyone uses the same DMA ID
		// 	1: Local buffer to perform "work" on
		// 	2: Local buffer to read data into from the SPE with a rank one lower than this one's
		// 	3: Local buffer to commit back to main memory
		// 	4: Main memory address to commit the 3rd local buffer to
		PROCESS_ONE_CHUNK_FROM_SPE_TO_MEM(0, area[0][b1], area[0][b2], area[0][b0], buffer+o0);		
		PROCESS_ONE_CHUNK_FROM_SPE_TO_MEM(0, area[0][b2], area[0][b0], area[0][b1], buffer+o1);
		
		o0=o0+offset;
	    o1=o1+offset;

		b3 = b0;
		b0 = b2;
		b2 = b1;
		b1 = b3;
	}

	if(max_iterations > 0) {
		// Put the buffer we just computed back to main memory
		mfc_put(&area[0][b0], buffer+o0, e_size, 1, 0, 0);
	
		// Get the last buffer
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);

		// Compute on the last buffer and commit it back to main memory
		outputbytes += extrawork(&area[0][b1], e_size);
		mfc_put(&area[0][b1], buffer+o1, e_size, 1, 0, 0);
	}
	
	if(offsetbytes > 16384) {
		// Signal the previous SPE to start sending the
		// first 16384 extra bytes
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);

		// Wait for the first 16384 extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
	
		// Signal the previous SPE to start sending the
		// remaining extra bytes
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b1]);
	
		// Process the first 16384 extra bytes
		outputbytes += extrawork(&area[0][b0], e_size);
	
		// Wait for the remaining extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);
		
		// Send the first 16384 extra bytes back to main memory
		mfc_put(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
		
		// Process the remaining extra bytes
		outputbytes += extrawork(&area[0][b1], offsetbytes - 16384);
		
		// Send the remaining extra bytes back to main memory
		mfc_put(&area[0][b1], buffer+o1, e_size, 0, 0, 0);
	}
	else if(offsetbytes > 0) {
		// Signal the previous SPE to start sending the
		// extra bytes
		START_GET_ONE_CHUNK_FROM_PREV_SPE(0, area[0][b0]);

		// Wait for the extra bytes to arrive
		WAIT_GET_ONE_CHUNK_FROM_PREV_SPE(0);

		// Process the extra bytes
		outputbytes += extrawork(&area[0][b0], offsetbytes);
		
		// Send the extra bytes back to main memory
		mfc_put(&area[0][b0], buffer+o0, e_size, 0, 0, 0);
		
	}
	
	// Ensure all DMA operations are done
	DMA_Wait2(0xFF);
	
	return outputbytes;
}