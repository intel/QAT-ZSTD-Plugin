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

LIBDIR  = $(ZSTD_ROOT)/lib

LIBZSTD = $(LIBDIR)/libzstd.a
EXMATCHFINDER = $(ZSTD_ROOT)/contrib/externalMatchfinder

CPPFLAGS+= -I$(LIBDIR) -I$(LIBDIR)/compress -I$(LIBDIR)/common

QATFLAGS = -I$(ICP_ROOT)/quickassist/include	\
		   -I$(ICP_ROOT)/quickassist/include/dc	\
		   -I$(ICP_ROOT)/quickassist/lookaside/access_layer/include	\
		   -L$(ICP_ROOT)/build -lqat_s -DQAT_SUPPORT


CFLAGS  += -std=gnu99 -g -O0
DEBUGFLAGS= -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
            -Wstrict-aliasing=1 -Wswitch-enum \
            -Wstrict-prototypes -Wundef -Wpointer-arith \
            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings \
            -Wredundant-decls
CFLAGS  += $(DEBUGFLAGS) $(MOREFLAGS)

CP ?= cp

default: test

all: test

test: ../src/qatmatchfinder.o test.c $(LIBZSTD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(QATFLAGS)  $^ $(LDFLAGS) -o $@

.PHONY: $(LIBZSTD)
$(LIBZSTD):
	$(CP) -r ../src/qatmatchfinder* $(EXMATCHFINDER)
	$(MAKE) -C $(LIBDIR) libzstd.a CFLAGS="$(CFLAGS) $(QATFLAGS)"

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(QATFLAGS) $(LDFLAGS) -c $< -o $@

clean:
	$(RM) *.o
	$(RM) ../src/*.o
	$(MAKE) -C $(LIBDIR) clean > /dev/null
	$(RM) test
