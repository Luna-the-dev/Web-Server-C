#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define BUFFER_SIZE 512
#define QUEUE_SIZE 512

pthread_mutex_t m_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_activeFile = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_logFile = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_gotRequest = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_accessFile = PTHREAD_COND_INITIALIZER;

int connQueue[QUEUE_SIZE]; // queue of connfd
int connQueueCount = 0;

// creating an array of pairs (file name, read/write status) that the threads
// use to communicate with eachother whether they are using a certain file.
// size is dynamically allocated
struct FileInfo {
  char fileName[19];
  int isBeingWritten;
};
struct FileInfo* activeFiles;

int logFileDesc = -1;
uint16_t port;

int numOfThreads = 5;

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

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t portNum) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    err(EXIT_FAILURE, "socket error");
  }

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY);
  addr.sin_port = htons(portNum);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    err(EXIT_FAILURE, "bind error");
  }

  if (listen(listenfd, 500) < 0) {
    err(EXIT_FAILURE, "listen error");
  }

  return listenfd;
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

void logRequest(int statusCode, char* requestCmd, int threadNum, int contentLength, char* httpVer, char* firstThouBytes, int FTBLen) {
  char log[2100];

  if (statusCode >= 300) {
    sprintf(log, "FAIL\t%s /%s HTTP/%s\t%d\n",
        requestCmd,
        activeFiles[threadNum].fileName,
        httpVer,
        statusCode
    );
  } else if (strcmp(requestCmd, "HEAD") == 0) {
    sprintf(log, "%s\t/%s\tlocalhost:%d\t%d\n",
        requestCmd,
        activeFiles[threadNum].fileName,
        port,
        contentLength
    );
  } else if (strcmp(requestCmd, "GET") == 0) {
    // read first 1000 bytes from the file
    char asciiBuf[1000];
    int file = open(activeFiles[threadNum].fileName, O_RDONLY);
    int bytesRead = read(file, asciiBuf, 1000);
    if (bytesRead < 0) {
      warn("cannot open file for reading");
    }

    // convert it to hex
    char firstThouBytesHex[bytesRead*2];
    for (int i = 0; i < bytesRead; ++i) {
      sprintf(&firstThouBytesHex[i*2], "%02x", asciiBuf[i]);
    }

    sprintf(log, "%s\t/%s\tlocalhost:%d\t%d\t%s\n",
        requestCmd,
        activeFiles[threadNum].fileName,
        port,
        contentLength,
        firstThouBytesHex
    );
  } else if (strcmp(requestCmd, "PUT") == 0) {
    // convert first 1000 bytes to hex
    char firstThouBytesHex[FTBLen*2];
    for (int i = 0; i < FTBLen; ++i) {
      sprintf(&firstThouBytesHex[i*2], "%02x", firstThouBytes[i]);
    }

    sprintf(log, "%s\t/%s\tlocalhost:%d\t%d\t%s\n",
        requestCmd,
        activeFiles[threadNum].fileName,
        port,
        contentLength,
        firstThouBytesHex
    );
  }

  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_logFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  write(logFileDesc, log, strlen(log));

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_logFile);
}

void healthcheck(int connfd, char* httpVer) {
  int numOfEntries = 0;
  int numOfErrors = 0;
  int statusCode = 200;

  char buffer[BUFFER_SIZE];
  int firstEntry = 1;

  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_logFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }
  
  while (1) {
    // reading in BUFFER_SIZE btyes into the buffer
    int bytesRead = read(logFileDesc, buffer, BUFFER_SIZE);
    if (bytesRead < 0) {
      warn("cannot open file for reading");
      statusCode = 500;
      break;
    }

    // checking to see if first logged entry is FAIL
    if (firstEntry && strncmp(buffer, "FAIL", 4) == 0) {
      numOfErrors++;
      firstEntry = 0;
    }

    // checking buffer for newlines and FAIL entries
    for (int i = 0; i < bytesRead; ++i) {
      if (buffer[i] == '\n') {
        numOfEntries++;
        // if there is another entry and if it is a FAIL
        if (i < bytesRead-4 && buffer[i+1] == 'F' && buffer[i+2] == 'A' && buffer[i+3] == 'I' && buffer[i+4] == 'L') {
          numOfErrors++;
        }
      }
    }

    if (bytesRead < BUFFER_SIZE) {
      break;
    }
  }

  // combining the # of errors and entries into a string so we can measure
  // the length of it for the headers
  char content[32];
  sprintf(content, "%d\n%d", numOfErrors, numOfEntries);

  // sending healthcheck to client
  char healthcheck[64];

  if (statusCode >= 300) {
    sprintf(healthcheck, "HTTP/%s %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        httpVer,
        statusCode,
        generate_status_msg(statusCode),
        strlen(generate_status_msg(statusCode)) + 1,
        generate_status_msg(statusCode)
    );
    send(connfd, healthcheck, strlen(healthcheck), 0);
  } else {
    sprintf(healthcheck, "HTTP/%s %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        httpVer,
        statusCode,
        generate_status_msg(statusCode),
        strlen(content) + 1,
        content
    );
    send(connfd, healthcheck, strlen(healthcheck), 0);
  }
  if (logFileDesc != -1) {
    // convert healthcheck to hex
    int len = strlen(healthcheck);
    char healthcheckHex[len*2];
    for (int i = 0; i < len; ++i) {
      sprintf(&healthcheckHex[i*2], "%02x", healthcheck[i]);
    }

    char log[264];
    sprintf(log, "GET\t/healthcheck\tlocalhost:%d\t%d\t%s\n",
        port,
        len,
        healthcheckHex
    );

    write(logFileDesc, log, strlen(log));
  }

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_logFile);

  return;
}

int send_headers(int connfd, int threadNum, char buffer[], char* requestCmd) {
  int statusCode = 200;

  char fileName[20];
  memset(fileName, '\0', 20); // this is here to fix a buf with the file names
  
  // pointing to the start of the file name
  char* pFileName = strchr(buffer, '/') + 1;
  if (strcspn(pFileName, " ") > 19) {
    statusCode = 400;
    goto SkipOpenFile;
  }
  memcpy(fileName, pFileName, strcspn(pFileName, " ")); // copying the file name into fileName[]

  // check to see if filename is valid
  if (!valid_filename(fileName)) {
    statusCode = 400;
    goto SkipOpenFile;
  }

  // getting the http version
  char httpVer[4];
  char* pHttpVer = strstr(buffer, "HTTP/") + 5;
  memcpy(httpVer, pHttpVer, 3);

  // checking to see if healthcheck was requested
  if (strcmp(fileName, "healthcheck") == 0) {
    if (strcmp(requestCmd, "GET") == 0) {
      if (logFileDesc != -1) {
        healthcheck(connfd, httpVer);
        return -1;
      } else { // -l flag was not specified
        statusCode = 404;
        goto SkipOpenFile;
      }
    } else {
      statusCode = 403;
      goto SkipOpenFile;
    }
  }

  // make sure the file isnt currently being written to. if it is, loop until it isnt
  int fileBlocked;
  while (1) {
    fileBlocked = 0;
    for (int i = 0; i < threadNum; ++i) {
      // check to see if file matches one that is already being written to
      if (strcmp(activeFiles[i].fileName, fileName) == 0 &&
          activeFiles[i].isBeingWritten == 1
      ) {
        fileBlocked = 1;
        break;
      }
    }
    if (!fileBlocked)
      break;
  }

  // ALERT: THIS CAUSES RACE CONDITION.
  // if file is not blocked while loop runs, but then becomes blocked while thread is
  // waiting to enter the critical region, this causes a race condition.


  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_activeFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // mark file as being read from
  strcpy(activeFiles[threadNum].fileName, fileName);
  activeFiles[threadNum].isBeingWritten = 0;

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_activeFile);


  // open the file
  int file = open(fileName, O_RDONLY);

  // check file perms
  if (file < 0) {
    if (errno == EACCES)
      statusCode = 403;
    else
      statusCode = 404;
  }

  // CHECKING THE PORT NUMBER

  char portName[5];
  memset(portName, '\0', 5);

  // checking to see if the port number is valid
  char* pPortName = strstr(buffer, "Host:") + 16;
  if (strcspn(pPortName, "\r\n") > 5) {
    statusCode = 400;
    goto SkipOpenFile;
  }
  memcpy(portName, pPortName, strcspn(pPortName, "\r\n"));
  
  for (unsigned int i = 0; i < strcspn(pPortName, "\r\n"); ++i) {
    if (!isdigit(portName[i])) {
      statusCode = 400;
      goto SkipOpenFile;
    }
  }

  unsigned int portNum = atoi(portName);

  if (portNum > 32767) {
    statusCode = 400;
    goto SkipOpenFile;
  }

  SkipOpenFile: ;

  char headers[64];
  // sending response if not successful
  if (statusCode >= 300) {
    sprintf(headers, "HTTP/%s %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        httpVer,
        statusCode,
        generate_status_msg(statusCode),
        strlen(generate_status_msg(statusCode)) + 1,
        generate_status_msg(statusCode)
    );
    send(connfd, headers, strlen(headers), 0);
    if (logFileDesc != -1) {
      logRequest(statusCode, requestCmd, threadNum, 0, httpVer, NULL, 0);
    }
    return -1;
  }

  // getting the content length
  // taken from: https://stackoverflow.com/a/3138754
  struct stat* buf = malloc(sizeof(struct stat));
  stat(fileName, buf);
  int contentLength = buf->st_size;
  free(buf);

  // sending headers as response
  sprintf(headers, "HTTP/%s %d %s\r\nContent-Length: %d\r\n\r\n",
      httpVer,
      statusCode,
      generate_status_msg(statusCode),
      contentLength
  );
  send(connfd, headers, strlen(headers), 0);
  if (logFileDesc != -1) {
    logRequest(statusCode, requestCmd, threadNum, contentLength, httpVer, NULL, 0);
  }
  return file;
}

void get_req(int connfd, int threadNum, char buffer[]) {

  // send the headers to client, returning the file
  int file = send_headers(connfd, threadNum, buffer, "GET");

  // file is less than 0 if the file was not found
  // OR healthcheck was performed
  if (file < 0) {
    return;
  }

  // getting the text from the file
  while (1) {
    // reading in BUFFER_SIZE btyes into the buffer
    int bytesRead = read(file, buffer, BUFFER_SIZE);
    if (bytesRead < 0) {
      warn("cannot open file for reading");
      break;
    }

    // if you've reached the end of the file: output whatever is remaining and jump to the end
    if (bytesRead < BUFFER_SIZE) {
      send(connfd, buffer, bytesRead, 0);
      break;
    }

    // output BUFFER_SIZE bytes and continue
    send(connfd, buffer, BUFFER_SIZE, 0);
  }

  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_activeFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // mark file as not being used anymore
  memset(activeFiles[threadNum].fileName, '\0', 19);

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_activeFile);


  if(close(file) < 0) {
    warnx("file close fail get");
  }
  return;
}

void put_req(int connfd, int threadNum, char buffer[]) {

  char fileName[20];
  memset(fileName, '\0', 20); // this is here to fix a buf with the file names
  char* pFileName;
  int statusCode = 200;

  // OPEN FILE AND CHECK FOR ERRORS

  // pointing to the start of the file name
  pFileName = strchr(buffer, '/') + 1;

  // copying the file name into fileName[]
  memcpy(fileName, pFileName, strcspn(pFileName, " "));

  // check to see if filename is valid
  if (!valid_filename(fileName)) {
    statusCode = 400;
    goto SkipOpenFile;
  }

  // checking to see if healthcheck was requested
  // send back error because you cant PUT a healthcheck
  if (strcmp(fileName, "healthcheck") == 0) {
    statusCode = 403;
    goto SkipOpenFile;
  }

  // getting the http version
  char httpVer[4];
  char* pHttpVer = strstr(buffer, "HTTP/") + 5;
  memcpy(httpVer, pHttpVer, 3);

  // make sure the file isnt currently being written to. if it is, loop until it isnt
  int fileBlocked;
  while (1) {
    fileBlocked = 0;
    for (int i = 0; i < threadNum; ++i) {
      // check to see if file matches one that is already being written to
      if (strcmp(activeFiles[i].fileName, fileName) == 0 &&
          activeFiles[i].isBeingWritten == 1
      ) {
        fileBlocked = 1;
        break;
      }
    }
    if (!fileBlocked)
      break;
  }

  // ALERT: THIS CAUSES RACE CONDITION.
  // if file is not blocked while loop runs, but then becomes blocked while thread is
  // waiting to enter the critical region, this causes a race condition.


  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_activeFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // mark file as being read from
  strcpy(activeFiles[threadNum].fileName, fileName);
  activeFiles[threadNum].isBeingWritten = 0;

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_activeFile);


  // open the file and truncate it
  int file = open(fileName, O_WRONLY | O_TRUNC);

  // check for file permissions. if file doesnt exist, create a new file
  if (file < 0) {
    if (errno == EACCES) {
      statusCode = 403;
      goto SkipOpenFile;
    }
    else {
      file = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      statusCode = 201;
      goto SkipOpenFile;
    }
  }

  // if for some reason this still failed, throw an error
  if (file < 0 && statusCode < 300) {
    statusCode = 500;
    goto SkipOpenFile;
  }

  // CHECKING THE PORT NUMBER

  char portName[5];
  memset(portName, '\0', 5);

  // checking to see if the port number is valid
  char* pPortName = strstr(buffer, "Host:") + 16;
  if (strcspn(pPortName, "\r\n") > 5) {
    statusCode = 400;
    goto SkipOpenFile;
  }
  memcpy(portName, pPortName, strcspn(pPortName, "\r\n"));
  
  for (unsigned int i = 0; i < strcspn(pPortName, "\r\n"); ++i) {
    if (!isdigit(portName[i])) {
      statusCode = 400;
      goto SkipOpenFile;
    }
  }

  unsigned int portNum = atoi(portName);

  if (portNum > 32767) {
    statusCode = 400;
    goto SkipOpenFile;
  }


  // CHECKING THE CONTENT LENGTH

  char conLenName[16];
  memset(conLenName, '\0', 16);

  // checking to see if the content length is valid
  char* pContentLength = strstr(buffer, "Content-Length:") + 16;
  if (!pContentLength) {
    statusCode = 400;
    goto SkipOpenFile;
  }
  memcpy(conLenName, pContentLength, strcspn(pContentLength, "\r\n"));
  
  for (unsigned int i = 0; i < strcspn(pContentLength, "\r\n"); ++i) {
    if (!isdigit(conLenName[i])) {
      statusCode = 400;
      goto SkipOpenFile;
    }
  }

  SkipOpenFile: ;

  // WRITE THE FILE TO SERVER

  // getting the text from the body 
  char bufferBody[BUFFER_SIZE];
  char firstThouBytes[1000];
  int iteration = 0;
  while (1) {
    // reading in BUFFER_SIZE btyes into the buffer
    int bytesRead = recv(connfd, bufferBody, BUFFER_SIZE, 0);
    if (bytesRead < 0) {
      warn("cannot open '%s' for reading", fileName);
      statusCode = 500;
      break;
    }

    // copy the first 1000 bytes for the logfile
    if (iteration == 0 && logFileDesc != -1) {
      if (bytesRead < 1000)
        memcpy(firstThouBytes, bufferBody, bytesRead);
      else
        memcpy(firstThouBytes, bufferBody, 1000);
    }

    // if you've reached the end of the file: output whatever is remaining and jump to the end
    if (bytesRead < BUFFER_SIZE) {
      write(file, bufferBody, bytesRead);
      break;
    }

    // output BUFFER_SIZE bytes and continue
    write(file, bufferBody, BUFFER_SIZE);
  }

  // getting the content length
  // taken from: https://stackoverflow.com/a/3138754
  struct stat* buf = malloc(sizeof(struct stat));
  stat(fileName, buf);
  int contentLength = buf->st_size;
  free(buf);

  // log the request in the logfile
  if (logFileDesc != -1) {
    logRequest(statusCode, "PUT", threadNum, contentLength, httpVer, firstThouBytes, strlen(firstThouBytes));
  }

  // START CRITICAL REGION
  rc = pthread_mutex_lock(&m_activeFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // mark file as not being used anymore
  memset(activeFiles[threadNum].fileName, '\0', 19);

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_activeFile);


  // SEND RESPONSE BACK

  char headers[64];
  // sending response if not successful
  if (statusCode >= 300) {
    sprintf(headers, "HTTP/%s %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        httpVer,
        statusCode,
        generate_status_msg(statusCode),
        strlen(generate_status_msg(statusCode)) + 1,
        generate_status_msg(statusCode)
    );
    send(connfd, headers, strlen(headers), 0);
    if (file > 0) {
      if(close(file) < 0) {
        warnx("file close fail put code > 300");
      }
    }
    return;
  }

  // output the headers including the content-Length
  sprintf(headers, "HTTP/%s %d %s\r\nContent-Length: %lu\r\n\r\n%s\n",
      httpVer,
      statusCode,
      generate_status_msg(statusCode),
      (strlen(generate_status_msg(statusCode)) + 1),
      generate_status_msg(statusCode)
  );
  send(connfd, headers, strlen(headers), 0);

  if(close(file) < 0) {
    warnx("file close fail put code < 300");
  }
  return;
}

void head_req(int connfd, int threadNum, char buffer[]) {

  // send the headers to client
  int file = send_headers(connfd, threadNum, buffer, "HEAD");

  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_activeFile);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // mark file as not being used anymore
  memset(activeFiles[threadNum].fileName, '\0', 19);

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_activeFile);

  if(file < 0) {
    return;
  }

  if(close(file) < 0) {
    warnx("file close fail head");
  }
  return;
}

// process called by worker threads
void process_request(int connfd, int threadNum) {
  char buffer[BUFFER_SIZE];     // buffer for reading in text BUFFER_SIZE bytes at a time

  recv(connfd, buffer, BUFFER_SIZE, 0);
 
  char command[10];
  memset(command, '\0', 10);
  memcpy(command, buffer, strcspn(buffer, " "));

  if (strcmp(command, "GET") == 0) {
    get_req(connfd, threadNum, buffer);
  } else if (strcmp(command, "PUT") == 0) {
    put_req(connfd, threadNum, buffer);
  } else if (strcmp(command, "HEAD") == 0) {
    head_req(connfd, threadNum, buffer);
  } else {
    // request isnt GET, PUT, or HEAD
    char headers[64];
    int statusCode = 501;
    sprintf(headers, "HTTP/1.1 %d %s\r\nContent-Length: %ld\r\n\r\n%s\n",
        statusCode,
        generate_status_msg(statusCode),
        strlen(generate_status_msg(statusCode)) + 1,
        generate_status_msg(statusCode)
    );
    send(connfd, headers, strlen(headers), 0);
  }

  // when done, close socket
  close(connfd);
}

int openLogFile(char* logFileName) {

  // open the file
  int file = open(logFileName, O_RDWR | O_APPEND);

  // check for file permissions. if file doesnt exist, create a new file and return
  if (file < 0) {
    if (errno == EACCES) {
      errx(EXIT_FAILURE, "the log file does not have read/write permissions open to this program");
    }
    else {
      file = open(logFileName, O_RDWR | O_CREAT, 0777);
      return file;
    }
  }

  char buffer[BUFFER_SIZE];
  int tabCount = 0;
  // getting the text from the file
  while (1) {
    // reading in BUFFER_SIZE btyes into the buffer
    int bytesRead = read(file, buffer, BUFFER_SIZE);
    if (bytesRead < 0) {
      warn("cannot open file for reading");
      break;
    }

    // verify that the existing logfile is in the correct format
    for (int i = 0; i < bytesRead; ++i) {
      if (buffer[i] == '\t') {
        tabCount++;
      } else if (buffer[i] == '\n') {
        // check to see if there are correct # of tabs in the line
        if (tabCount < 2 || tabCount > 4) {
          errx(EXIT_FAILURE, "%s does not follow the correct format", logFileName);
        }
        tabCount = 0;
      }
    }

    // reached the EOF
    if (bytesRead < BUFFER_SIZE) {
      break;
    }
  }

  return file;
}

void parseServerArgs(int argc, char *argv[]) {
	int opt;
  
  // parsing through the flags
  while((opt = getopt(argc, argv, ":n:l:")) != -1) {
    switch (opt) {
      case 'n':
        numOfThreads = atoi(optarg);
        break;
      case 'l':
        logFileDesc = openLogFile(optarg);
        break;
      case ':':
        errx(EXIT_FAILURE, "option -%c needs a value", optopt);
      case '?':
        errx(EXIT_FAILURE, "unknown option -%c", optopt);
    }
  }

  // getting the port number
  port = strtouint16(argv[optind]);
  if (port == 0) {
    errx(EXIT_FAILURE, "invalid port number: %d", optind);
  }
}

// main worker thread function
void* t_wait_for_req(void* arg) {
  // getting the thread number
  int* p_threadNum = (int*) arg;
  int threadNum = *p_threadNum;

  while (1) {
    // START CRITICAL REGION
    int rc = pthread_mutex_lock(&m_queue);
    if (rc) {
      perror("pthread_mutex_lock failed");
      pthread_exit(NULL);
    }

    while (connQueueCount == 0){
      pthread_cond_wait(&c_gotRequest, &m_queue);
    }

    // pop conndf off front of queue
    int connfd = connQueue[0];
    for (int i = 0; i < connQueueCount-1; ++i) {
      connQueue[i] = connQueue[i+1];
    }
    connQueueCount--;

    // END CRITICAL REGION
    pthread_mutex_unlock(&m_queue);

    process_request(connfd, threadNum);
  }
  return NULL;
}

// producer function
void handle_connection(int connfd) {
  // wait if queue is full
  while (connQueueCount == QUEUE_SIZE) {  }

  // START CRITICAL REGION
  int rc = pthread_mutex_lock(&m_queue);
  if (rc) {
    perror("pthread_mutex_lock failed");
    pthread_exit(NULL);
  }

  // push connfd onto queue
  connQueue[connQueueCount] = connfd;
  connQueueCount++;

  // END CRITICAL REGION
  pthread_mutex_unlock(&m_queue);

  // broadcast to worker threads that there is a connection(s) to grab
  pthread_cond_broadcast(&c_gotRequest);
}

int main(int argc, char *argv[]) {
  int listenfd;

  // move data from args into variables
  parseServerArgs(argc, argv);

  // declare the length of the active files array
  activeFiles = malloc(numOfThreads * sizeof *activeFiles);

  // create array of n threads
	pthread_t t_ids[numOfThreads];
  int args[numOfThreads]; // passes thread number to each thread
	for (int i = 0; i < numOfThreads; ++i) {
    args[i] = i;
		if (pthread_create(&t_ids[i], NULL, &t_wait_for_req, &args[i]) != 0) {
			perror("Failed to create thread");
		}
	}

  listenfd = create_listen_socket(port);

  while(1) {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }
    handle_connection(connfd);
  }

  close(logFileDesc);
  free(activeFiles);
  return EXIT_SUCCESS;
}
