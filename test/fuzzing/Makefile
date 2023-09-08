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
LIB     = ../../src

CC = clang

default: qatseqprodfuzzer.o

all: qatseqprodfuzzer.o

ENABLE_USDM_DRV ?= 0
ifneq ($(ENABLE_USDM_DRV), 0)
		QATFLAGS += -DENABLE_USDM_DRV
endif

ifneq ($(ICP_ROOT), )
	QATFLAGS = -I$(ICP_ROOT)/quickassist/include	\
			-I$(ICP_ROOT)/quickassist/include/dc	\
			-I$(ICP_ROOT)/quickassist/lookaside/access_layer/include
	ifneq ($(ENABLE_USDM_DRV), 0)
		QATFLAGS += -I$(ICP_ROOT)/quickassist/utilities/libusdm_drv
	endif
else
	QATFLAGS = -DINTREE
endif

ifdef ZSTDLIB
	CFLAGS += -I$(ZSTDLIB)
endif


CFLAGS += -g -fno-omit-frame-pointer -fsanitize=undefined,address,fuzzer
DEBUGLEVEL ?=0
DEBUGFLAGS += -DDEBUGLEVEL=$(DEBUGLEVEL)

qatseqprodfuzzer.o: $(LIB)/qatseqprod.c
	$(CC) -c $(CFLAGS) $(QATFLAGS) $(DEBUGFLAGS) $^ -o qatseqprod.o
	$(CC) -c $(CFLAGS) qatseqprodfuzzer.c -o _qatseqprodfuzzer.o
	ld -r qatseqprod.o _qatseqprodfuzzer.o -o $@

clean:
	$(RM) *.o
