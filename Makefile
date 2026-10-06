ktop: ktop.c
	cc -O2 -Wall -Wextra -o ktop ktop.c -lncurses

clean:
	rm -f ktop
