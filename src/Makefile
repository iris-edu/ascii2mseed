
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use

# Required compiler parameters
CFLAGS += -I../libmseed

LDFLAGS = -L../libmseed
LDLIBS = -lmseed

BIN = ascii2mseed

SRCS = ascii2mseed.c

OBJS = $(SRCS:.c=.o)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o ../$@ $(OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(OBJS) ../$(BIN)

install:
	@echo
	@echo "No install target, copy the executable(s) yourself"
	@echo
