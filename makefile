CC=g++
CFLAGS=-Wall -Wno-switch --std=c++11 -L /lib64 -I. -O2 `pkg-config --cflags opencv`
ODIR=obj
LIBS=-lcurl -lz -lpthread `pkg-config --libs opencv`

SRCS=checksum.cpp common.cpp file.cpp http.cpp json.cpp main.cpp match.cpp path.cpp url.cpp vod.cpp

OBJS=$(SRCS:.cpp=.o)

MAIN=vodscanner

.PHONY: depend clean

all:	$(MAIN)
	@echo Done

$(MAIN):	$(OBJS)
	$(CC) $(CFLAGS) -o $(MAIN) $(OBJS) $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o *~ $(MAIN)

