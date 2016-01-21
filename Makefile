CC = gcc
CCFLAGS = -ansi -Wall -Wshadow -O2 $(EXTRAS)


all: test bench example example2


test: test.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^
	./$@


bench: benchmark.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^

example: example.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^

example2: example2.o tinyexpr.o
	$(CC) $(CCFLAGS) -o $@ $^

.c.o:
	$(CC) -c $(CCFLAGS) $< -o $@


clean:
	rm *.o
	rm *.exe
