// Copy Paste Daemon - CopyPasted
// for send and receive text over the network
// connected at the some wifi router

#include <Clipboard.h>
#include <Application.h>
#include <Message.h>

#include <stdio.h>
#include <stdlib.h>     /* exit, EXIT_FAILURE */
#include <sys/wait.h>   /* wait */

#include "string.h"
#include "unistd.h"
#include "sys/un.h"
#include "sys/socket.h"

#include <netdb.h>
#include <netinet/in.h>

int main(int argc, char *argv[]) {
	const char *buffer;
    ssize_t textLen;
    BApplication app("application/x-vnd.luuvki.CopyPasted");
	
#ifdef WIN32
	if (!getenv("HOME")) putenv("HOME=C:\\");
#else

#endif
	
	printf("Home folder <%s>\n", getenv("HOME"));
	
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;

    struct hostent *server;
    
    portno = 5001;

    // create socket and get file descriptor
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    server = gethostbyname("127.0.0.1");

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);


    // connect to server with server address which is set above (serv_addr)

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR while connecting");
        exit(1);
    }

    // inside this while loop, implement communicating with read/write or send/recv function
    while (1) {
		//char buffer[256];
        //printf("Find Message: ");
        //bzero(buffer,256);
        //scanf("%s", buffer);
        
        
        BMessage *clip = (BMessage *)NULL;
    	if (be_clipboard->Lock()) {
        if ((clip = be_clipboard->Data()))
            clip->FindData("text/plain", B_MIME_TYPE,(const void **)&buffer, &textLen);
            printf(buffer);
    		/////
    		n = write(sockfd,buffer,strlen(buffer));

        	if (n < 0){
          	  perror("ERROR while writing to socket");
          	  exit(1);
       	 	}

        	//bzero(buffer,256);
        	//n = read(sockfd, buffer, 255);

        	//if (n < 0){
            //	perror("ERROR while reading from socket");
            //	exit(1);
        	//}
    		/////
    		be_clipboard->Unlock();
    		//be_clipboard->Clear();
    	}
        
        // printf("server replied: %s \n", buffer);

        // escape this loop, if the server sends message "quit"

        if (!bcmp(buffer, "quit", 4))
            break;
            
        // Wait
        usleep(10L);
    }
    return 0;
}
