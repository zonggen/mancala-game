FLAGS = -Wall -g -std=gnu99

mancsrv : mancsrv.c
	gcc ${FLAGS} -o $@ $^

clean:
	rm -f mancsrv
