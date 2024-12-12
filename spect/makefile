# Define the compiler
CC = g++

# Define the compiler flags
CFLAGS = -c -Wall -std=c++11 

# Define the linker flags
LDFLAGS = -pthread

# Define the source files
SOURCES = main.cpp spect.cpp

# Define the object files
OBJECTS = $(SOURCES:.cpp=.o)

# Define the name of the executable
EXECUTABLE = spect

# Define the build target
all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

# Define the clean target
clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
