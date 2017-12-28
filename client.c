#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <ctype.h>

/* define */
#define MAX_LINE_LEN 2048 // ASCTLEN, USERLEN, TITLELEN の分だけ小さくできる
#define MAX_TITLE_LEN 128
#define MAX_USER_LEN 128 //ユーザ名
#define MAX_FILTER 100 //フィルタ
#define MAX_ASKTIME_LEN 32
#define NORMAL_LINE_LEN 70 //行数
#define MAX_ENTRY 1000

#define PORT 4000

// #define SUCCESS 1
// #define FAIL 0

/* フラグ */
enum{
  FAILED,
  SUCCESS,
  END,
  S_ERROR, //sending error
  ANY,
  LIST,
};

typedef struct thread{
  char title[MAX_TITLE_LEN];
  char c_time[MAX_ASKTIME_LEN];
  struct thread *next;
}Thread;

/* グローバルな変数 */
int  write_flag;
char user_name[MAX_USER_LEN];
char title_name[MAX_TITLE_LEN];
//char filter_file[MAX_USER_LEN];
char filter_file[MAX_USER_LEN+strlen(".filter")+1];
Thread **list, *tail;

char *fill_space(char *s)
{
  while(isspace(*s)){
    *s = '\0';
    s = &s[1];}
  return s;
}

/* 文字列 s を sep で最大 n 個に分割して，分割した文字列の先頭アドレスを ret に格納 */
int split(char *s, char *sstr[], char sep, int n)
{
  int i = 1;
  s = fill_space(s);
  sstr[0] = s;
  while(*s != '\0' && i < n){
    if(*s == sep){
      *s = '\0';
      s = fill_space(s+1);
      sstr[i++] = s;
    }
    s++;
  }
  if(i == n)  return i;
  else          return 0;
}

/* 文字列 s 中の文字 from を to で置き換える */
void subst(char *s, char f, char t)
{
  while(*s != '\0'){
    if(*s == f)  *s = t;
    s++;
  }
}


int auto_strncmp(char *s, char *t){
  return strncmp(s, t, strlen(t));
}

int my_send(int fd, char *s)
{
  int len = strlen(s);
  if(send(fd, s, len+1, 0) != len+1) return 0;
  else return 1;
}

int send_NG(int fd){return my_send(fd ,"_NG");}
int send_OK(int fd){return my_send(fd ,"_OK");}
int send_END(int fd){return my_send(fd ,"_END");}

int check_list(char *title, char *t)
{
  Thread *tmp = *list;
  while(tmp != tail){
    if(strcmp(tmp->title, title) == 0){
      if(strcmp(tmp->c_time, t) == 0){
        return 1;
      }
      else{
        memset(tmp->c_time, 0, sizeof(tmp->c_time));
        strcpy(tmp->c_time, t);
        return 2; //マジックナンバになっている
      }
    }
    tmp = tmp->next;
  }
  return 0;
}


void insert(Thread **list, Thread *newData ){
  Thread *tmp = *list;

  *list = newData;
  newData->next = tmp;
}

int reg_list(char *title, char *t)
{
  Thread *new_thread;
  int i = 0; //エラーチェックのため

  i = check_list(title, t);
  if(i == 1) return 0; // no
  else if(i == 2) return 1; //yes

  new_thread = malloc(sizeof(Thread));
  memset(new_thread->title, 0, sizeof(new_thread->title));
  memset(new_thread->c_time, 0, sizeof(new_thread->c_time));

  strcpy(new_thread->title, title);
  strcpy(new_thread->c_time, t);

  insert(list, new_thread);
  return 0;
}

void print_list(char *buf)
{
  char *sstr[2];
  int i = 0;

  subst(buf, '\n', '\0');

  if(split(buf, sstr, '|', 2) == 0) return;

  i = reg_list(sstr[0], sstr[1]);
  printf("%-32s%-32s", sstr[0], sstr[1]); fflush(stdout);

  if(i == 1) printf(" New!\n");
  else printf("\n");
}

void printline(int n)
{
  while(n--)
    write(1, "-", sizeof("-"));
  write(1, "\n", sizeof("\n"));
}

int check_status(int fd)
{
  char tmp[MAX_LINE_LEN];
  int  recv_len = 0;

  memset(tmp, 0, sizeof(tmp));
  recv_len = recv(fd, tmp, sizeof(tmp), 0);
  if(recv_len > 0){
    if(auto_strncmp(tmp, "_OK") == 0) ;//なにもしない
    else if(auto_strncmp(tmp, "_NG") == 0) return FAILED;
    else if(auto_strncmp(tmp, "_END") == 0) return END;
    else{
      switch(write_flag){
        case ANY:
          if(write(1, tmp, strlen(tmp)+1) != strlen(tmp)+1)  return S_ERROR;
          else return FAILED;
        case LIST:
          print_list(tmp);
          break;
        default:
          ; //なにもしない
      }
    }
  }
  else{
    printf("Cannot receive the message.\n");
    send_NG(fd);
    return S_ERROR;
  }
  return SUCCESS;
}

//openコマンド/
//nameをチェック
int bad_name(char *s)
{
  while(*s != '\0'){
    if(*s == '/' ||
       (*s == '.' && s[1] == '.'))
      return 1; //bad
    s++;
  }
  return 0; //correct
}

int send_and_check(int fd, char *s)
{
  int status = SUCCESS;

  if(my_send(fd, s) == 0){
    printf("Error: Cannot send a message.\n");
    return FAILED;
  }
  status = check_status(fd);
  if(status == FAILED)
    printf("send_and_check: Cannot get the server message.\n");//エラーチェック

  return status;
}


int exec_open(int fd, char *s)
{
  char *sstr[2];
  int status = SUCCESS;

  if(title_name[0] != '\0'){
    memset(title_name, 0 , sizeof(title_name));
    printf("That title is already exits. Clear old title.\n");
  }

  subst(s, '\n', '\0');
  if(split(s, sstr, ' ', 2) == 0) return FAILED;

  if(bad_name(sstr[1])){
    printf("Bad name! > %s\n", s);
    return FAILED;
  }
  if(strncpy(title_name, sstr[1], strlen(sstr[1])+1) > 0){
    if(send_and_check(fd, sstr[0]) != SUCCESS) return FAILED;
    if((status = send_and_check(fd, sstr[1])) == FAILED){
      printf("Fail to get title list.\n");
      return FAILED;
    }
    else if(status == END)
      printf("%s is not exists. New title.\n", sstr[1]);
    else
      printf("%s is exits.\n", sstr[1]);

    printf("Set title > %s\n", title_name);
  }
  else{
    printf("Cannot get title name. Please try again.\n");
    return FAILED;
  }
  return SUCCESS;
}

//closeコマンド
//
int exec_close(int fd, char *s)
{
  printf("Close %s.\n", title_name);
  if(memset(title_name, 0, sizeof(title_name)) != title_name)
    return FAILED;
  return SUCCESS;
}

//exitコマンド
int exec_exit(int fd, char *s){
  exit(0);
}

//listコマンド
//
int exec_list(int fd, char*s)
{
  int status = SUCCESS;;
  char *sstr[2];

  write_flag = LIST;

  subst(s, '\n', '\0');
  split(s, sstr, ' ', 2);  //"list"の後のスペースを削除

  status = send_and_check(fd, sstr[0]);

  if(status == SUCCESS){
    if(send_OK(fd) == 0)  return S_ERROR;
  }
  else if(status == FAILED){
    if(send_NG(fd) == 0)  return S_ERROR;
  }

  printf("Title---------------------------");  fflush(stdout);
  printf("Last Update");  fflush(stdout);
  printline(NORMAL_LINE_LEN - 32 - strlen("Last Update"));  fflush(stdout);
  while(1){
    status = check_status(fd);
    switch(status){
      case FAILED: case S_ERROR:
        printf("Fail to get the title list.\n");
      case END:
        goto break_loop;
      default:
        if(send_OK(fd) == 0)  return S_ERROR;
    }
  }
 break_loop:
  printline(NORMAL_LINE_LEN);
  return SUCCESS;
}


//printコマンド

//date|name|text... => name: XXX date:
//                     text:
int print_data(char *s, char **filters, int filter_size)
{
  char *sstr[4];
  int   i = 0;

  if(split(s, sstr, '|', 4) == 0){
    printf("print_data: Incorrect text type.\n%s\n", s);
    return FAILED;
  }

  subst(sstr[3], '\n', '\0');

  for(i = 0; i < filter_size; i++){
    if(strncmp(filters[i], sstr[2], strlen(sstr[1])) == 0){
      printf("%s:Blocked by filter.(%s)\n", sstr[0], filters[i]);
      return SUCCESS;
    }
  }
  printf("%s",        sstr[0]); fflush(stdout);
  printf("NAME:%s ",  sstr[2]); fflush(stdout);
  printf("DATE:%s\n", sstr[1]); fflush(stdout);
  printline(NORMAL_LINE_LEN);          fflush(stdout);
  printf("%s\n",      sstr[3]); fflush(stdout);
  printline(NORMAL_LINE_LEN);          fflush(stdout);
  printf("\n");                 fflush(stdout);

  return SUCCESS;
}


int rs_data(int fd, char **filters, int filter_size)
{
  char buf[MAX_LINE_LEN];

  if(recv(fd, buf, sizeof(buf), 0) > 0){
    if(auto_strncmp(buf, "_END") == 0) return END;
    else if(auto_strncmp(buf, "_NG") == 0) return FAILED;

    if(print_data(buf, filters, filter_size) == SUCCESS){
      if(send_OK(fd) == 0) return S_ERROR;
      else return SUCCESS;
    }
  }
  else{
    printf("rs_data: Cannot receive the message from server.\n");
  }
  if(send_NG(fd) == 0) return S_ERROR;
  else return FAILED;
}

int exec_print(int fd, char *s)
{
  char *filters[MAX_FILTER];
  char tmp[MAX_LINE_LEN];
  char *sstr[2];
  char arg[32];
  int  i = 0, j = 0, n = 0;
  FILE *fp;
  int status = SUCCESS;

  memset(arg, 0, sizeof(arg));

  subst(s, '\n', '\0');
  n = split(s, sstr, ' ', 2);

  //コマンドをsend
  status = send_and_check(fd, sstr[0]);
  if(status == FAILED || status == S_ERROR)
    return FAILED;

  //"."  ".."  "/" はダメ
  if(strlen(title_name) < 1){
    printf("Set title name like this.\n");
    printf("\"(bbs)open title\"\n");
    if(send_NG(fd) == 0) return S_ERROR;
    else return SUCCESS;
  }

  //スレッド（板）名をsend
  if((status = send_and_check(fd, title_name)) == FAILED)
    return FAILED;
  else if(status == END){
    printf("Cannot read datas from %s.\n", title_name);
    return SUCCESS;
  }

  if(n == 0) strcpy(arg, "all");
  else strncpy(arg, sstr[1], strlen(sstr[1])+1);

  if(send_and_check(fd, arg) == FAILED)
    return FAILED;

  if((fp = fopen(filter_file, "r")) != NULL){
    while(fgets(tmp, sizeof(tmp), fp)){
      subst(tmp, '\n', '\0');
      filters[i] = (char*)malloc(sizeof(char)*strlen(tmp) + 1);
      strcpy(filters[i++], tmp);
    }
    fclose(fp);
  }

  //データをrecv
  while(1){
    status = rs_data(fd, filters, i);
    if(status == END) break;
    else if(status == FAILED || status == S_ERROR){
      printf("Fail to receive server's message.\n");
      return FAILED;
    }
  }

  for(j = 0; j < i; j++)
    free(filters[j]);

  printf("Finished.\n");
  return SUCCESS;
}

//writeコマンド
//
int exec_write(int fd, char *s)
{
  char send_buf[MAX_LINE_LEN];
  char read_str[MAX_LINE_LEN];
  int status = SUCCESS;
  int read_len = 0;

  memset(send_buf, 0, sizeof(send_buf));
  memset(read_str, 0, sizeof(read_str));

  //connectしてるか
  status = send_and_check(fd, s);
  if(status == FAILED || status == S_ERROR)
    return FAILED;

  //titleを取得
  if(title_name[0] == '\0'){
    printf("Set title!\n");
    if(send_NG(fd) == 0) return S_ERROR;
    else return FAILED;
  }
  else{
    if(send_OK(fd) == 0) return S_ERROR;
  }

  if(write(1, "text:", strlen("text:")) != strlen("text:"))
    return FAILED;
  if((read_len = read(0, read_str, sizeof(read_str))) > 0){
    if(read_len >= MAX_LINE_LEN)
      read_str[MAX_LINE_LEN - 1] = '\0';
    subst(read_str, '\n', '\0');
    sprintf(send_buf, "%s|%s|%s", title_name, user_name, read_str);

    if(my_send(fd, send_buf) == 0)
      return FAILED;
    while(1){
      status = check_status(fd);
      switch(status){
        case END:
          printf("Send message.\n");
          return SUCCESS;
        case FAILED:
          printf("Error:Cannot write text.\n");
        case S_ERROR:
          return FAILED;
        default:
	      ; //なにもしない
      }
    }
  }
  else
    printf("Cannot read your input. Try again.\n");

  return FAILED;
}


//filterコマンド
int addfilter(char *s)
{
  FILE *fp;
  char tmp[MAX_LINE_LEN];

  memset(tmp, 0, sizeof(tmp));

  if((fp = fopen(filter_file, "a+")) == NULL){
    printf("Cannot open file.\n");
    return FAILED;
  }

  while(fgets(tmp, sizeof(tmp), fp) > 0)
    if(strncmp(s, tmp, strlen(s)) == 0){
      printf("\"%s\" is already registered.\n", s);
      return SUCCESS;
    }

  fprintf(fp, "%s\n", s);
  printf("Add \"%s\" to your filter.\n", s);
  fclose(fp);
  return SUCCESS;
}

// "s" -> "s" "\0"
int find_and_del(FILE *fp, FILE *tmp, char *s)
{
  char str[MAX_LINE_LEN];

  while(fgets(str, sizeof(str), fp)){
    if(strncmp(s, str, strlen(s)) != 0)
      fprintf(tmp, "%s", str);
  }
  subst(str, '\n', '\0');
  printf("Remove \"%s\" from filter file.\n", str);
  return SUCCESS;
}

int delfilter(char *s)
{
  FILE *fp, *tmp;

  if((fp = fopen(filter_file, "r+")) == NULL){
    printf("Cannot open file.\n");
    return FAILED;
  }

  if((tmp = fopen("tmp.filter", "w")) == NULL){
    printf("Cannot open file.\n");
    return FAILED;
  }

  if(find_and_del(fp, tmp, s) == FAILED)  return FAILED;

  remove((const char*)filter_file);
  rename("tmp.filter", (const char*)filter_file);
  fclose(tmp);
  return SUCCESS;
}

int exec_filter(int fd, char *s)
{
  //ポインタを移動
  subst(s, '\n', '\0');
  while(isspace(*s) == 0 && *s != '\0')
    s++;
  s = fill_space(s);

  if(s[0] == '+')       return addfilter(&s[1]);
  else if(s[0] == '-')  return delfilter(&s[1]);
  else                  return FAILED;
}


int est_connect(int sock_fd, struct sockaddr_in *saddr)
{
  //socket
  if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    fprintf(stderr, "Error: socket\n");
    return FAILED;
  }
  //establish connection
  if (connect(sock_fd, (struct sockaddr*)saddr, sizeof(*saddr)) < 0){
    fprintf(stderr, "est_connect: fail to establish connection\n");
    return FAILED;
  }
  return sock_fd;
}


int reg_user()
{
  FILE *fp;
  memset(user_name, 0, sizeof(user_name));
  memset(filter_file, 0, sizeof(filter_file));

  if(write(1, "user name > ", strlen("user name > ")) !=
     strlen("user name > ")){
    return -1;
  }

  if(read(0, user_name, sizeof(user_name)) <= 0)
    strcpy(user_name, "noname");
  else
    subst(user_name, '\n', '\0');

  printf("User \"%s\" recognized...\n\n", user_name);
  sprintf(filter_file, "%s.filter", user_name);
  if((fp = fopen(filter_file, "r")) != NULL){
    printf("Open filter file --> %s\n\n", filter_file);
    fclose(fp);
  }
  return 1;
}

int exec_help()
{
	printf("========================COMMAND LIST=====================================\n");
  printf("help        : print this message.\n");
  printf("open <title>: open the thread board of <title>.\n");
  printf("            : if <title> doesn't exist, make the thread board of <title>.\n");
  printf("close       : close the thread board opened by \"open\".\n");
  printf("list        : print titles of thread boards.\n");
  printf("print       : print entries of the thread board opened by \"open\".\n");
  printf("write       : write to the thread board opened by \"open\"\n");
  printf("filter      : add entry to filter.\n");
	printf("exit        : exit from this program.\n");
	printf("=========================================================================\n");

  return SUCCESS;
}

int exec_command(char *s, struct sockaddr_in *saddr)
{
  int (* command)(int, char*);
  int sock_fd, status;
  int conn_flag = 0;

  write_flag = ANY;

  s = fill_space(s);
  if(auto_strncmp(s, "open") == 0){
    command = exec_open;
    conn_flag = 1;
  }
  else if(auto_strncmp(s, "close") == 0){
    command = exec_close;
  }
  else if(auto_strncmp(s, "exit") == 0){
    command = exec_exit;
  }
  else if(auto_strncmp(s, "list") == 0){
    command = exec_list;
    conn_flag = 1;
  }
  else if(auto_strncmp(s, "print") == 0){
    command = exec_print;
    conn_flag = 1;
  }
  else if(auto_strncmp(s, "write") == 0){
    command = exec_write;
    conn_flag = 1;
  }
  else if(auto_strncmp(s, "filter") == 0){
    command = exec_filter;
  }
  else if(auto_strncmp(s, "help") == 0){
    command = exec_help;
  }
  else{
    printf("Unknown command.\n");
    return FAILED;
  }

  if(conn_flag == 1)
    if((sock_fd = est_connect(sock_fd, saddr)) == FAILED)
      return FAILED;

  status = command(sock_fd, s);
  if(conn_flag == 1)
    close(sock_fd);
  return status;
}



int main(int argc, char *argv[])
{
  struct hostent *hp;
  struct sockaddr_in saddr;
  int  read_len = 0;
  char recv_buf[MAX_LINE_LEN];
  char send_buf[MAX_LINE_LEN];

  write_flag = ANY;
  tail = malloc(sizeof(Thread));
  list = malloc(4);
  *list = tail;

  memset(user_name, 0, sizeof(user_name));
  memset(title_name, 0, sizeof(title_name));
  memset(filter_file, 0, sizeof(filter_file));

  if (argc != 2){
    printf("Usage: ./exec_file [hostname]\n");
    exit(1);
  }
  //接続先のアドレスを得る
  if ((hp = gethostbyname(argv[1])) == NULL){
    fprintf(stderr, "Sorry, fail to get host name...:%s\n", argv[1]);
    exit(1);
  }
  //ソケットに紐づける値をセット
  saddr.sin_family = hp->h_addrtype;
  saddr.sin_port = htons(PORT);
  bzero(&saddr.sin_addr, 12);
  bcopy(hp->h_addr, &saddr.sin_addr, hp->h_length);

  if(reg_user() == -1)  exit(1); //ユーザ名入力のエラーチェック

  printf("Please input any commands.\n");
  printf("If you don't know any commands, please input \"help\".\n");

  while(1){
    read_len = 0;
    //バッファの初期化
    memset(recv_buf, 0, sizeof(recv_buf));
    memset(send_buf, 0, sizeof(send_buf));

    write(1, "(bbs)", strlen("(bbs)"));
    if((read_len = read(0, send_buf, sizeof(send_buf))) <= 1){
      printf("Read error!\n");
    }
    else{
      switch(exec_command(send_buf, &saddr)){
        case SUCCESS: break;

        case FAILED:
          printf("Fail to execute command.\n");
          break;

        default:
          printf("Error: Undefined execution.\n");
      }
    }
  }
  return 0;
}
