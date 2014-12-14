#include "common.h"
/** 
 * Sets up server and handles incoming connections
 * @param port Server port
 */
void server(int port)
{
  int sock = create_socket(port);
  struct sockaddr_in client_address;
  int len = sizeof(client_address);
  int connection, pid, bytes_read;

  while(1){
    connection = accept(sock, (struct sockaddr*) &client_address,&len);
    char buffer[BSIZE];
    Command *cmd = malloc(sizeof(Command));
    State *state = malloc(sizeof(State));
    memset(state,0,sizeof(State));
    pid = fork();
    
    memset(buffer,0,BSIZE);

    if(pid<0){
      fprintf(stderr, "Cannot create child process.");
      exit(EXIT_FAILURE);
    }

    if(pid==0){
      state->tr_pid = 0;
      close(sock);
      char welcome[BSIZE] = "220 ";
      if(strlen(welcome_message)<BSIZE-4){
        strcat(welcome,welcome_message);
      }else{
        strcat(welcome, "Welcome to HEDSPI FTP service.");
      }

      /* Write welcome message */
      strcat(welcome,"\n");
      write(connection, welcome,strlen(welcome));

      /* Read commands from client */
      while (bytes_read = read(connection,buffer,BSIZE)){
        
        signal(SIGCHLD,my_wait);

        if(!(bytes_read>BSIZE)){
          /* TODO: output this to log */
          printf("User %s sent command: %s\n",(!state->username_ok)?"unknown":state->user.username,buffer);
          parse_command(buffer,cmd);
          state->connection = connection;
          
          /* Ignore non-ascii char. Ignores telnet command */
          if(buffer[0]<=127 || buffer[0]>=0){
            response(cmd,state);
          }
          memset(buffer,0,BSIZE);
          memset(cmd,0,sizeof(cmd));
        }else{
          /* Read error */
          perror("server:read");
        }
      }
      printf("Client disconnected.\n");
      exit(0);
    }else{
      printf("closing... :(\n");
      close(connection);
    }
  }
}

/**
 * Creates socket on specified port and starts listening to this socket
 * @param port Listen on this port
 * @return int File descriptor for new socket
 */
int create_socket(int port)
{
  int sock;
  int reuse = 1;

  /* Server addess */
  struct sockaddr_in server_address = (struct sockaddr_in){  
     AF_INET,
     htons(port),
     (struct in_addr){INADDR_ANY}
  };


  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    fprintf(stderr, "Cannot open socket");
    exit(EXIT_FAILURE);
  }

  /* Address can be reused instantly after program exits */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

  /* Bind socket to server address */
  if(bind(sock,(struct sockaddr*) &server_address, sizeof(server_address)) < 0){
    fprintf(stderr, "Cannot bind socket to address");
    exit(EXIT_FAILURE);
  }

  listen(sock,5);
  return sock;
}

/**
 * Accept connection from client
 * @param socket Server listens this
 * @return int File descriptor to accepted connection
 */
int accept_connection(int socket)
{
  int addrlen = 0;
  struct sockaddr_in client_address;
  addrlen = sizeof(client_address);
  return accept(socket,(struct sockaddr*) &client_address,&addrlen);
}

/**
 * Get ip where client connected to
 * @param sock Commander socket connection
 * @param ip Result ip array (length must be 4 or greater)
 * result IP array e.g. {127,0,0,1}
 */
void getip(int sock, int *ip)
{
  socklen_t addr_size = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  getsockname(sock, (struct sockaddr *)&addr, &addr_size);
  int host,i;

  host = (addr.sin_addr.s_addr);
  for(i=0; i<4; i++){
    ip[i] = (host>>i*8)&0xff;
  }
}

/**
 * Lookup enum value of string
 * @param cmd Command string 
 * @return Enum index if command found otherwise -1
 */

int lookup_cmd(char *cmd){
  const int cmdlist_count = sizeof(cmdlist_str)/sizeof(char *);
  return lookup(cmd, cmdlist_str, cmdlist_count);
}

/**
 * General lookup for string arrays
 * It is suitable for smaller arrays, for bigger ones trie is better
 * data structure for instance.
 * @param needle String to lookup
 * @param haystack Strign array
 * @param count Size of haystack
 */
int lookup(char *needle, const char **haystack, int count)
{
  int i;
  for(i=0;i<count; i++){
    if(strcmp(needle,haystack[i])==0)return i;
  }
  return -1;
}

int get_user(char *username, Users us, User *user) {
  int i;
  for (i=0;i<us.count;i++) {
    if (strcmp(username, us.users[i].username)==0) {
      *user = us.users[i];
      return i;
    }
  }
  return -1;
}

/** 
 * Writes current state to client
 */
void write_state(State *state)
{
  write(state->connection, state->message, strlen(state->message));
}

/**
 * Generate random port for passive mode
 * @param state Client state
 */
int gen_port()
{
  srand(time(NULL));
  return 0xC000 + (rand() % 0x4000); // Range: 49152 to 65535 as IANA port number assignments
}

/**
 * Parses FTP command string into struct
 * @param cmdstring Command string (from ftp client)
 * @param cmd Command struct
 */
void parse_command(char *cmdstring, Command *cmd)
{
  sscanf(cmdstring,"%s %s",cmd->command,cmd->arg);
}

/**
 * Handles zombies
 * @param signum Signal number
 */
void my_wait(int signum)
{
  int status;
  wait(&status);
}

Users get_users(const char* filename)
{
  FILE *handler;
  Users u;
  User *us;
  int c = 0;
  char buffer[80], *f,*f2,cwd[BSIZE];

  handler = fopen(filename,"r");
  if (!handler) {
    printf("Can't open %s\n",filename);
    exit(1);
  }
  
  while ( fgets (buffer , 79 , handler) != NULL ) c++;
  us = (User*)malloc(sizeof(User)*c);
  fseek(handler, 0,SEEK_SET);
  
  c = 0;
  while (fgets (buffer, 79, handler) != NULL) {
    f = strchr(buffer,'|');
    if (f) *f = '\0';
    else continue;
    f2 = strchr(++f,'|');
    if (f2) *f2 = '\0';
    else continue;
    ++f2;
    if (f2[strlen(f2)-1]=='\n') f2[strlen(f2)-1]='\0';
    if (f2[strlen(f2)-1]=='\r') f2[strlen(f2)-1]='\0';
    if (f2[0]!='/') {
      getcwd(cwd,BSIZE);
      if (cwd[strlen(cwd)-1]!='/') {
        cwd[strlen(cwd)+1]='\0';
        cwd[strlen(cwd)]='/';
      }
      strcat(cwd,f2);
      strcpy(f2,cwd);
    }
    if (f2[strlen(f2)-1]=='/') f2[strlen(f2)-1]='\0';
    strcpy(us[c].username,buffer);
    strcpy(us[c].password,f);
    strcpy(us[c].root,f2);
    c++;
  }
  fclose(handler);
  
  u.users = us;
  u.count = c;
  return u;
}

char* getLocalPath(char *path, const char *root) {
  int i=0,j;
  const char *c;
  if (path[0] != '/') return path; // relative path
  //memcpy(path,path+strlen(root),strlen(path)+1);
  //memcpy(path,root,strlen(root));
  j = strlen(root);
  for (i=strlen(path);i>=0;i--) {
    path[i+j] = path[i];
  }
  i = 0;
  c = root;
  while (*c) {
    path[i++] = *c;
    c++;
  }
  return path;
}

char* getFtpPath(char *path, const char *root) {
  strcpy(path, path + strlen(root));
  if (path[0] == '\0')  {
    path[0] = '/';
    path[1] = '\0';
  }
  return path;
}

int main( int arn, char *arv[])
{
  int port = 8021;
  int i,u=0;
  if (arn > 2) {
    for (i=1;i<arn;++i) {
      if (strcmp(arv[i],"-p")==0) {
        if (arn > ++i) port = atoi(arv[i]);
        else break;
      } else if (strcmp(arv[i],"-u")==0) {
        if (arn > ++i) {
          users = get_users(arv[i]);
          u = 1;
        } else break;
      }
    }
  }
  if (!u) users = get_users("users.txt");
  
  server(port);
  return 0;
}
