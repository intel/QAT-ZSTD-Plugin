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
LIB     = ../src

LDFLAGS = $(LIB)/libqatseqprod.a -I$(LIB)

ifneq ($(ICP_ROOT), )
	LDFLAGS += -lqat_s -lusdm_drv_s -Wl,-rpath,$(ICP_ROOT)/build -L$(ICP_ROOT)/build
else
	LDFLAGS += -lqat -lusdm
endif

ifdef ZSTDLIB
LDFLAGS += $(ZSTDLIB)/libzstd.a
else
ZSTDLIB := $(shell find /usr /lib /local -name 'libzstd.a' 2>/dev/null | head -n 1)
ZSTDLIB ?= /usr/local/lib/libzstd.a
ifneq ("$(wildcard $(ZSTDLIB))","")
    LDFLAGS += $(ZSTDLIB)
else
    $(error libzstd.a not found, please install libzstd or specify the path manually)
endif
endif

default: test benchmark

all: test benchmark

test: test.c
	$(Q)$(MAKE) -C $(LIB)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

benchmark: benchmark.c
	$(Q)$(MAKE) -C $(LIB)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@ -lpthread

clean:
	$(Q)$(MAKE) -C $(LIB) $@
	$(RM) test benchmark
