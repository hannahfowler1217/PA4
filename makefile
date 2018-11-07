# makefile

all: dataserver client

reqchannel.o: reqchannel.h reqchannel.cpp
	g++ -g -w -Wall -O1 -std=c++1y -c reqchannel.cpp

SafeBuffer.o: SafeBuffer.h SafeBuffer.cpp
	g++ -g -w -Wall -O1 -std=c++1y -c SafeBuffer.cpp

Histogram.o: Histogram.h Histogram.cpp
	g++ -g -w -Wall -O1 -std=c++1y -c Histogram.cpp


dataserver: dataserver.cpp reqchannel.o
	g++ -g -w -Wall -O1 -std=c++1y -o dataserver dataserver.cpp reqchannel.o -lpthread

client: client.cpp reqchannel.o SafeBuffer.o Histogram.o
	g++ -g -w -Wall -O1 -std=c++1y -o client client.cpp reqchannel.o SafeBuffer.o Histogram.o -lpthread

clean:
	rm -rf *.o fifo* dataserver client
