# #######################################################################
#
#   BSD LICENSE
#
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# #######################################################################

.PHONY: default
default: lib

INSTALLDIR ?= /usr/local
LIBDIR     ?= $(INSTALLDIR)/lib
INCLUDEDIR ?= $(INSTALLDIR)/include

CP ?= cp

ifneq ($(ICP_ROOT), )
	QATFLAGS = -I$(ICP_ROOT)/quickassist/include	\
		   	   -I$(ICP_ROOT)/quickassist/include/dc	\
		       -I$(ICP_ROOT)/quickassist/lookaside/access_layer/include
	LDFLAGS  = -Wl,-rpath,$(ICP_ROOT)/build -L$(ICP_ROOT)/build -lqat_s
	ifneq ($(ENABLE_USDM_DRV), 0)
		QATFLAGS += -I$(ICP_ROOT)/quickassist/utilities/libusdm_drv -DENABLE_USDM_DRV
		LDFLAGS  += -lusdm_drv_s
	endif
else
	QATFLAGS = -DINTREE
	LDFLAGS  = -lqat
	ifneq ($(ENABLE_USDM_DRV), 0)
		QATFLAGS += -DENABLE_USDM_DRV
		LDFLAGS	 += -lusdm
	endif
endif

ifdef ZSTDLIB
CFLAGS   += -I$(ZSTDLIB)
endif

CFLAGS += -Wall -Werror -Wextra -Wcast-align -Wshadow -Wstrict-aliasing=1 \
		  -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
		  -Wundef -Wpointer-arith -Wvla -Wformat=2 -Winit-self \
		  -Wfloat-equal -Wwrite-strings -Wredundant-decls -Wc++-compat \
		  -pedantic -fstack-protector-strong -fPIE -fPIC \
		  -fno-delete-null-pointer-checks -fwrapv -fno-strict-overflow

DEBUGLEVEL ?=0

DEBUGFLAGS += -DDEBUGLEVEL=$(DEBUGLEVEL)
ifneq ($(DEBUGLEVEL), 0)
	QATFLAGS += -g -O0
else
	QATFLAGS += -O3
endif

qatseqprod.o: qatseqprod.c
	$(CC) -c $(CFLAGS) $(QATFLAGS) $(DEBUGFLAGS) $^ -o $@

lib: qatseqprod.o
	$(AR) rc libqatseqprod.a $^
	$(CC) -shared $^ $(LDFLAGS) -o libqatseqprod.so

.PHONY: install
install: lib
	$(CP) libqatseqprod.a $(LIBDIR)
	$(CP) libqatseqprod.so $(LIBDIR)
	$(CP) qatseqprod.h $(INCLUDEDIR)
	@echo qatseqprod library successfully installed

.PHONY: uninstall
uninstall:
	$(RM) $(LIBDIR)/libqatseqprod.a
	$(RM) $(LIBDIR)/libqatseqprod.so
	$(RM) $(INCLUDEDIR)/qatseqprod.h
	@echo qatseqprod library successfully uninstalled

clean:
	$(RM) *.o
	$(RM) libqatseqprod.a libqatseqprod.so
