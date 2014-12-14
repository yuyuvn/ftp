#include "common.h"
/**
 * Generates response message for client
 * @param cmd Current command
 * @param state Current connection state
 */
void response(Command *cmd, State *state)
{
  switch(lookup_cmd(cmd->command)){
    case USER: ftp_user(cmd,state); break;
    case PASS: ftp_pass(cmd,state); break;
    case PASV: ftp_pasv(cmd,state); break;
    case LIST: ftp_list(cmd,state); break;
    case CWD:  ftp_cwd(cmd,state); break;
    case PWD:  ftp_pwd(cmd,state); break;
    case MKD:  ftp_mkd(cmd,state); break;
    case RMD:  ftp_rmd(cmd,state); break;
    case RETR: ftp_retr(cmd,state); break;
    case STOR: ftp_stor(cmd,state); break;
    case DELE: ftp_dele(cmd,state); break;
    case SIZE: ftp_size(cmd,state); break;
    case ABOR: ftp_abor(state); break;
    case QUIT: ftp_quit(state); break;
    case TYPE: ftp_type(cmd,state); break;
    case CDUP: ftp_cdup(state); break;
    case HELP: ftp_help(cmd, state); break;
    case NLST: ftp_nlst(cmd, state); break;
    case RNFR: ftp_rnfr(cmd, state); break;
    case RNTO: ftp_rnto(cmd, state); break;
    case APPE: ftp_appe(cmd, state); break;
    case NOOP:
      if(state->logged_in){
        state->message = "200 Zzz...\n";
      }else{
        state->message = "530 Please login with USER and PASS\n";
      }
      write_state(state);
      break;
    default: 
      state->message = "500 Unknown command\n";
      write_state(state);
      break;
  }
}

/**
 * Handle USER command
 * @param cmd Command with args
 * @param state Current client connection state
 */
void ftp_user(Command *cmd, State *state)
{
  User user;
  if(get_user(cmd->arg,users,&user)>=0){
    state->user = user;
    state->username_ok = 1;
    state->message = "331 User name okay, need password\n";
  }else{
    state->message = "530 Invalid username\n";
  }
  write_state(state);
}

/** PASS command */
void ftp_pass(Command *cmd, State *state)
{
  if(state->username_ok==1){
    if (strcmp(state->user.password,cmd->arg)==0 || strlen(state->user.password)==0) {
      state->logged_in = 1;
      state->message = "230 Login successful\n";
      chdir(state->user.root);
    } else {
      state->message = "500 Invalid username or password\n";
    }
  }else{
    state->message = "530 Invalid username\n";
  }
  write_state(state);
}

/** PASV command */
void ftp_pasv(Command *cmd, State *state)
{
  if(state->logged_in){
    int ip[4];
    int port;
    char buff[255];
    char *response = "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\n";
    port = gen_port();
    getip(state->connection,ip);

    /* Close previous passive socket? */
    close(state->sock_pasv);

    /* Start listening here, but don't accept the connection */
    state->sock_pasv = create_socket(port);
    printf("port: %d\n",port);
    sprintf(buff,response,ip[0],ip[1],ip[2],ip[3],(port&~0xff)>>8,port&0xff);
    state->message = buff;
    state->mode = SERVER;
    puts(state->message);

  }else{
    state->message = "530 Please login with USER and PASS.\n";
    printf("%s",state->message);
  }
  write_state(state);
}

/** LIST command */
void ftp_list(Command *cmd, State *state)
{
  if(state->logged_in==1){
    struct dirent *entry;
    struct stat statbuf;
    struct tm *time;
    char timebuff[80], current_dir[BSIZE];
    int connection;
    time_t rawtime;

    /* TODO: dynamic buffering maybe? */
    char cwd[BSIZE], cwd_orig[BSIZE];
    memset(cwd,0,BSIZE);
    memset(cwd_orig,0,BSIZE);
    
    /* Later we want to go to the original path */
    getcwd(cwd_orig,BSIZE);
    
    /* Just chdir to specified path */
    if(strlen(cmd->arg)>0&&cmd->arg[0]!='-'){
      chdir(getLocalPath(cmd->arg,state->user.root));
    }
    
    getcwd(cwd,BSIZE);
    DIR *dp = opendir(cwd);

    if(!dp){
      state->message = "550 Failed to open directory.\n";
    }else{
      if(state->mode == SERVER){

        connection = accept_connection(state->sock_pasv);
        state->message = "150 Sending directory list.\n";
        puts(state->message);

        while(entry=readdir(dp)){
          if(stat(entry->d_name,&statbuf)==-1){
            fprintf(stderr, "FTP: Error reading file stats...\n");
          }else{
            char *perms = malloc(9);
            memset(perms,0,9);

            /* Convert time_t to tm struct */
            rawtime = statbuf.st_mtime;
            time = localtime(&rawtime);
            strftime(timebuff,80,"%b %d %H:%M",time);
            str_perm((statbuf.st_mode & ALLPERMS), perms);
            dprintf(connection,
                "%c%s %5d %4d %4d %8d %s %s\r\n", 
                (entry->d_type==DT_DIR)?'d':'-',
                perms,statbuf.st_nlink,
                statbuf.st_uid, 
                statbuf.st_gid,
                statbuf.st_size,
                timebuff,
                entry->d_name);
          }
        }
        write_state(state);
        state->message = "226 Directory send OK.\n";
        state->mode = NORMAL;
        close(connection);
        close(state->sock_pasv);

      }else if(state->mode == CLIENT){
        state->message = "502 Command not implemented.\n";
      }else{
        state->message = "425 Use PASV or PORT first.\n";
      }
    }
    closedir(dp);
    chdir(cwd_orig);
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  state->mode = NORMAL;
  write_state(state);
}


/** QUIT command */
void ftp_quit(State *state)
{
  state->message = "221 Goodbye.\n";
  write_state(state);
  close(state->connection);
  exit(0);
}

/** PWD command */
void ftp_pwd(Command *cmd, State *state)
{
  if(state->logged_in){
    char cwd[BSIZE];
    char result[BSIZE];
    memset(result, 0, BSIZE);
    if(getcwd(cwd,BSIZE)!=NULL){
      getFtpPath(cwd,state->user.root);
      strcat(result,"257 \"");
      strcat(result,cwd);
      strcat(result,"\"\n");
      state->message = result;
    }else{
      state->message = "550 Failed to get pwd.\n";
    }
    write_state(state);
  }
}

/** CWD command */
void ftp_cwd(Command *cmd, State *state)
{
  if(state->logged_in){
    if(chdir(getLocalPath(cmd->arg,state->user.root))==0){
      state->message = "250 Directory successfully changed.\n";
    }else{
      state->message = "550 Failed to change directory.\n";
    }
  }else{
    state->message = "500 Login with USER and PASS.\n";
  }
  write_state(state);

}

/** 
 * MKD command 
 * TODO: full path directory creation
 */
void ftp_mkd(Command *cmd, State *state)
{
  if(state->logged_in){
    char cwd[BSIZE];
    char res[BSIZE];
    memset(cwd,0,BSIZE);
    memset(res,0,BSIZE);
    getcwd(cwd,BSIZE);

    getLocalPath(cmd->arg,state->user.root);
    /* TODO: check if directory already exists with chdir? */

    /* Absolute path */
    if(cmd->arg[0]=='/'){
      if(mkdir(cmd->arg,S_IRWXU)==0){
        strcat(res,"257 \"");
        strcat(res,cmd->arg);
        strcat(res,"\" new directory created.\n");
        state->message = res;
      }else{
        state->message = "550 Failed to create directory. Check path or permissions.\n";
      }
    }
    /* Relative path */
    else{
      if(mkdir(cmd->arg,S_IRWXU)==0){
        sprintf(res,"257 \"%s/%s\" new directory created.\n",cwd,cmd->arg);
        state->message = res;
      }else{
        state->message = "550 Failed to create directory.\n";
      }
    }
  }else{
    state->message = "500 Login with USER and PASS.\n";
  }
  write_state(state);
}

/** RETR command */
void ftp_retr(Command *cmd, State *state)
{
  int pid;
  if((pid = fork())==0){
    int connection;
    int fd;
    struct stat stat_buf;
    off_t offset = 0;
    int sent_total = 0;
    if(state->logged_in){

      /* Passive mode */
      if(state->mode == SERVER){
        getLocalPath(cmd->arg,state->user.root);
        if(access(cmd->arg,R_OK)==0 && (fd = open(cmd->arg,O_RDONLY))){
          fstat(fd,&stat_buf);
          
          state->message = "150 Opening BINARY mode data connection.\n";
          
          write_state(state);
          
          connection = accept_connection(state->sock_pasv);
          close(state->sock_pasv);
          if(sent_total = sendfile(connection, fd, &offset, stat_buf.st_size)){
            
            if(sent_total != stat_buf.st_size){
              perror("ftp_retr:sendfile");
              exit(EXIT_SUCCESS);
            }

            state->message = "226 File send OK.\n";
          }else{
            state->message = "550 Failed to read file.\n";
          }
        }else{
          state->message = "550 Failed to get file\n";
        }
      }else{
        state->message = "550 Please use PASV instead of PORT.\n";
      }
    }else{
      state->message = "530 Please login with USER and PASS.\n";
    }

    close(fd);
    close(connection);
    write_state(state);
    exit(EXIT_SUCCESS);
  } else if (pid > 0) {
    state->tr_pid = pid;
  }
  state->mode = NORMAL;
  close(state->sock_pasv);
}

void ftp_stor(Command *cmd, State *state)
{
  int pid;
  if((pid = fork())==0){
    int connection, fd;
    off_t offset = 0;
    int pipefd[2];
    int res = 1;
    const int buff_size = 8192;

    getLocalPath(cmd->arg,state->user.root);
    FILE *fp = fopen(cmd->arg,"w");

    if(fp==NULL){
      state->message = "451 Can't write file.\n";
      perror("ftp_stor:fopen");
    }else if(state->logged_in){
      if(!(state->mode==SERVER)){
        state->message = "550 Please use PASV instead of PORT.\n";
      }
      /* Passive mode */
      else{
        fd = fileno(fp);
        connection = accept_connection(state->sock_pasv);
        close(state->sock_pasv);
        if(pipe(pipefd)==-1)perror("ftp_stor: pipe");

        state->message = "125 Data connection already open; transfer starting.\n";
        write_state(state);

        /* Using splice function for file receiving.
         * The splice() system call first appeared in Linux 2.6.17.
         */

        while ((res = splice(connection, 0, pipefd[1], NULL, buff_size, SPLICE_F_MORE | SPLICE_F_MOVE))>0){
          splice(pipefd[0], NULL, fd, 0, buff_size, SPLICE_F_MORE | SPLICE_F_MOVE);
        }


        /* Internal error */
        if(res==-1){
          perror("ftp_stor: splice");
          exit(EXIT_SUCCESS);
        }else{
          state->message = "226 File send OK.\n";
        }
        close(connection);
        close(fd);
      }
    }else{
      state->message = "530 Please login with USER and PASS.\n";
    }
    close(connection);
    write_state(state);
    exit(EXIT_SUCCESS);
  } else if (pid > 0) {
    state->tr_pid = pid;
  }
  state->mode = NORMAL;
  close(state->sock_pasv);

}

/** ABOR command */
void ftp_abor(State *state)
{
  if(state->logged_in){
    state->message = "226 Closing data connection.\n";
    if (state->sock_pasv) {
      if (state->tr_pid > 0) kill(state->tr_pid, SIGTERM);
      close(state->sock_pasv);
    } else {
      state->message = "225 Data connection open; no transfer in progress.\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  write_state(state);

}

/** 
 * Handle TYPE command.
 * BINARY only at the moment.
 */
void ftp_type(Command *cmd,State *state)
{
  if(state->logged_in){
    if(cmd->arg[0]=='I'){
      state->message = "200 Switching to Binary mode.\n";
    }else if(cmd->arg[0]=='A'){

      /* Type A must be always accepted according to RFC */
      state->message = "200 Switching to ASCII mode.\n";
    }else{
      state->message = "504 Command not implemented for that parameter.\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  write_state(state);
}

/** Handle DELE command */
void ftp_dele(Command *cmd,State *state)
{
  if(state->logged_in){
    getLocalPath(cmd->arg,state->user.root);
    if(unlink(cmd->arg)==-1){
      state->message = "550 File unavailable.\n";
    }else{
      state->message = "250 Requested file action okay, completed.\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  write_state(state);
}

/** Handle RMD */
void ftp_rmd(Command *cmd, State *state)
{
  if(!state->logged_in){
    state->message = "530 Please login first.\n";
  }else{
    getLocalPath(cmd->arg,state->user.root);
    if(rmdir(cmd->arg)==0){
      state->message = "250 Requested file action okay, completed.\n";
    }else{
      state->message = "550 Cannot delete directory.\n";
    }
  }
  write_state(state);

}

/** Handle SIZE (RFC 3659) */
void ftp_size(Command *cmd, State *state)
{
  if(state->logged_in){
    struct stat statbuf;
    char filesize[128];
    memset(filesize,0,128);
    /* Success */
    getLocalPath(cmd->arg,state->user.root);
    if(stat(cmd->arg,&statbuf)==0){
      sprintf(filesize, "213 %d\n", statbuf.st_size);
      state->message = filesize;
    }else{
      state->message = "550 Could not get file size.\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }

  write_state(state);

}

/** 
 * Converts permissions to string. e.g. rwxrwxrwx 
 * @param perm Permissions mask
 * @param str_perm Pointer to string representation of permissions
 */
void str_perm(int perm, char *str_perm)
{
  int curperm = 0;
  int flag = 0;
  int read, write, exec;
  
  /* Flags buffer */
  char fbuff[3];

  read = write = exec = 0;
  
  int i;
  for(i = 6; i>=0; i-=3){
    /* Explode permissions of user, group, others; starting with users */
    curperm = ((perm & ALLPERMS) >> i ) & 0x7;
    
    memset(fbuff,0,3);
    /* Check rwx flags for each*/
    read = (curperm >> 2) & 0x1;
    write = (curperm >> 1) & 0x1;
    exec = (curperm >> 0) & 0x1;

    sprintf(fbuff,"%c%c%c",read?'r':'-' ,write?'w':'-', exec?'x':'-');
    strcat(str_perm,fbuff);

  }
}

void ftp_cdup(State *state)
{
  if(state->logged_in){
    // TODO check if in root
    chdir("..");
    state->message = "250 Change dir susscefull.\n";
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  write_state(state);
}

void ftp_help(Command *cmd, State *state) 
{
  switch(lookup_cmd(cmd->arg)){
    case USER: 
      state->message = "214 Syntax: USER <sp> username\n";
      break;
    case PASS:
      state->message = "214 Syntax: PASS <sp> password\n";
      break;
    case PASV:
      state->message = "214 Syntax: PASV\n";
      break;
    case LIST:
      state->message = "214 Syntax: LIST\n";
      break;
    case CWD:
      state->message = "214 Syntax: CWD pathname\n";
      break;
    case PWD:
      state->message = "214 Syntax: PWD\n";
      break;
    case MKD:
      state->message = "214 Syntax: MKD dirname\n";
      break;
    case RMD:
      state->message = "214 Syntax: RMD dirname\n";
      break;
    case RETR:
      state->message = "214 Syntax: RETRe\n";
      break;
    case STOR:
      state->message = "214 Syntax: STOR\n";
      break;
    case DELE:
      state->message = "214 Syntax: DELE filename\n";
      break;
    case SIZE:
      state->message = "214 Syntax: SIZE filename\n";
      break;
    case ABOR:
      state->message = "214 Syntax: ABOR\n";
      break;
    case QUIT:
      state->message = "214 Syntax: QUIT\n";
      break;
    case TYPE:
      state->message = "214 Syntax: TYPE type\n";
      break;
    case CDUP:
      state->message = "214 Syntax: CDUP\n";
      break;
    case HELP:
      state->message = "214 Syntax: HELP\n";
      break;
    case NLST:
      state->message = "214 Syntax: NLST\n";
      break;
    case RNFR:
      state->message = "214 Syntax: RNFR filename\n";
      break;
    case RNTO:
      state->message = "214 Syntax: RNTO filename\n";
      break;
    case APPE:
      state->message = "214 Syntax: APPE\n";
      break;
    case NOOP:
      state->message = "214 Syntax: NOOP\n";
      break;
  }
  state->message = "214 This is hedspi ftp server! Check manual for more information\n";
  write_state(state);
}

void ftp_nlst(Command *cmd, State *state) 
{
  if(state->logged_in==1){
    struct dirent *entry;
    struct stat statbuf;
    struct tm *time;
    char timebuff[80], current_dir[BSIZE];
    int connection;
    time_t rawtime;

    /* TODO: dynamic buffering maybe? */
    char cwd[BSIZE], cwd_orig[BSIZE];
    memset(cwd,0,BSIZE);
    memset(cwd_orig,0,BSIZE);
    
    /* Later we want to go to the original path */
    getcwd(cwd_orig,BSIZE);
    
    /* Just chdir to specified path */
    getLocalPath(cmd->arg,state->user.root);
    if(strlen(cmd->arg)>0&&cmd->arg[0]!='-'){
      chdir(cmd->arg);
    }
    
    getcwd(cwd,BSIZE);
    DIR *dp = opendir(cwd);

    if(!dp){
      state->message = "550 Failed to open directory.\n";
    }else{
      if(state->mode == SERVER){

        connection = accept_connection(state->sock_pasv);
        state->message = "150 Here comes the directory listing.\n";
        puts(state->message);

        while(entry=readdir(dp)){
          if(stat(entry->d_name,&statbuf)==-1){
            fprintf(stderr, "FTP: Error reading file stats...\n");
          }else{
            char *perms = malloc(9);
            memset(perms,0,9);

            /* Convert time_t to tm struct */
            rawtime = statbuf.st_mtime;
            time = localtime(&rawtime);
            strftime(timebuff,80,"%b %d %H:%M",time);
            str_perm((statbuf.st_mode & ALLPERMS), perms);
            dprintf(connection,
                "%s\015\012",
                entry->d_name);
          }
        }
        write_state(state);
        state->message = "226 Directory send OK.\n";
        state->mode = NORMAL;
        close(connection);
        close(state->sock_pasv);

      }else if(state->mode == CLIENT){
        state->message = "502 Command not implemented.\n";
      }else{
        state->message = "425 Use PASV or PORT first.\n";
      }
    }
    closedir(dp);
    chdir(cwd_orig);
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  state->mode = NORMAL;
  write_state(state);
}

void ftp_rnfr(Command *cmd, State *state)
{
  struct stat st;
  if(state->logged_in){
    getLocalPath(cmd->arg,state->user.root);
    int result = stat(cmd->arg, &st);
    if (result != 0) {
      state->message = "450 File not found!\n";
    } else {
      state->rename = malloc(32);
      strcpy(state->rename, cmd->arg);
      state->message = "350 File ok, need file rename to\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  state->mode = NORMAL;
  write_state(state);
}

void ftp_rnto(Command *cmd, State *state)
{
  int result;
  if(state->logged_in){
    getLocalPath(cmd->arg,state->user.root);
    result = rename(state->rename, cmd->arg);
    if (result == 0) {
      state->message = "250 Rename susscessfully\n";
    } else {
      state->message = "553 Rename errored\n";
    }
  }else{
    state->message = "530 Please login with USER and PASS.\n";
  }
  write_state(state);
}

void ftp_appe(Command *cmd, State *state)
{
  int pid;
  if((pid = fork())==0){
    int connection, fd;
    off_t offset = 0;
    int pipefd[2];
    int res = 1;
    const int buff_size = 8192;

    getLocalPath(cmd->arg,state->user.root);
    FILE *fp = fopen(cmd->arg,"a");

    if(fp==NULL){
      state->message = "451 Can't write file.\n";
      perror("ftp_stor:fopen");
    }else if(state->logged_in){
      if(!(state->mode==SERVER)){
        state->message = "550 Please use PASV instead of PORT.\n";
      }
      /* Passive mode */
      else{
        fd = fileno(fp);
        connection = accept_connection(state->sock_pasv);
        close(state->sock_pasv);
        if(pipe(pipefd)==-1)perror("ftp_stor: pipe");

        state->message = "125 Data connection already open; transfer starting.\n";
        write_state(state);

        /* Using splice function for file receiving.
         * The splice() system call first appeared in Linux 2.6.17.
         */

        while ((res = splice(connection, 0, pipefd[1], NULL, buff_size, SPLICE_F_MORE | SPLICE_F_MOVE))>0){
          splice(pipefd[0], NULL, fd, 0, buff_size, SPLICE_F_MORE | SPLICE_F_MOVE);
        }


        /* Internal error */
        if(res==-1){
          perror("ftp_stor: splice");
          exit(EXIT_SUCCESS);
        }else{
          state->message = "226 File send OK.\n";
        }
        close(connection);
        close(fd);
      }
    }else{
      state->message = "530 Please login with USER and PASS.\n";
    }
    close(connection);
    write_state(state);
    exit(EXIT_SUCCESS);
  } else if (pid > 0) {
    state->tr_pid = pid;
  }
  state->mode = NORMAL;
  close(state->sock_pasv);

}
