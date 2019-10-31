all:
	gcc -Wall -ggdb -o receiver src/receiver.c -lz

sender:
	gcc -Wall -ggdb -o sender src/sender.c -lz

receiver:
	gcc -Wall -ggdb -o receiver src/receiver.c -lz

tests:
	echo "Lancez nos sender dans un autre terminal pour lancer les tests."
	./receiver :: 1234 -o sortie.txt
	./receiver :: 12345 -o sortie99.txt

clean:
	rm receiver
