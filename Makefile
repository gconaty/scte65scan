scte65scan:	scte65scan.h scte65scan.c tunerdmx.h tunerdmx.c
	$(CC) -g tunerdmx.c scte65scan.c -o $@

all: scte65scan

clean:
	rm scte65scan
