all:reinette-II

reinette-II:reinette-II.c
	gcc -Wall -Werror -O3 reinette-II.c -o reinette-II -lncurses

