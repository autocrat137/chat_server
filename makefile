all: client	server
client: c.c
	ipcrm --all && gcc -g c.c -o c
server: s.c
	ipcrm --all && gcc -g s.c -o s
killc:
	killall -9 c
kills:
	killall -9 s
clean:
	rm c s