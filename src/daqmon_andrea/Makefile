#
# Makefile for coda/daqmon/
#

#-----------------------------
# mandatory definitions: MAIN
#-----------------------------
MAIN = daqmon


#------------------
# generic Makefile
#------------------
include $(CODA)/src/Makefile.include

ROOTCFLAGS   := $(shell root-config --cflags)
ROOTLIBS     := $(shell root-config --libs) 
ROOTGLIBS    := $(shell root-config --glibs)

#-----------------------
# customize definitions
#-----------------------
CLON_FLAGS += -std=c++11

CLON_FLAGS += -I./src.s \
				-I$(CODA)/common/include -I$(CODA)/common/include \
				-I$(IPC_HOME)/src/main -I$(CODA)/src/ipc/ipc.s \
				-I$(CODA)/src/codautil/codautil.s

CLON_FLAGS += $(ROOTCFLAGS)

ifeq ("$(OSTYPE_MACHINE)","Linux_i686")
  CLON_FLAGS += -I/usr/local/include/apr-1
else ifeq ("$(OSTYPE_MACHINE)","Linux_i686_vme")
  CLON_FLAGS += -I/usr/local/include/apr-1
else
  CLON_FLAGS += -I/usr/include/apr-1
endif


LIBNAMES += $(CODA)/$(OSTYPE_MACHINE)/lib/libipc.a
LIBNAMES += $(CODA)/$(OSTYPE_MACHINE)/lib/libcodautil.a
LIBNAMES += $(CODA)/$(OSTYPE_MACHINE)/lib/libmysql4.1.20.a
LIBNAMES += $(CODA)/$(OSTYPE_MACHINE)/lib/libet.a
LIBNAMES += $(CODA)/$(OSTYPE_MACHINE)/lib/libevio.a

LIBNAMES += $(ROOTLIBS) $(ROOTGLIBS)

ifeq ("$(OSTYPE_MACHINE)","Linux_i686")
  LIBNAMES += /usr/local/lib/libactivemq-cpp.so
  LIBNAMES += /usr/local/lib/libapr-1.so
  LIBNAMES += -luuid -lrt -lcrypt -lssl -lcrypto -ldl -lz
  LIBNAMES += -L/usr/kerberos/lib -lpthread -pthread -Wl,-rpath -Wl,/usr/local/lib
else
  LIBNAMES += /usr/local/lib/libactivemq-cpp.so
  LIBNAMES += -lapr-1
  LIBNAMES += -lrt -lcrypt -lssl -lcrypto -ldl -lz
  LIBNAMES += -L/usr/kerberos/lib -lpthread -pthread -Wl,-rpath -Wl,/usr/local/lib
endif

#for old solaris only ???
LIBNAMES += -lresolv

LIBNAMES += $(X11LIBS)
LIBNAMES += $(SYSLIBS)



#-------------------
# customize targets
#-------------------
install: install_bin

#install: install_lib install_bin
#	rm -rf tmp
#	mkdir tmp
#	cp $(MAIN).s/*.h tmp
#	chmod 664 tmp/*
#	cp tmp/* $(CODA)/common/include
#	rm -rf tmp





















