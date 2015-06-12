/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11406
 *         Name: Won Wook SONG
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/*
 * Functions to define
 */
void *process_request(void* vargp);
void return_errormsg(int fd, char *msg);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
void print_log(char *host, int portnum, int size, char* msg);

// pointer to the log file
FILE* log_file;
// mutexes
sem_t mutex;
int totalcnt;

// struct for the arguments passed onto the thread
struct thread_args {
    int connectfd;
    int connectport;
    char hostaddr[MAXLINE];
};

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd, listenport; // listening port num and listening descriptor.
    struct sockaddr_in clientaddr;
    int clientleng = sizeof(clientaddr);
    pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    // ignore sigpipe signals
    Signal(SIGPIPE, SIG_IGN);

    // set up logfile
    log_file = Fopen(PROXY_LOG, "w");
    // initialize totalcnt
    totalcnt = 0;
    // initialize mutex to 1.
    Sem_init(&mutex, 0, 1);

    // set up listening descriptor
    listenport = atoi(argv[1]);
    listenfd = Open_listenfd(listenport);

    while(1) {
        struct thread_args *argp = (struct thread_args*) malloc(sizeof(struct thread_args));

        //set up connection descriptor.
        argp->connectfd = Accept(listenfd, (SA *) &clientaddr, &clientleng);
        argp->connectport = ntohs(clientaddr.sin_port);
        strcpy(argp->hostaddr, inet_ntoa(clientaddr.sin_addr));

        struct hostent *hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);

        printf("Proxy server connected to %s (%s), port %d\n", hp->h_name, argp->hostaddr, argp->connectport);

        // create a thread that processes given request.
        Pthread_create(&tid, NULL, process_request, argp);
    }

    exit(0);
}

/*
 * Each thread processes each request through the variables passed
 */
void *process_request(void* vargp) {
    pthread_t tid = pthread_self();
    printf("Served by thread %u\n", tid);
    Pthread_detach(tid);

    int connectfd = ((struct thread_args*)vargp)->connectfd;
    int connectport = ((struct thread_args*)vargp)->connectport;
    char hostaddr[MAXLINE];
    strcpy(hostaddr, ((struct thread_args*)vargp)->hostaddr);

#ifdef DEBUG
  printf("| process request -> connectfd:%d connectport:%d, hostaddr:%s\n", connectfd, ((struct thread_args*)vargp)->connectport, hostaddr);
#endif
    // this frees argp in main()
    Free(vargp);

    // read stuff from the connectfd.
    char buffer[MAXLINE];
    rio_t rio_client;
    rio_t rio_server;
    int cnt, i;

    Rio_readinitb(&rio_client, connectfd);
    while(1) {
        if((cnt = Rio_readlineb_w(&rio_client, buffer, MAXLINE)) <= 0) {
            printf("Bad request!\n");
            close(connectfd);
            return;
        }

        char *host, *portstr, *msg;
        int clientfd, port;

        P(&mutex);
        totalcnt += cnt;
        V(&mutex);
        printf("Thread %u received %d bytes (%d total)\n", tid, cnt, totalcnt);

        char *proxyerrormsg = "proxy usage: <host> <port> <message>\n";
        // process out the buffer
#ifdef DEBUG
printf("| | string to process:%s", buffer);
#endif

        host = strtok(buffer, " ");
        i = strlen(host);
        if(cnt <= i || cnt == i+2) { // only one argument
            return_errormsg(connectfd, proxyerrormsg);
            continue;
        }
        portstr = strtok(NULL, " ");
        i += strlen(portstr) + 1;
        if(cnt <= i || cnt == i+2 || portstr[0] < '0' || portstr[0] > '9') { // only two arguments OR portstr[0]!=number
            return_errormsg(connectfd, proxyerrormsg);
            continue;
        }
        msg = strtok(NULL, "`");

        P(&mutex);
        port = atoi(portstr);
        V(&mutex);
        if(port <= 0 || port == connectport) { // error with the port number
            return_errormsg(connectfd, proxyerrormsg);
            continue;
        }

#ifdef DEBUG
        printf("| | after processing: host:%s, port:%s, msg:%s", host, portstr, msg);
#endif

        // open clientfd
        if((clientfd = open_clientfd_ts(host, port, &mutex)) < 0) {
            char *openclientfderr = "Warning: unable to connect to end server\n";
            return_errormsg(connectfd, openclientfderr);
            continue;
        }

        // connect to the server
        Rio_readinitb(&rio_server, clientfd);
        Rio_writen_w(clientfd, msg, strlen(msg));
        if((cnt = Rio_readlineb_w(&rio_server, buffer, MAXLINE)) <= 0) {
            return_errormsg(connectfd, proxyerrormsg);
        } else {
            Rio_writen_w(connectfd, buffer, strlen(buffer));
            print_log(hostaddr, port, cnt, buffer);
        }
        Close(clientfd);
    }
    
    // close Rio_readinitb
    Close(connectfd);
    return;
}

/*
 *
 */
void return_errormsg(int fd, char *msg) {
    Rio_writen_w(fd, msg, strlen(msg));
    printf("%s", msg);
    return;
}

/*
 *
 */
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp) {
    int clientfd;
    struct hostent hstnt, *hp = &hstnt, *hp_tmp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
#ifdef DEBUG
    printf("| | | clientfd: %d\n", clientfd);
#endif

    P(mutexp);
    hp_tmp = gethostbyname(hostname);
    if(hp_tmp != NULL) hstnt = *hp_tmp;
    V(mutexp);

    if(hp_tmp == NULL) return -2;

    bzero((char *) &serveraddr, sizeof(serveraddr)); 
    serveraddr.sin_family = AF_INET; 
    bcopy((char *)hp->h_addr, (char *)&serveraddr.sin_addr.s_addr, hp->h_length); 
    serveraddr.sin_port = htons(port);

    if ((connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr))) < 0) return -1;
    return clientfd;
}

/*
 *
 */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) {
    ssize_t read_count;  
    if ((read_count = rio_readn(fd, ptr, nbytes)) < 0) {  
        printf("Warning: rio_readn failed\n");  
        return 0; 
    }
    return read_count;  
}

/*
 *
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t read_count;
    if ((read_count = rio_readlineb(rp, usrbuf, maxlen)) < 0) {  
        printf("Warning: rio_readlineb failed\n");
        return 0;
    }
    return read_count;
}

/*
 *
 */
void Rio_writen_w(int fd, void *usrbuf, size_t n) {
    if (rio_writen(fd, usrbuf, n) != n) {
        printf("Warning: rio_writen failed.\n");
    }
}

/*
 *
 */
void print_log(char *host, int portnum, int size, char* msg) {
    char log_string[MAXLINE]; 

    time_t now;
    char time_str[MAXLINE];

    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    sprintf(log_string, "%s: %s %d %d %s", time_str, host, portnum, size, msg);
#ifdef DEBUG
    printf("| print log:%s", log_string);
#endif

    P(&mutex);
    fprintf(log_file, log_string);
    fflush(log_file);
    V(&mutex);

    return;
}
