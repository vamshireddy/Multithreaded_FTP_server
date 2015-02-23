#include "common.h"
#ifndef PROTO
#define PROTO
#include "protocol.h"
#endif
#include "socket_utilities.h"

void sig_term_handler()
{
	// Kill all threads
	printf("FTP Server exiting\n");
	exit(0);
}

void sig_pipe_handler()
{
	printf("Client terminated because of Pipe termination\n");
}

// Runs the server socket on port 21
void client_function(void* var)
{
	// All open descriptors are stored here for the client
	int open_desc[MAX_OPEN_DESC];
	int open_desc_count = 0;
	// get the client socket
	// Copy the args to local variables
	int client_sock = (int)var;
	// Add fd to the list
	open_desc[open_desc_count++] = client_sock;
	assert(client_sock);

	printf("Client connected\n");
	
	// This structure is for data connection
	struct sockaddr_in active_client_addr;
	// This is for the requests
	ftp_request_t request;
	// Clear the contents
	bzero((void*)&active_client_addr,sizeof(active_client_addr));
	bzero((void*)&request,sizeof(request));
	// Now send the greeting to the client.
	Write(client_sock, greeting, strlen(greeting), open_desc, open_desc_count);
	// Command and arg to be recieved from the client
	
	char* command;
	char* arg;
	for( ;; )
	{
		// Start serving the requests
		// This method will overwrite the existing command and arg
		if( read_request(client_sock, &request, open_desc, open_desc_count) ==  0 )
		{
			printf("Bad Command\n");
			Write(client_sock, error, strlen(error), open_desc, open_desc_count);
			break;
		}
		// get the command and arg from the structure
		command = request.command;
		arg = request.arg;
		printf("%s : %s\n",command,arg);
		
		// Process the command
		if( strcmp(command,"USER") == 0 )
		{
			// USER REQUEST
			Write(client_sock, allow_user, strlen(allow_user), open_desc, open_desc_count);
		}
		else if( strcmp(command,"SYST") == 0 )
		{
			// SYSTEM REQUEST
			Write(client_sock, system_str, strlen(system_str), open_desc, open_desc_count);
		}
		else if( strcmp(command,"PORT") == 0 )
		{
			// PORT COMMAND
			// Send reply
			Write(client_sock, port_reply, strlen(port_reply), open_desc, open_desc_count);
			// Argument is of form h1,h2,h3,h4,p1,p2
			store_ip_port_active(arg,&active_client_addr);
		}
		else if( strcmp(command,"TYPE") == 0 )
		{
			Write(client_sock, type_ok, strlen(type_ok), open_desc, open_desc_count);
		}
		else if( strcmp(command,"QUIT") == 0 )
		{
			// Child exited
			// break and free the client_sock
			break;
		}
		else if( (strcmp(command,"LIST") == 0 ) || (strcmp(command,"RETR") == 0 ))
		{
			// RETR
			// Argument will have the file name
			int file;
			if( strcmp(command,"LIST") ==  0)
			{
				// Execute the list command and store it in a file
				system("ls -l > .list");
				// Now open that file and send it to the client
				file = open(".list",O_RDONLY);
			}
			else
			{
				file = open(arg,O_RDONLY);
			}

			if( file == -1 )
			{
				perror("Open");
				Write(client_sock, file_error, strlen(file_error), open_desc, open_desc_count);
				// If the file open has error, quit the connection.
				break;
			}
			// FILE OK
			open_desc[open_desc_count++] = file;

			Write(client_sock, file_ok, strlen(file_ok), open_desc, open_desc_count);
			
			// Now transfer the file to the client
			int data_sock = Socket(AF_INET, SOCK_STREAM, 0, open_desc, open_desc_count);
			open_desc[open_desc_count++] = data_sock;	
			
			if( connect(data_sock, (struct sockaddr*)&active_client_addr, sizeof(active_client_addr))  == -1 )
			{
				printf("Cant establish data connection to %d\n", ntohs(active_client_addr.sin_port));
				// Close existing fd's related to this command
				break;
			}
			// Now transfer the file.
			int n;
			char data_buff[BUFF_SIZE];
			while( ( n = Read(file, data_buff,BUFF_SIZE, open_desc, open_desc_count) ) > 0 )
			{
				Write( data_sock, data_buff, n, open_desc, open_desc_count);
			}
			// File transferred succesfully
			// Send reply now
			Write( client_sock, file_done, strlen(file_done), open_desc, open_desc_count);
			break;
		}
		else
		{
			Write( client_sock, error, strlen(error), open_desc, open_desc_count);
		}
	}
	Write( client_sock, close_con, strlen(close_con), open_desc, open_desc_count);
	printf("Closing client connection and killing THREAD\n");
	clean_all_fds(open_desc,open_desc_count);
	pthread_exit(0);
}


int main()
{
	int open_desc[1];
	int open_desc_count;
	sigignore(SIGPIPE);
	signal(SIGTERM, sig_term_handler);
	// Change the current working directory to the FILES folder.
	if( chdir("../FTP_FILES") == -1 )
	{
		perror("CWD");
		exit(0);
	}

	// This is the listen socket on 21
	int listen_sock = Socket(AF_INET, SOCK_STREAM, 0, open_desc, open_desc_count);
	struct sockaddr_in server_addr;
	
	server_addr.sin_port = htons(21);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htons(INADDR_ANY);

	Bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
	Listen(listen_sock, BACKLOG);
	printf("FTP SERVER STARTED\n");
	// thread id
	pthread_t pid;
	// Create a client addr structure
	struct sockaddr_in client_addr;
	int client_addr_len = 0;
	int client_sock;
	// Accept the connections
	while( TRUE )
	{
		printf("LISTENING FOR CLIENTS\n");
		client_sock = Accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
		// Now create a new thread for this client.	
		// Pass the client sock fd as the argument.
		if( pthread_create(&pid, NULL, (void*)client_function, (void*)client_sock ) != 0 )
		{
			perror("pthread create in main");
			close(client_sock);
		}
	}
}
