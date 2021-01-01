lcd: lcd.c
	cc -Wall -o lcd lcd.c

install: lcd 
	cp lcd /usr/sbin
	cp lcd.init /etc/init.d/lcd
