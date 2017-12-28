#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>

/* define */
#define MAX_LINE_LEN 2048 // USERLEN TITLELEN の分だけ小さくできる
#define MAX_TITLE_LEN 128
#define MAX_PATH 256
#define MAX_USER_LEN 128
#define MAX_ASKTIME_LEN 32
#define MAX_ENTRY 1000

#define PERMISSION 0755 //755: -rwxr-xr-x
#define PORT 4000

// #define SUCCESS 1
// #define FAIL 0

#define THREAD_DIR "bbs_thread" //スレッド（板）を格納するディレクトリ
#define TITLE_LIST "files.bbs" //上記の「スレッド」一覧を保持するファイル

#define LOG_DIR "bbs_log"
#define LOG_FILE ".svr.log"

enum{
  FAILED,
  SUCCESS,
  END,
  S_ERROR,
};

int my_send(int fd, char *s){
  int len = strlen(s);
  if(send(fd, s, len+1, 0) != len+1)  return 0;
  else return 1;
}

int send_NG(int fd){return my_send(fd ,"_NG");}
int send_OK(int fd){return my_send(fd ,"_OK");}
int send_END(int fd){return my_send(fd ,"_END");}


void create_dir(char *dirname)
{
  FILE *fp;
  //ログを格納するディレクトリ "LOGS" は存在しているか
  if((fp = fopen(dirname, "r")) == NULL){
    printf("There is no directry to store.\n"
           "Create new directry.\n");
    //755: drwxr-xr-x
    if(mkdir(dirname, PERMISSION) == -1){
      printf("Error: cannot create a new directly.\n");
    }
  }
  else fclose(fp);
}

/* 必要な情報をソケットに紐づける */
void format_sa_in(struct sockaddr_in *sa)
{
  memset((char*)sa, 0, sizeof(*sa));
  (*sa).sin_family = AF_INET;
  (*sa).sin_addr.s_addr = htonl(INADDR_ANY);
  (*sa).sin_port = htons(PORT);
}

int auto_strncmp(char *s, char *t){
  return strncmp(s, t, strlen(t));
}

int check_status(int fd)
{
  char tmp[MAX_LINE_LEN];
  int  recv_len = 0;

  memset(tmp, 0, sizeof(tmp));

  recv_len = recv(fd, tmp, sizeof(tmp), 0);
  if(recv_len > 0){
    if(auto_strncmp(tmp, "_OK") == 0); //なにもしない
    else if(auto_strncmp(tmp, "_NG") == 0) return FAILED;
    else if(auto_strncmp(tmp, "_END") == 0) return END;
    else{
      printf("Received undefined message.\n");
      return FAILED;
    }
  }
  else{
    printf("Cannot receive the message.\n");
    send_NG(fd);
    return S_ERROR;
  }
  // //Check
  // if(recv(fd, recv_buf, sizeof(recv_buf), 0) < 0)
  //   if(strncmp(recv_buf, "_NG", strlen("_NG")+1) == 0){
  //     printf("Error:Client returns NG.\n");
  //     return FAIL;
  //   }
  return SUCCESS;
}

/* 文字列 s を sep で最大 n 個に分割して，分割した文字列の先頭アドレスを ret に格納 */
int split(char *s, char *sstr[], char sep, int n)
{
  int i = 1;
  sstr[0] = s;
  while(*s != '\0' && i < n){
    if(*s == sep){
      *s = '\0';
      sstr[i++] = s + 1;
    }
    s++;
  }
  if(i == n){
    return i;
  }
  else{
    return 0;
  }
}

/* 文字列 s 中の文字 from を to で置き換える */
void subst(char *s, char from, char to)
{
  while(*s != '\0'){
    if(*s == from)
      *s = to;
    s++;
  }
}



/* ディレクトリ diname を展開して，filename で指定したファイル名で保存 */
int lsdir_to_file(char *dirname, char *filename)
{
  FILE *wfp;
  struct dirent *dp; //data
  DIR  *dfd;
  char filepath[MAX_PATH];
  struct stat stbuf;

  if((wfp = fopen(filename, "w")) == NULL){
    printf("lsdir_to_file: cannot open file (%s)\n", filename);
    printf("creat the file (%s).\n", filename);
    return FAILED;
  }

  if((dfd = opendir((const char*)THREAD_DIR)) == NULL){
    printf("lsdir_to_file: cannot open directry(%s).\n", dirname);
    return END;
  }

  while((dp = readdir(dfd)) != NULL){
    if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
      continue;
    if(strlen(dirname) + strlen(dp->d_name) + 2 > sizeof(filepath)){
      fprintf(stderr, "Error: lsdir_to_file, Too long name.\n>%s/%s",
	            dirname, dp->d_name);
      return FAILED;
    }
    sprintf(filepath, "%s/%s", dirname, dp->d_name);
    if(stat(filepath, &stbuf) == -1){
      fprintf(stderr, "Error: lsdir_to_file, stat returns error.\n");
      return FAILED;
    }
    //filenameを保存
    fprintf(wfp, "%s|%s", dp->d_name, asctime(localtime(&stbuf.st_mtime)));
    memset(filepath, 0, sizeof(filepath));
  }
  closedir(dfd);
  fclose(wfp);
  return SUCCESS;
}

/* 取得したリスト内を走査 */
int search_title(char *title)
{
  int status = FAILED;
  FILE *rfp;
  char tmp[MAX_TITLE_LEN + MAX_ASKTIME_LEN + 1];
  char *tmpstr[2];

  //files.bbs を走査
  status = lsdir_to_file(THREAD_DIR, TITLE_LIST);
  if(status != SUCCESS) return FAILED;

  if((rfp = fopen(TITLE_LIST, "r")) == NULL){
    printf("Fail to open file \"%s\"\n", TITLE_LIST);
    return FAILED;
  }

  while(fgets(tmp, sizeof(tmp), rfp)){
    if(split(tmp, tmpstr, '|', 2) != 2){
      fclose(rfp); return FAILED;
    }
    if(strcmp(tmp, title) == 0){
      fclose(rfp); return SUCCESS;
    }
  }
  fclose(rfp);
  return END;
}

/* THREAD_DIR中のスレッドをひとつオープン */
int exec_open(int fd){
  char recv_buf[MAX_TITLE_LEN];
  int exist = FAILED;

  memset(recv_buf, 0, sizeof(recv_buf));
  if(recv(fd, recv_buf, sizeof(recv_buf), 0) > 0){
    subst(recv_buf, '\n', '\0');
    exist = search_title(recv_buf);
    if(exist == SUCCESS){
      if(send_OK(fd) == 0) return S_ERROR;
    }
    else if(exist == END){
      if(send_END(fd) == 0) return S_ERROR;
    }
    else if(exist == FAILED){
      if(send_NG(fd) == 0) return S_ERROR;
    }
  }
  else{
    return S_ERROR;
  }
  return SUCCESS;
}

//  番号.(sep)日付(sep)名前(sep)本文
void comb_str_sep(char *dst, char *src[], char *separator, int num)
{
  time_t now;

  memset(dst, 0, sizeof(dst));

  time(&now);
  sprintf(dst, "%d.%s%s", num, separator, asctime(localtime(&now)));
  subst(dst, '\n', '\0');
  strcat(dst, separator); //番号，日付
  strcat(dst, src[1]); // 名前
  strcat(dst, separator);

  if(strlen(src[2]) > MAX_LINE_LEN - strlen(dst)){
    strncat(dst, src[2], strlen(src[2]) - (MAX_LINE_LEN - strlen(dst) - 1));
  }
  else strcat(dst, src[2]);
}

/* write コマンド
 * クライアントから送られた文字列を "番号 | 日付 | ユーザ名 | 本文 " の形にする
 */
int exec_write(int fd, struct sockaddr_in *sa)
{
  char *sstr[3];
  char recv_buf[MAX_LINE_LEN];
  char file_place[MAX_TITLE_LEN + strlen("bbs_thread/")];//行
  char tmp[MAX_LINE_LEN];
  char write_string[MAX_LINE_LEN];
  FILE *wfp, *rfp, *lfp;
  int i = 1;
  char *log_path;

  // memset(recv_buf, 0, sizeof(recv_buf));
  memset(file_place, 0, sizeof(file_place));

  if(recv(fd, recv_buf, sizeof(recv_buf), 0) > 0){
    if(auto_strncmp(recv_buf, "_NG") == 0) return FAILED;
  }
  else{
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }
  //Get text
  if(recv(fd, recv_buf, sizeof(recv_buf), 0) > 0){
    if(auto_strncmp(recv_buf, "_NG") == 0) return FAILED;
  }
  else{
    printf("Sorry, cannot receive the client's message.\n");
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  if(split(recv_buf, sstr, '|', 3) == 0){
    // send(fd, "_NG", 0, 0);
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  //recv_buf を title|username|text に
  sprintf(file_place, THREAD_DIR"/%s", sstr[0]);
  puts(file_place);

  //行数
  if((rfp = fopen(file_place, "r")) != NULL){
    while(fgets(tmp, sizeof(tmp), rfp))
      i++;
    fclose(rfp);
  }

  // ファイルをオープン
  if((wfp = fopen(file_place, "a")) == NULL){
    // send(fd, "_NG", 0, 0);
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  log_path = malloc(strlen(LOG_DIR) + strlen(sstr[1]) + strlen(LOG_FILE) + 1);
  sprintf(log_path, LOG_DIR"/%s"LOG_FILE, sstr[1]);
  if((lfp = fopen(log_path, "a")) == NULL){
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }
  free(log_path);


  if(i >= MAX_ENTRY){
    my_send(fd, "The thread is full!!\nPlease open a new thread.\n");
    return SUCCESS;
  }

  comb_str_sep(write_string, sstr, "|", i);

  fprintf(wfp, "%s\n", write_string);
  // printf("written:%s\n", write_string);
  // send(fd, "_END", strlen("_END")+1, 0);
  fprintf(lfp, "%s(%s) written %s:%s\n",
          sstr[1],inet_ntoa(sa->sin_addr), sstr[0], sstr[2]);
  send_END(fd);

  printf("written: %s in %s\n", write_string, file_place);
  fclose(wfp);
  fclose(lfp);

  return SUCCESS;
}


/* TITLE_LISTに1行ずつ書き込む関数 */
int exec_list(int fd)
{
  char s[MAX_TITLE_LEN * 100];
  FILE *fp;
  char filename[MAX_LINE_LEN];
  // char recv_buf[MAX_LINE_LEN];
  int status = SUCCESS;

  memset(s, 0, sizeof(s));
  // memset(recv_buf, 0, sizeof(recv_buf));
  memset(filename, 0, sizeof(filename));

  status = check_status(fd);
  if(status == FAILED || status == S_ERROR)  return FAILED;

  //files.bbsに書き込む処理
  if((status = lsdir_to_file(THREAD_DIR, TITLE_LIST)) == END){
    //THREAD_DIRが存在していなければ
    if(send_END(fd) == 0) return S_ERROR;
    else return SUCCESS;
  }
  else if(status == FAILED){
    fprintf(stderr, "lsdir_to_file: Cannot save list in files.bbs.\n");
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  if((fp = fopen(TITLE_LIST, "r")) == NULL){
    printf("Cannot open the list file(files.bbs).\n");
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  //strcat(s, filename);
  while(fgets(filename, sizeof(filename), fp)){
    if(my_send(fd, filename) == 0) return S_ERROR;
    status = check_status(fd);
    if(status == FAILED) return FAILED;
    else if(status == S_ERROR){
      printf("Cannot recieve the message from client.\n");
      return FAILED;
    }
    memset(filename, 0, sizeof(filename));
  }
  printf("Send title list\n");
  if(send_END(fd) == 0) return S_ERROR;
  else return SUCCESS;
}

// i から j まで送信（∴ j が 0なら全部）
int send_data(int fd, FILE *fp, int i, int j)
{
  char send_buf[MAX_LINE_LEN];
  // char recv_buf[MAX_LINE_LEN];
  char send_all = 0;
  int status = SUCCESS;

  memset(send_buf, 0, sizeof(send_buf));
  // memset(recv_buf, 0, sizeof(recv_buf));

  if(j == 0) send_all = 1;
  if(i <= 0) i = 0;        //i <= 0 : send all

  //行の読み飛ばし
  while(i--){
    if(fgets(send_buf, sizeof(send_buf), fp))
    // printf("%d\n", i);
      printf("send_data: skip %s", send_buf);
    else{
      if(send_END(fd) == 0) return S_ERROR;
      printf("Too large number.\n");
      return FAILED;
    }
  }

  while(1){
    memset(send_buf, 0, sizeof(send_buf));

    if(fgets(send_buf, sizeof(send_buf), fp) == NULL)  break;
    if(send_all == 0 && j-- <= 0 )                     break;

    if(my_send(fd, send_buf) == 0){
      printf("send_data: Fail to send message.\n");
      return S_ERROR;
    }
    status = check_status(fd);
    if(status == SUCCESS)
      printf("Send message: %s", send_buf);
    else if(status == FAILED){
      printf("Fail to get client's mesage.\n");
      if(send_NG(fd) == 0) return S_ERROR;
      else return FAILED;
    }
    else if(status == S_ERROR) return FAILED;
  }

  printf("Finish to send messages.\n");
  return SUCCESS;
}

/* クライアントから受け取ったファイル名を開き，その内容を1行ずつ送信 */
int exec_print(int fd)
{
  char recv_buf[MAX_LINE_LEN];
  char send_buf[MAX_LINE_LEN];
  char file_name[MAX_TITLE_LEN];
  char *sstr[2];
  FILE *fp;
  int i, j;
  int status = SUCCESS;
  i = 0;
  j = 0;

  memset(recv_buf, 0, sizeof(recv_buf));
  memset(send_buf, 0, sizeof(send_buf));
  memset(file_name, 0, sizeof(file_name));

  //掲示板スレッドのタイトル
  if(recv(fd, recv_buf, sizeof(recv_buf), 0) > 0){
    if(strncmp(recv_buf, "_NG", strlen("_NG")) == 0)
      return SUCCESS; //client set no thread title
    else{
      sprintf(file_name, THREAD_DIR"/%s", recv_buf);
      //check board is exits or not
      if((fp = fopen(file_name, "r")) == NULL){
        if(send_END(fd) == 0) return S_ERROR;
        printf("Fail to open file %s\n", file_name);
        return SUCCESS;//the board not exits
      }
      else{
        printf("%s is open\n", file_name);
        if(send_OK(fd) == 0)  return S_ERROR;
      }
    }
  }
  else{
    send(fd, "No file exits.", strlen("No file exits") + 1, 0);
    printf("Cannot receive the message from client.\n");
    return FAILED;
  }

  //コマンドライン引数を取得
  memset(recv_buf, 0, sizeof(recv_buf));
  if(recv(fd, recv_buf, sizeof(recv_buf), 0) > 0){
    if(send_OK(fd) == 0) return S_ERROR;
  }
  else{
    if(send_NG(fd) == 0) return S_ERROR;
    printf("Cannot receive the message from client.\n");
    return FAILED;
  }

  //データ送信
  if(strncmp(recv_buf, "all", strlen("all")) == 0){
    status = send_data(fd, fp, 0, 0);
    printf("Send all datas of %s\n", file_name);
  }
  else if((i = split(recv_buf, sstr, '-', 2)) == 0){//case i
    i = atoi(recv_buf);
    printf("Print %dth data.\n", i);
    if(i < 1){
      i = -i;
    }
    status = send_data(fd, fp, i - 1, 1);
    printf("Send %dth data of %s\n", i, file_name);
  }
  else{//case i-j
    i = atoi(sstr[0]);
    j = atoi(sstr[1]);
    status = send_data(fd, fp, i - 1, j - i + 1);
    printf("Send datas of %s from %d to %d\n", file_name, i, j);
  }

  if(status == SUCCESS || status == END){
    if(send_END(fd) == 0)  return S_ERROR;
  }
  else{
    if(send_NG(fd) == 0)   return S_ERROR;
  }
  return status;
}


/* 各コマンドを実行する関数へと処理を渡す関数 */
// int bbs_manager(int fd, char *cmd)
int bbs_manager(int fd, char *s, struct sockaddr_in *sa)
{
  // int status = SUCCESS;
  int (*command)(int);

  // send(fd, "_OK", strlen("_OK")+1, 0);
  //
  // if(strcmp(cmd, "write") == 0){
  //   exec_write(fd);
  // }
  // else if(strcmp(cmd, "list") == 0){
  //   status = exec_list(fd);
  // }
  // else if(strcmp(cmd, "print") == 0){
  //   status = exec_print(fd);
  // }
  // else{
  //   printf("The command: \"%s\" does not exist.\n", cmd);
  //   status = FAIL;
  // }
  //
  // return status;

  if(auto_strncmp(s, "open") == 0){
    command = exec_open;
  }
  else if(auto_strncmp(s, "list") == 0){
    command = exec_list;
  }
  else if(auto_strncmp(s, "print") == 0){
    command = exec_print;
  }
  else if(auto_strncmp(s, "write") == 0){
    if(send_OK(fd) == 0) return S_ERROR;
    return exec_write(fd, sa);
  }
  else{
    printf("The command: \"%s\" does not exist.\n", s);
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }

  if(send_OK(fd) == 0)  return S_ERROR;
  return command(fd);
}

/* ゾンビをkill */
void reaper(){
  pid_t pid;
  int   status;
  while(waitpid(pid, &status, WNOHANG) > 0)
    fprintf(stdout, "KILL ZOMBIE\n");
}

void kill_z(int signum, siginfo_t *info, void *ctx){
  pid_t pid;
  int   status;

  //kill zombie process
  while(waitpid(pid, &status, WNOHANG) > 0){
    write(1, "KILL ZOMBIE\n", strlen("KILL ZOMBIE\n")+1);
    fflush(stdout);
  }
}

/******************* main ********************/
int main(int argc, char *argv[])
{
  struct sockaddr_in ser;
  struct sockaddr_in cli;
  int  sock_fd;
  int  sock_accptd_fd;
  char recv_buf[MAX_LINE_LEN];
  // FILE *fp;
  // int  new_dir_fd;

  int cl_len;
  struct sigaction sa_killz;

  memset(&sa_killz, 0, sizeof(sa_killz));
  sa_killz.sa_sigaction = kill_z;
  sa_killz.sa_flags = SA_RESTART | SA_SIGINFO;

  // //"thread_dir"は存在しているか
  // if((fp = fopen(THREAD_DIR, "r")) == NULL){
  //   printf("There is no directry to store datas.\n"
	//          "Create new directry.\n");
  //   //PERMISSION: drwxr-xr-x
  //   if((new_dir_fd = mkdir(THREAD_DIR, PERMISSION)) == -1){
  //     printf("Error: cannot create a new directly.\n");
  //   }
  // }
  // else{
  //   fclose(fp);
  // }

  // スレッド，ログ格納するディレクトリがなければ作成
  create_dir(THREAD_DIR);
  create_dir(LOG_DIR);

  /* ソケットのマクロ */
  // memset((char*)&sa, 0, sizeof(sa));
  // sa.sin_family = AF_INET;
  // sa.sin_addr.s_addr = htonl(INADDR_ANY);
  // sa.sin_port = htons(PORT);
  format_sa_in(&ser);
  format_sa_in(&cli);


  /* エラーチェック */
  if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    printf("Error: socket\n");
    exit(1);
  }

  if(bind(sock_fd, (struct sockaddr *)&ser, sizeof(ser)) < 0){
    printf("Error: fail to name the socket.\n");
    exit(1);
  }

  if(listen(sock_fd, SOMAXCONN) < 0){ //SOMAXCONN: 128
    printf("Error: cannot get request.\n");
    exit(1);
  }

  /* ゾンビの埋葬 */
  if(sigaction(SIGCHLD, &sa_killz, NULL) == -1){
    perror("sigint_handler\n");
    exit(1);
  }

  while(1){
    cl_len = sizeof(cli);
    if((sock_accptd_fd = accept(sock_fd, (struct sockaddr*)&cli,
		                     (socklen_t*)&cl_len)) < 0){
      printf("Accept: accept returns ERROR \n");
      goto close_socket;
    }

    if(fork() != 0){ //親プロセス
      close(sock_accptd_fd);
      continue;
    }
    close(sock_fd);//子プロセス

    memset(recv_buf, 0, sizeof(recv_buf));
    if(recv(sock_accptd_fd, recv_buf, sizeof(recv_buf), 0) > 0){
      if(bbs_manager(sock_accptd_fd, recv_buf, &cli) == SUCCESS)
        ;//printf("SUCCESS!\n");
      else printf("Error: bbs_manager\n");
    }
    close(sock_accptd_fd);
    exit(0);
  }

  // while(1){
  //   if((sock_accptd_fd = accept(sock_fd, NULL, NULL)) < 0){
  //     printf("Accept: accept returns ERROR \n");
  //     goto close_socket;
  //   }
  //
  //   memset(recv_buf, 0, sizeof(recv_buf));
  //   //fprintf(stdout, "Waiting...\n");
  //   if(recv(sock_accptd_fd, recv_buf, sizeof(recv_buf), 0) > 0){
  //     if(bbs_manager(sock_accptd_fd, recv_buf) == SUCCESS);
  //     //printf("bbs_manager\n");
  //     else
	//      printf("Error: Error occured in bbs_manager\n");
  //   }
  //   close(sock_accptd_fd);
  // }

 close_socket:
  close(sock_fd);
  return 0;
}
