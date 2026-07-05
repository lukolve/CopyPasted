# Name of our executable binary
TARGET = CopyPaste

# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -O2

# Add Haiku's system headers
CXXFLAGS += -I/boot/system/develop/headers

# Haiku specific libraries needed (libbe handles BApplication and BClipboard)
LIBS = -lbe -lnetwork -lpthread

# Source files
SRCS = main.cpp

# Default rule
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

