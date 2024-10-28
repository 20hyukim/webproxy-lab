# Makefile for Tiny Web server and CGI program

CC = gcc
CFLAGS = -g -Wall -I. # 현재 디렉토리에서 헤더 파일을 찾도록 설정
LDFLAGS = -lpthread

# 기본 실행 파일과 CGI 프로그램
all: tiny cgi-bin/adder

# tiny.c를 컴파일하여 tiny 실행 파일을 생성
tiny: tiny.o csapp.o
	$(CC) $(CFLAGS) tiny.o csapp.o -o tiny $(LDFLAGS)

# CGI 프로그램 adder를 컴파일하여 cgi-bin 디렉토리에 설치
cgi-bin/adder: cgi-bin/adder.o
	$(CC) $(CFLAGS) cgi-bin/adder.o -o cgi-bin/adder $(LDFLAGS)

# csapp.c를 컴파일
csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

# tiny.c를 컴파일
tiny.o: tiny.c csapp.h
	$(CC) $(CFLAGS) -c tiny.c

# adder.c를 컴파일하여 cgi-bin 디렉토리에 .o 파일을 생성
cgi-bin/adder.o: cgi-bin/adder.c
	$(CC) $(CFLAGS) -c cgi-bin/adder.c -o cgi-bin/adder.o

# 정리 규칙
clean:
	rm -f *.o tiny cgi-bin/adder cgi-bin/*.o