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
	$(MAKE) -C $(LIBDIR) clean > /dev/null
	$(RM) test
