#NAME: Forrest Burton
#EMAIL: burton.forrest10@gmail.com
#ID: 005324612

default:
		gcc -g -Wall -Wextra -lz -o lab1b-client lab1b-client.c
		gcc -g -Wall -Wextra -lz -o lab1b-server lab1b-server.c

clean:
		rm -f lab1b-client lab1b-server *tar.gz

dist:
		tar -czvf lab1a-005324612.tar.gz lab1b-client.c lab1b-server.c README Makefile 