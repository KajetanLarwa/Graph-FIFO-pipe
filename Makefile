all: prog
prog: prog.c	
	gcc -Wall -o prog prog.c
.PHONY: clean all
clean:
	rm prog