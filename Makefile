# Variables
TARGET  := 64th
CFLAGS  := -g -Wall
LDFLAGS :=

# Targets
.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	rm -f $(TARGET)

64th: 64th.c arg.h core.64th
	gcc $(CFLAGS) -o $@ $< $(LDFLAGS)
