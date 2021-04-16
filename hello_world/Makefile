
LIBS = -lerpc -lpthread -lnuma -ldl

all:
	g++ -g -std=c++11 -o server server.cc \
		-Wno-deprecated-declarations \
		-march=native \
		-I ../src \
		-L ../build $(LIBS)

	g++ -g -std=c++11 -o client client.cc \
		-Wno-deprecated-declarations \
		-march=native \
		-I ../src \
		-L ../build $(LIBS)

clean:
	rm server client

