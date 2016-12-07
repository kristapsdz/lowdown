CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings
OBJS		= autolink.o \
		  buffer.o \
		  document.o \
		  escape.o \
		  html.o \
		  html_blocks.o \
		  main.o \
		  stack.o

all: lowdown

lowdown: $(OBJS)
	$(CC) -o $@ $(OBJS)

$(OBJS): extern.h

clean:
	rm -f $(OBJS) lowdown
