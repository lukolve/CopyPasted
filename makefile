all:
	g++ -lbe -lnetwork  -o client client.cpp
	gcc -lnetwork -o server_single server_single.c
