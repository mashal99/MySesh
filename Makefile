CC = gcc
CFLAGS = -g -std=c99 -Wall -fsanitize=address,undefined

mysh: mysh.o 
	$(CC) $(CFLAGS) mysh.o -o $@

mysh.o: mysh.c 
	$(CC) $(CFLAGS) -c $^

clean:
	rm *.o mysh