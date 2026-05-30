CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
BINARY = recon_client

ifeq ($(OS),Windows_NT)
	BINARY := recon_client.exe
	LDLIBS += -lws2_32
endif

build: $(BINARY)

$(BINARY): client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

run: build
	./$(BINARY)

clean:
	rm -f recon_client recon_client.exe
