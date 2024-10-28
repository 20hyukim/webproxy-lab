# Makefile for Echo Server and Client

CC = gcc
CFLAGS = -g -Wall -I. # 현재 디렉토리에서 헤더 파일을 찾도록 설정
LDFLAGS = -lpthread

all: echo_server echo_client

# 컴파일 규칙
csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

echo_server.o: echo/echo_server.c csapp.h
	$(CC) $(CFLAGS) -c echo/echo_server.c

echo_client.o: echo/echo_client.c csapp.h
	$(CC) $(CFLAGS) -c echo/echo_client.c

echo_routine.o: echo/echo_routine.c csapp.h
	$(CC) $(CFLAGS) -c echo/echo_routine.c

# 실행 파일 빌드 규칙
echo_server: echo_server.o echo_routine.o csapp.o
	$(CC) $(CFLAGS) echo_server.o echo_routine.o csapp.o -o echo_server $(LDFLAGS)

echo_client: echo_client.o echo_routine.o csapp.o
	$(CC) $(CFLAGS) echo_client.o echo_routine.o csapp.o -o echo_client $(LDFLAGS)

# Clean 규칙
clean:
	rm -f *~ *.o echo_server echo_client core

# Handin 규칙 (기존 내용 유지)
handin:
	(make clean; cd ..; tar cvf $(USER)-proxylab-handin.tar proxylab-handout --exclude tiny --exclude nop-server.py --exclude proxy --exclude driver.sh --exclude port-for-user.pl --exclude free-port.sh --exclude ".*")