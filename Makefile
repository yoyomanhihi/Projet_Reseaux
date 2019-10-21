
all:
	clean sender receiver

sender:
	gcc -Wall -ggdb -o sender src/sender.c -lz

receiver:
	gcc -Wall -ggdb -o receiver src/receiver.c -lz

tests:
	clean sender receiver
	gcc -Wall -o test1 tests/test1.c -lpthread -lcunit
	./test1
	rm sender receiver

clean:
	rm -f sender receiver test1
