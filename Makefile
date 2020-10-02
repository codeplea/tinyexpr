CC = gcc
CCFLAGS = -ansi -Wall -Wshadow -O2
LFLAGS = -lm

.PHONY = all clean

all: smoke smoke_pr bench example example2 example3


smoke: smoke.c tinyexpr.c
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)
	./$@

smoke_pr: smoke.c tinyexpr.c
	$(CC) $(CCFLAGS) -DTE_POW_FROM_RIGHT -DTE_NAT_LOG -o $@ $^ $(LFLAGS)
	./$@

bench: benchmark.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example: example.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example2: example2.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example3: example3.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

.c.o:
	$(CC) -c $(CCFLAGS) $< -o $@

clean:
	rm -f *.o *.exe example example2 example3 bench smoke_pr smoke
