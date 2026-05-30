CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
BINARY = recon_server
SOURCES = server.cpp recon.cpp image_utils.cpp

ifeq ($(OS),Windows_NT)
	BINARY := recon_server.exe
	LDLIBS += -lws2_32 -lpsapi
endif

build: $(BINARY)

$(BINARY): $(SOURCES) recon.hpp image_utils.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES) $(LDLIBS)

run: build
	./$(BINARY)

clean:
	rm -f recon_server recon_server.exe
	rm -rf output
