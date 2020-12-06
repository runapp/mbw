#CFLAGS=-O2 -Wall -g
NAME=mbw
TARFILE=${NAME}.tar.gz

CC=gcc
CFLAGS=-g -O3 -static -Wall
LDFLAGS=-lm

all: mbw mbw-gen

mbw: mbw.o

mbw-gen: mbw-gen.o

clean:
	rm -f mbw
	rm -f mbw-gen
	rm -f ${NAME}.tar.gz

${TARFILE}: clean
	 tar cCzf .. ${NAME}.tar.gz --exclude-vcs ${NAME} || true

rpm: ${TARFILE}
	 rpmbuild -ta ${NAME}.tar.gz 
