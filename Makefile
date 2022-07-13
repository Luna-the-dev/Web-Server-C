httpserver: httpserver.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -pthread -g -o httpserver httpserver.c
httpproxy: httpproxy.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -pthread -g -o httpproxy httpproxy.c
httpclient: httpclient.c
	gcc -Wall -Wextra -Wpedantic -Wshadow -pthread -g -o httpclient httpclient.c
