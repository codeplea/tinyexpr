CC = gcc
CCFLAGS = -ansi -Wall -Wshadow -O2
LFLAGS = -lm


all: test bench example example2


test: test.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)
	./$@


bench: benchmark.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example: example.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example2: example2.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

.c.o:
	$(CC) -c $(CCFLAGS) $< -o $@


clean:
	rm *.o
	rm *.exe
