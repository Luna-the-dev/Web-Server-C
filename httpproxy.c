#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define BUFFER_SIZE 512
#define QUEUE_SIZE 512

uint16_t strtouint16(char number[]);
int isStrInt(char* str);
int valid_filename(char fileName[]);

int create_listen_socket(uint16_t port);
int create_client_socket(uint16_t port);
void parseArgs(int argc, char *argv[]);
void handle_connection(int connfd);
void getHealthcheck();
int getServerPort(int connfd);
int checkCache(int clientConnfd, int port, char resourceName[]);
int isCachedFileUpToDate(char cachedModifyDate[], char serverModifyDate[]);
void getServerLastModified(int clientConnfd, int port, char resourceName[], char serverLastModified[]);
void sendCachedResponseToClient(int connfd, int index);
void* t_waitForReq(void* arg);
void* t_healthcheck(void* arg);
void process_request(int connfd);
int parseRequestHeaders(char buffer[], int connfd, char method[], char resource[], char httpVer[], char host[]);
void fwdResponseToClient(int connfd, int clientConnfd, char resourceName[]);
void send_response_fail(int connfd, int statusCode);
const char* generate_status_msg(int code);


// GLOBAL VARIABLES
pthread_mutex_t m_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_healthcheck = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_cache = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_gotRequest = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_performHC = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_useCache = PTHREAD_COND_INITIALIZER;

int connQueue[QUEUE_SIZE]; // queue of connfds and their server ports
int connQueueCount = 0;

int numOfThreads = 5, healthcheckInterval = 5, reqSinceLastHC = 0, healthchecksNeeded = 0;

int numOfCachedFiles = 3, maxCachedBytes = 1024;

uint16_t clientPort;
uint16_t* serverPorts;
int numOfServerPorts = 0;

struct HealthcheckInfo {
  int entries;
  int errors;
  int isProblematic;
};
struct HealthcheckInfo* healthchecks;

struct CachedFilesInfo {
  char resourceName[20];
  char lastModified[40];
  char* content;
  int contentLength;
};
struct CachedFilesInfo* cachedFiles;

int main(int argc, char *argv[]) {
  int listenfd;

  // parse through the server arguments
  parseArgs(argc, argv);

  // initialize healthchecks array
  healthchecks = malloc(numOfServerPorts * sizeof(healthchecks));

  // initialize cached cached files array
  cachedFiles = malloc(numOfCachedFiles * sizeof(cachedFiles));
  for (int i = 0; i < numOfCachedFiles; ++i) {
    strcpy(cachedFiles[i].resourceName, "hey!!!");
    cachedFiles[i].content = malloc(maxCachedBytes * sizeof(char));
  }

  getHealthcheck();

  pthread_t healthcheckThread;
  if (pthread_create(&healthcheckThread, NULL, &t_healthcheck, NULL) != 0) {
    perror("Failed to create thread");
  }
  
  // create a listening socket on the client's port number
  listenfd = create_listen_socket(clientPort);

  // create array of n threads
	pthread_t t_ids[numOfThreads];
	for (int i = 0; i < numOfThreads; ++i) {
		if (pthread_create(&t_ids[i], NULL, &t_waitForReq, NULL) != 0) {
			perror("Failed to create thread");
		}
	}

  while(1) {
    // create the connection file descriptor
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }

    // handle the connection
    handle_connection(connfd);
  }
  free(serverPorts);
  free(healthchecks);
  free(cachedFiles);

  return EXIT_SUCCESS;
}

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}

/*
  Checks to see if string is all digits, not including any special chars (i.e. '-'. '.', etc)
  Returns 0 if non-digit char is found, 1 otherwise
*/
int isStrInt(char* str) {
  int len = strlen(str);
  for (int i = 0; i < len; ++i) {
    if (!isdigit(str[i])) {
      return 0;
    }
  }
  return 1;
}

// checks to see if fileName is valid
int valid_filename(char fileName[]) {
  int fileNameLen = strlen(fileName);

  // return false if filename is too long
  if (fileNameLen > 19) {
    return 0;
  }

  // return false if filename contains any characters other than a-z, A-Z, 0-9, '.', or '_'
  for (int i = 0; i < fileNameLen; ++i) {
    if ( isalnum(fileName[i]) || fileName[i] == '.' || fileName[i] == '_')
      continue;
    return 0;
  }

  return 1;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    err(EXIT_FAILURE, "socket error");
  }

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    err(EXIT_FAILURE, "bind error");
  }

  if (listen(listenfd, 500) < 0) {
    err(EXIT_FAILURE, "listen error");
  }

  return listenfd;
}

/**
   Creates a socket for connecting to a server running on the same
   computer, listening on the specified port number.  Returns the
   socket file descriptor on succes.  On failure, returns -1 and sets
   errno appropriately.
 */
int create_client_socket(uint16_t port) {
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd < 0) {
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (connect(clientfd, (struct sockaddr*) &addr, sizeof addr)) {
    return -1;
  }
  return clientfd;
}

/*
  Parses the args from the command line and assigns them to their
  respective variables
*/
void parseArgs(int argc, char *argv[]) {
  int opt;
  
  // parsing through the flags
  while((opt = getopt(argc, argv, ":N:R:s:m:")) != -1) {
    // check to see if option was a pos int
    if (!isStrInt(optarg)) {
      errx(EXIT_FAILURE, "option -%c has to be a positive integer", optopt);
    }
    switch (opt) {
      case 'N':
        numOfThreads = atoi(optarg);
        break;
      case 'R':
        healthcheckInterval = atoi(optarg);
        break;
      case 's':
        numOfCachedFiles = atoi(optarg);
        break;
      case 'm':
        maxCachedBytes = atoi(optarg);
        break;
      case ':':
        errx(EXIT_FAILURE, "option -%c needs a value", optopt);
      case '?':
        errx(EXIT_FAILURE, "unknown option -%c", optopt);
    }
  }
  
  // checking if minimum number of ports were specified
  if (optind > argc - 2) {
    errx(EXIT_FAILURE, "program requires at least one client and one server port number");
  }
  
  // get the client port number
  clientPort = strtouint16(argv[optind]);
  if (clientPort == 0) {
    errx(EXIT_FAILURE, "port numbers must be positive integers");
  }
  optind++;
  
  // fill array of server port numbers
  numOfServerPorts = argc - optind;
  serverPorts = malloc(numOfServerPorts * sizeof *serverPorts);
  for (int i = 0; optind < argc; ++i, ++optind) {
    serverPorts[i] = strtouint16(argv[optind]);
    if (serverPorts[i] == 0) {
      errx(EXIT_FAILURE, "port numbers must be positive integers");
      free(serverPorts);
    }
  }
}

// wrapper for the thread that handles the healthcheck
void* t_healthcheck(void* arg) {
  free(arg);
  while (1) {
    /* ---------- START CRIT REGION ---------- */
    pthread_mutex_lock(&m_healthcheck);
    // wait until a healthcheck needs to be done
    while (healthchecksNeeded <= 0) {
      pthread_cond_wait(&c_performHC, &m_healthcheck);
    }
    healthchecksNeeded--; // decrement the number of healthchecks that need to be done

    getHealthcheck();
    pthread_mutex_unlock(&m_healthcheck);
    /* ----------- END CRIT REGION ----------- */
  }
}

// dispatcher function
void handle_connection(int connfd) {
  // wait if queue is full (this will never happen in this program's scope)
  while (connQueueCount == QUEUE_SIZE) {  }

  /* ---------- START CRIT REGION ---------- */
  int rc = pthread_mutex_lock(&m_queue);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // push connfd and onto queue
  connQueue[connQueueCount] = connfd;
  connQueueCount++;

  // signal to worker threads that there is a new connection to grab
  pthread_cond_signal(&c_gotRequest);

  pthread_mutex_unlock(&m_queue);
  /* ----------- END CRIT REGION ----------- */
}

void getHealthcheck() {
  char healthcheckBuf[128];

  for (int i = 0; i < numOfServerPorts; ++i) {
    // create client socket from current server port
    int clientConnfd = create_client_socket(serverPorts[i]);

    // check to see if server responded
    if (clientConnfd == -1) {
      healthchecks[i].isProblematic = 1;
      continue;
    }

    // empty healthcheckBuf
    memset(healthcheckBuf, '\0', 128);

    // send healthcheck request to clientConnfd
    sprintf(healthcheckBuf, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\n\r\n",
        serverPorts[i]
    );
    send(clientConnfd, healthcheckBuf, strlen(healthcheckBuf), 0);

    // empty healthcheckBuf
    memset(healthcheckBuf, '\0', 128);

    // TODO: maybe change to this to make it more robust
    // receive response from server
    int bytesRead = recv(clientConnfd, healthcheckBuf, 128, 0);

    char* pResponseCode = strstr(healthcheckBuf, "HTTP/1.1 ") + 9;
    char responseCodeStr[4];
    memcpy(responseCodeStr, pResponseCode, strcspn(pResponseCode, " "));
    int responseCode = atoi(responseCodeStr);
    if (responseCode >= 300) {
      healthchecks[i].isProblematic = 1;
      continue;
    }

    // point to the response body
    char* pHealthcheckBody = strstr(healthcheckBuf, "\r\n\r\n");

    // recv again if body wasnt included
    char* endPtr = healthcheckBuf + bytesRead;
    if (endPtr <= pHealthcheckBody + 4) {
      // empty healthcheckBuf
      memset(healthcheckBuf, '\0', 128);
      recv(clientConnfd, healthcheckBuf, 128, 0);
      pHealthcheckBody = healthcheckBuf;  // point to the beginning of the buffer, which should now be the body
    } else {  // if body was included, move pointer to it
      pHealthcheckBody += 4;
    }

    close(clientConnfd);

    // parse body into entries and errors
    if (sscanf(pHealthcheckBody, "%d\n%d\n", &healthchecks[i].errors, &healthchecks[i].entries) != 2) {
      healthchecks[i].isProblematic = 1;
    } else {
      healthchecks[i].isProblematic = 0;
    }
  }
}

/*
  load balancer
  returns server port number
*/
int getServerPort(int connfd) {
  int serverIndex = 0;
  int lowestEntries = INT_MAX;

  for (int i = 0; i < numOfServerPorts; ++i) {
    // save the current server if it has the lowest number of entries and is stable
    if (healthchecks[i].entries < lowestEntries && healthchecks[i].isProblematic == 0) {
      serverIndex = i;
      lowestEntries = healthchecks[i].entries;
    } else
    // save the current server if it has the same number of entries, less errors, and is stable
    if (healthchecks[i].entries == lowestEntries &&
        healthchecks[i].errors < healthchecks[serverIndex].errors &&
        healthchecks[i].isProblematic == 0
    ) {
      serverIndex = i;
      lowestEntries = healthchecks[i].entries;
    }
  }

  // all servers are down
  if (lowestEntries == INT_MAX) {
    send_response_fail(connfd, 500);
  }

  healthchecks[serverIndex].entries++;
  return serverPorts[serverIndex];
}

/*
  checks to see if the requested file is already in the cache and is up to date
  returns the index of the cached file if it meets those requirements, else returns -1
*/
int checkCache(int clientConnfd, int port, char resourceName[]) {
  // loop through the cached files
  for (int i = 0; i < numOfCachedFiles; ++i) {
    // if requested file is in cache AND it is up to date, return index of cached file
    if (strcmp(resourceName, cachedFiles[i].resourceName) == 0) {
      char serverLastModified[40];
      getServerLastModified(clientConnfd, port, resourceName, serverLastModified);
      if ( isCachedFileUpToDate(cachedFiles[i].lastModified, serverLastModified) ) {
        return i;
      }
    }
  }

  // else return -1
  return -1;
}

/*
  checks to see if the cached file is up to date
  returns 1 if the cached file's last modified date is later or equal to the servers, else returns 0
*/
int isCachedFileUpToDate(char cachedModifyDate[], char serverModifyDate[]) {
  char cDayName[4], sDayName[4];
  int cDay = 0, sDay = 0;
  char cMonthStr[4], sMonthStr[4];
  int cMonth = 0, sMonth = 0;
  int cYear = 0, sYear = 0;
  int cHour = 0, sHour = 0;
  int cMinute = 0, sMinute = 0;
  int cSecond = 0, sSecond = 0;
  
  // parse the cached file's last-modified date
  sscanf(cachedModifyDate, "%s, %d %s %d %d:%d:%d GMT",
      cDayName, &cDay, cMonthStr, &cYear, &cHour, &cMinute, &cSecond
  );

  // parse the server file's last-modified date
  sscanf(serverModifyDate, "%s, %d %s %d %d:%d:%d GMT",
      sDayName, &sDay, sMonthStr, &sYear, &sHour, &sMinute, &sSecond
  );

  // convert month from string to int for comparison
  if (strcmp(cMonthStr, "Jan") == 0)
    cMonth = 0;
  else if (strcmp(cMonthStr, "Feb") == 0)
    cMonth = 1;
  else if (strcmp(cMonthStr, "Mar") == 0)
    cMonth = 2;
  else if (strcmp(cMonthStr, "Apr") == 0)
    cMonth = 3;
  else if (strcmp(cMonthStr, "May") == 0)
    cMonth = 4;
  else if (strcmp(cMonthStr, "Jun") == 0)
    cMonth = 5;
  else if (strcmp(cMonthStr, "Jul") == 0)
    cMonth = 6;
  else if (strcmp(cMonthStr, "Aug") == 0)
    cMonth = 7;
  else if (strcmp(cMonthStr, "Sep") == 0)
    cMonth = 8;
  else if (strcmp(cMonthStr, "Oct") == 0)
    cMonth = 9;
  else if (strcmp(cMonthStr, "Nov") == 0)
    cMonth = 10;
  else if (strcmp(cMonthStr, "Dec") == 0)
    cMonth = 11;

  if (strcmp(sMonthStr, "Jan") == 0)
    sMonth = 0;
  else if (strcmp(sMonthStr, "Feb") == 0)
    sMonth = 1;
  else if (strcmp(sMonthStr, "Mar") == 0)
    sMonth = 2;
  else if (strcmp(sMonthStr, "Apr") == 0)
    sMonth = 3;
  else if (strcmp(sMonthStr, "May") == 0)
    sMonth = 4;
  else if (strcmp(sMonthStr, "Jun") == 0)
    sMonth = 5;
  else if (strcmp(sMonthStr, "Jul") == 0)
    sMonth = 6;
  else if (strcmp(sMonthStr, "Aug") == 0)
    sMonth = 7;
  else if (strcmp(sMonthStr, "Sep") == 0)
    sMonth = 8;
  else if (strcmp(sMonthStr, "Oct") == 0)
    sMonth = 9;
  else if (strcmp(sMonthStr, "Nov") == 0)
    sMonth = 10;
  else if (strcmp(sMonthStr, "Dec") == 0)
    sMonth = 11;

  // check if years are different
  if (cYear != sYear) {
    if (cYear > sYear) {
      return 1;
    } else {
      return 0;
    }
  }
  // check if months are different
  if (cMonth != sMonth) {
    if (cMonth > sMonth) {
      return 1;
    } else {
      return 0;
    }
  }
  // check if days are different
  if (cDay != sDay) {
    if (cDay > sDay) {
      return 1;
    } else {
      return 0;
    }
  }
  // check if hours are different
  if (cHour != sHour) {
    if (cHour > sHour) {
      return 1;
    } else {
      return 0;
    }
  }
  // check if minutes are different
  if (cMinute != sMinute) {
    if (cMinute > sMinute) {
      return 1;
    } else {
      return 0;
    }
  }
  // check if seconds are different
  if (cSecond != sSecond) {
    if (cSecond > sSecond) {
      return 1;
    } else {
      return 0;
    }
  }

  // times for both are the same, so return 1
  return 1;
  
}

/*
  gets the last modified field of a given resource from a server
  returns a string of the last modified value
*/
void getServerLastModified(int clientConnfd, int port, char resourceName[], char serverLastModified[]) {
  char buffer[128];

  // send head request to clientConnfd
  sprintf(buffer, "HEAD /%s HTTP/1.1\r\nHost: localhost:%d\r\n\r\n",
      resourceName,
      port
  );
  send(clientConnfd, buffer, strlen(buffer), 0);

  // empty buffer 
  memset(buffer, '\0', 128);

  // TODO: make this more robust
  // receive response from server
  recv(clientConnfd, buffer, 128, 0);

  // copy last modified string into array
  char* pLastModified = strstr(buffer, "Last-Modified: ") + 15;
  memcpy(serverLastModified, pLastModified, strcspn(pLastModified, "\r\n"));
}

// worker thread wrapper
void* t_waitForReq(void* arg) {
  free(arg);
  while (1) {
    /* ---------- START CRIT REGION ---------- */
    int rc = pthread_mutex_lock(&m_queue);
    if (rc) {
      perror("pthread_mutex_lock failed");
      pthread_exit(NULL);
    }

    // wait for a request if the queue of connfds is empty
    while (connQueueCount == 0){
      pthread_cond_wait(&c_gotRequest, &m_queue);
    }

    // pop connfd off front of queue
    int connfd = connQueue[0];
    for (int i = 0; i < connQueueCount-1; ++i) {
      connQueue[i] = connQueue[i+1];
    }
    connQueueCount--;

    pthread_mutex_unlock(&m_queue);
    /* ----------- END CRIT REGION ----------- */

    process_request(connfd);
  }
  return NULL;
}

// called by worker thread wrapper to handle the connection
void process_request(int connfd) {
	char buffer[BUFFER_SIZE];
	char method[16], resource[64], httpVer[10], host[64];
  int clientConnfd = -1;

  /* ---------- START CRIT REGION ---------- */
  // perform load balacing and get the port number of the intended server
  pthread_mutex_lock(&m_healthcheck);
  int port = getServerPort(connfd);
  pthread_mutex_unlock(&m_healthcheck);
  /* ----------- END CRIT REGION ----------- */

  // get the port index for healthchecking
  int portIndex;
  for (int i = 0; i < numOfServerPorts; ++i) {
    if (serverPorts[i] == port) {
      portIndex = i;
    }
  }

  while (1) {
    memset(buffer, '\0', BUFFER_SIZE);
    memset(method, '\0', 16);
    memset(resource, '\0', 64);
    memset(httpVer, '\0', 10);
    memset(host, '\0', 64);

    // read from request from client into buffer
    int requestBytes = recv(connfd, buffer, BUFFER_SIZE, 0);
    if (requestBytes < 0) {
      warn("cannot recieve response from server");
      break;
    }

    // end loop if client stops sending information
    if (requestBytes == 0) {
      break;
    }

    /* TODO: maybe implement this
      char* bufPtr = buffer;                                   // point the the beginning of the header buffer
      int requestBytes = 0;
      do {
        bufPtr += requestBytes;                                // putting this above the recv so i can use it as a strstr arg
        requestBytes += recv(connfd, bufPtr, BUFFER_SIZE, 0);
        if (requestBytes < 0) {
          warn("cannot recieve response from server");
          break;
        }
      } while (strstr(bufPtr, "\r\n\r\n"));                    // loop until you reach the end of the request headers
    */
    
    // parse and validate request headers, exit function if request was bad
    if (!parseRequestHeaders(buffer, connfd, method, resource, httpVer, host)) {
      /* ---------- START CRIT REGION ---------- */
      // update healthcheck, add one to reqSinceLastHC
      pthread_mutex_lock(&m_healthcheck);
      healthchecks[portIndex].entries++;
      healthchecks[portIndex].errors++;
      reqSinceLastHC++;
      // wake up healthcheck thread if its time to perform a healthcheck
      if (reqSinceLastHC >= healthcheckInterval) {
        reqSinceLastHC = 0;
        healthchecksNeeded++;
        pthread_cond_signal(&c_performHC);
      }
      pthread_mutex_unlock(&m_healthcheck);
      /* ----------- END CRIT REGION ----------- */
      break;
    }

    // create new connection with server
    clientConnfd = create_client_socket(port);

    // if server port went down since the last healthcheck
    while (clientConnfd < 0) {
      /* ---------- START CRIT REGION ---------- */
      // perform load balacing and get a new server
      pthread_mutex_lock(&m_healthcheck);
      healthchecks[portIndex].isProblematic = 1;
      port = getServerPort(connfd);
      pthread_mutex_unlock(&m_healthcheck);
      /* ----------- END CRIT REGION ----------- */

      clientConnfd = create_client_socket(port);
    }
    
    /* ---------- START CRIT REGION ---------- */
    // if requested content is in the cache and up to date, send it to client
    pthread_mutex_lock(&m_cache);
    int cachedFilesIndex = checkCache(clientConnfd, port, resource);
    if (cachedFilesIndex >= 0) {
      sendCachedResponseToClient(connfd, cachedFilesIndex);
      pthread_mutex_unlock(&m_cache);
      goto addOneToHC;
    }
    pthread_mutex_unlock(&m_cache);
    /* ----------- END CRIT REGION ----------- */

    // send http request to the server
    send(clientConnfd, buffer, requestBytes, 0);

    /*  TODO: maybe implement this
      int sendBytes = 0;
      do {
        sendBytes += send(clientConnfd, buffer, requestBytes, 0);
      } while (sendBytes < requestBytes);
    */
    
    // forward response from server to client
    fwdResponseToClient(connfd, clientConnfd, resource);

    addOneToHC: ;

    /* ---------- START CRIT REGION ---------- */
    // update healthcheck, add one to reqSinceLastHC (in a crit region)
    pthread_mutex_lock(&m_healthcheck);
    healthchecks[portIndex].entries++;
    reqSinceLastHC++;
    if (reqSinceLastHC >= healthcheckInterval) {
      reqSinceLastHC = 0;
      healthchecksNeeded++;
      pthread_cond_signal(&c_performHC);
    }
    pthread_mutex_unlock(&m_healthcheck);
    /* ----------- END CRIT REGION ----------- */
  }	

  if (clientConnfd != -1) {
    close(clientConnfd);
  }
  close(connfd);
}

void sendCachedResponseToClient(int connfd, int index) {
  char headers[64];

  // send headers to client
  sprintf(headers, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",
      cachedFiles[index].contentLength
  );
  send(connfd, headers, strlen(headers), 0);

  // send body to client
  send(connfd, cachedFiles[index].content, cachedFiles[index].contentLength, 0);
}

// parses request headers contained in the buffer into the arrays
// returns 0 if request was bad, 1 if it was fine
int parseRequestHeaders(char buffer[], int connfd, char method[], char resource[], char httpVer[], char host[]) {
  sscanf(buffer, "%s /%s %s", method, resource, httpVer);
	if (strstr(buffer, "Host: ") == NULL) {       // check is host field exists
		send_response_fail(connfd, 400);
    return 0;
	}
  // get host name if it exists
	char* pHost = strstr(buffer, "Host: ") + 6;
	memcpy(host, pHost, strcspn(pHost, "\r\n"));
	
	if (strcmp(method, "GET") != 0) {             // is method valid
		send_response_fail(connfd, 501);
    return 0;
	}
	if (!valid_filename(resource)) {              // is filename/resource valid
		send_response_fail(connfd, 400);
    return 0;
	}
	if (strcmp(httpVer, "HTTP/1.1") != 0) {       // is http version valid (1.1)
		send_response_fail(connfd, 400);
    return 0;
	}
	int hostLen = strlen(host);
	for (int i = 0; i < hostLen; ++i) {           // is host name valid
		if (isspace(host[i])) {
			send_response_fail(connfd, 400);
      return 0;
		}
	}

  return 1;
}

// forwards response from server to client
void fwdResponseToClient(int connfd, int clientConnfd, char resourceName[]) {
  char buffer[BUFFER_SIZE];

  // receive response from server
  int responseBytes = recv(clientConnfd, buffer, BUFFER_SIZE, 0);

  // get the status code from buffer
  int statusCode = 0;
  char statusCodeStr[4];
  char* pBufferParser = strstr(buffer, "HTTP/") + 9;
  memcpy(statusCodeStr, pBufferParser, 3);
  statusCode = atoi(statusCodeStr);

  // get content length from buffer
  int contentLen = 0;
  char contentLenStr[16];
  pBufferParser = strstr(buffer, "Content-Length: ") + 16;
  memcpy(contentLenStr, pBufferParser, strcspn(pBufferParser, "\r\n"));
  contentLen = atoi(contentLenStr);

  // get last modified date from buffer
  char lastModified[40];
  if (contentLen <= maxCachedBytes && statusCode < 300) {
    pBufferParser = strstr(buffer, "Last-Modified: ") + 15;
    memcpy(lastModified, pBufferParser, strcspn(pBufferParser, "\r\n"));
  }

  // point to beginning of the body of the response in the buffer
  pBufferParser = strstr(buffer, "\r\n\r\n") + 4;

  // get the length of the body in bytes
  char* endPtr = buffer + responseBytes;  // since chars are only one byte, we dont have to multiply by sizeof * char
  int currentLen = endPtr - pBufferParser;

  // start saving the body message for caching 
  char body[maxCachedBytes];
  char* pEndOfBody = body;
  if (contentLen <= maxCachedBytes && statusCode < 300) {
    memmove(body, pBufferParser, currentLen+1);
    pEndOfBody = body + currentLen; // move pointer to where the new end of the body is
  }

  // send response to client
  send(connfd, buffer, responseBytes, 0);

  // receive more data if we expect the body to be longer than what we received
  while (currentLen < contentLen) {
    responseBytes = recv(clientConnfd, buffer, BUFFER_SIZE, 0);
    if (responseBytes < 0) {
      warn("cannot recieve response from server");
      break;
    }

    // update the length of the body that we've received so far
    currentLen += responseBytes;

    // concat body message onto your string for caching
    if (contentLen <= maxCachedBytes && statusCode < 300) {
      memmove(pEndOfBody, buffer, responseBytes);
      pEndOfBody += responseBytes; // move pointer to where the new end of the body is
    }

    // send response to client
    send(connfd, buffer, responseBytes, 0);
  }

  // cache response
  if (contentLen <= maxCachedBytes && statusCode < 300) {
    /* ---------- START CRIT REGION ---------- */
    pthread_mutex_lock(&m_cache);

    int index = 0;

    // check to see if the file already exists in cache (its out of date and needs to me replaced)
    for (int i = 0; i < numOfCachedFiles; ++i) {
      if (strcmp(resourceName, cachedFiles[i].resourceName) == 0) {
        index = i;
      }
    }

    // shift all files in the queue forward one
    // (or it already existed in the cache, shift all files behind it forward by one)
    for (index = index; index < numOfCachedFiles-1; ++index) {
      strcpy(cachedFiles[index].resourceName, cachedFiles[index+1].resourceName);
      strcpy(cachedFiles[index].lastModified, cachedFiles[index+1].lastModified);
      memcpy(cachedFiles[index].content, cachedFiles[index+1].content, cachedFiles[index+1].contentLength);
      cachedFiles[index].contentLength = cachedFiles[index+1].contentLength;
    }

    // now index is at the end of the cache, so we insert the new file into the cache
    strcpy(cachedFiles[index].resourceName, resourceName);
    strcpy(cachedFiles[index].lastModified, lastModified);
    memcpy(cachedFiles[index].content, body, contentLen);
    cachedFiles[index].contentLength = contentLen;

    pthread_mutex_unlock(&m_cache);
    /* ----------- END CRIT REGION ----------- */
  }

}

void send_response_fail(int connfd, int statusCode) {
	char headers[64];
	sprintf(headers, "HTTP/1.1 %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        statusCode,
        generate_status_msg(statusCode),
        strlen(generate_status_msg(statusCode)) + 1,
        generate_status_msg(statusCode)
    );
    send(connfd, headers, strlen(headers), 0);
}

const char* generate_status_msg(int code) {
  switch(code) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 400:
      return "Bad Request";
    case 403:
      return "Forbidden";
    case 404:
      return "File Not Found";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
  }
  return "error code fallthrough";
}