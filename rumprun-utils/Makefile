CC=gcc
LD=gcc
CFLAGS=-Wall -O2 -c
LDFLAGS=
BIN=rumprun_service
PREFIX=/usr/bin
OBJS=service.o
HEADERS=service.h _service.h

.PHONY: clean
.PHONY: install
.PHONY: uninstall

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

uninstall:
	rm -f $(PREFIX)/$(BIN)

install: uninstall $(BIN)
	install -m 0755 $(BIN) $(PREFIX)

clean:
	rm -f *.o $(BIN)
