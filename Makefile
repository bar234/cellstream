CELL_TOP := /opt/cell/sdk

DIRS = spu  
PROGRAM_ppu64 = diskio

IMPORTS = spu/lib_diskio_spu.a \
	  -lspe2 -lm -lc -lnuma -lpthread -lmisc
	
CFLAGS = -DUNIX -fomit-frame-pointer -O3

include $(CELL_TOP)/buildutils/make.footer
