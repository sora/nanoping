nanoping: nanoping.c
	gcc -Wall -o nanoping nanoping.c

run: nanoping
	./nanoping -i en0 -d 8.8.8.8

clean:
	rm -f *.o nanoping
