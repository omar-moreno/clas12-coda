#
# Makefile for creg/
#

#-----------------------------
# mandatory definitions: MAIN
#-----------------------------
MAIN = creg


#------------------
# generic Makefile
#------------------
include $(CODA)/src/Makefile.include


#-----------------------
# customize definitions
#-----------------------

# Directory from which applications will reference the library of Dp
# scripts (note: you can set the DP_LIBRARY environment variable at
# run-time to override this value (Sergey: you can not !) ):

CLON_FLAGS += -I./$(MAIN).s -I$(CODA)/common/include \
	-DHAVE_UNISTD_H=1 -DSTDC_HEADERS=1 -DHAVE_LIMITS_H=1 -DHAVE_STRDUP=1 \
	-DHAVE_STRERROR=1  -DHAVE_STRCASECMP=1 -DHAVE_UNISTD_H=1 -DHAVE_ERRNO_H=1 \
	-DHAVE_FCNTL_H=1 -DNO_STREAM_H=1 -DNO_UN_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1  \
	-DHAVE_TOLOWER=1 -DSTDC_HEADERS=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_FLOAT_H=1 \
	-DHAVE_MALLOC_H=1 -DHAVE_ERRNO_H=1 -DHAVE_UNISTD_H=1 -DHAVE_MEMORY_H=1 -DHAVE_LIMITS_H=1 \
	-DHAVE_SYS_TIME_H=1 -DHAVE_SYS_WAIT_H=1 -DTIME_WITH_SYS_TIME=1 -DNO_UNION_WAIT=1 \
	-DNEED_MATHERR=1 -DSVIPC_NO_UNION_SEMUN=1 -DTK_FILE_COUNT=_cnt 

ifeq ("$(OSTYPE)","SunOS")
	CLON_FLAGS += -DSOLARIS=1 -DNO_UNION_WAIT=1 -DTK_FILE_COUNT=_cnt -DNEED_MATHERR=1 -Dvfork=fork \
		-D_REENTRANT -D__EXTENSIONS__
endif

ifeq ("$(OSTYPE)","Linux")
	CLON_FLAGS += -DTK_FILE_READ_PTR=1
endif

ifeq ("$(OSTYPE)","Darwin")
        CLON_FLAGS += $(X11INCDIR)
endif

LIBNAMES += $(X11LIBS)

#-------------------
# customize targets
#-------------------
install: install_lib
	rm -rf tmp
	mkdir tmp
	cp $(MAIN).s/codaRegistry.h tmp/
	chmod 664 tmp/*
	-mkdir -p $(CODA)/common/include
	cp tmp/* $(CODA)/common/include
	rm -rf tmp


















