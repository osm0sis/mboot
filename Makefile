ifeq ($(CC),cc)
CC = gcc
endif
AR = ar rc
ifeq ($(windir),)
EXT =
RM = rm -f
CP = cp
else
EXT = .exe
RM = del
CP = copy /y
endif

CFLAGS += -ffunction-sections -O3

INC = -I.

ifneq (,$(findstring darwin,$(CROSS_COMPILE)))
	UNAME_S := Darwin
else
	UNAME_S := $(shell uname -s)
endif
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -Wl,-dead_strip
else
	LDFLAGS += -Wl,--gc-sections -s
endif

all:mboot$(EXT)

static:
	$(MAKE) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS) -static"

mboot$(EXT):mboot.o
	$(CROSS_COMPILE)$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	$(CROSS_COMPILE)$(CC) -o $@ $(CFLAGS) -c $< $(INC) -Werror

install:
	install -m 755 mboot$(EXT) $(PREFIX)/bin

clean:
	$(RM) mboot
	$(RM) *.a *.~ *.exe *.o

