main: main.c
	$(CC) main.c -o main -Wall -Wextra -pedantic -std=c99

test:
	./main

clean:
	$(RM) main