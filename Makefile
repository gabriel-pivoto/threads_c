WINLIBS_GCC=C:/Users/gabri/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin/gcc.exe

ifeq ($(OS),Windows_NT)
ifneq (,$(wildcard $(WINLIBS_GCC)))
CC="$(WINLIBS_GCC)"
else
CC=gcc
endif
else
CC=gcc
endif

CFLAGS=-std=c11 -O2 -Wall -Wextra -pedantic -pthread
TARGET=ticket_sim

all: $(TARGET)

$(TARGET): main.c ticket_system.c ticket_system.h
	$(CC) $(CFLAGS) main.c ticket_system.c -o $(TARGET)

run-demo-1000: $(TARGET)
	./$(TARGET) 10 1000 20

run-demo-10000: $(TARGET)
	./$(TARGET) 10 10000 20

demo: $(TARGET)
	./$(TARGET) 10 1000 20
	./$(TARGET) 10 10000 20

run: run-demo-1000

clean:
	rm -f $(TARGET) $(TARGET).exe