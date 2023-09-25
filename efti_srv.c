#include <curses.h>
#include <ncurses.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "gears.h"
#include "efti_srv.h"
#include "libncread/vector.h"
#include "libncread/ncread.h"
#include "logger/logger.h"

int get_addr(char **s, char *a) {
	int p=0; int px=0;
	for (int i=0; i<strlen(a); i++) {
		if (!((a[i] >= '0' && a[i] <= '9') || a[i] == ':' || a[i] == '.')) {return 0;}
		if (px==2) {return 0;}
		if (a[i] == ':') {px++;p=0;continue;}
		if ((!px && p==15) || (px && p==5)) {return 0;}
		s[px][p] = a[i];
		p++;
	}
	return 1;
}

void server_create(struct TabList* tl) { /*Create server's process*/
	WINDOW* stdscr = tl->wobj[0].data->wins[0];
	WINDOW* main = tl->wobj[0].data->wins[4];
	WINDOW* wfiles = tl->wobj[0].data->wins[5];
	WINDOW* wins[] = {stdscr, main, wfiles};
	char *tmp_path = TMP_PATH;
	int tmp_size = strlen(tmp_path);
	char* tp1 = malloc(tmp_size+5+1);
	char* tp2 = malloc(tmp_size+9+1);
	strcpy(tp1, tmp_path); strcat(tp1, "/efti");
	strcpy(tp2, tmp_path); strcat(tp2, "/efti/pid");
	create_dir_if_not_exist(tp1);
	if (open(tp2, O_RDONLY) != -1) { /*if server exists, don't create another one*/
		return;
	}
	int fd[2];
	pipe(fd);
	pid_t pid = fork();
	if (!pid) {
		server_main(tl, fd);
		exit(0);
	}
	char buff[1]={0};
	read(fd[0], buff, 1);
	if (!buff[0]) {
		dialog(wins, "Server failed to initialize. Error on bind(); check the port and address.");
		free(tp1);free(tp2);
		return;
	}
	FILE *F = fopen(tp2, "w");
	fprintf(F, "%d\n", pid);
	fclose(F);
	free(tp1); free(tp2);
	return;
}

void server_main(struct TabList* tl, int* fd) { /*server: listen for connections*/
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(tl->settings.port));
	if (!tl->settings.srv_local) addr.sin_addr.s_addr = INADDR_ANY;
	else inet_aton("127.0.0.1", &addr.sin_addr);

	socklen_t addsize = sizeof(addr);
	int serv = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(serv, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)sizeof(opt));
	int r = bind(serv, (struct sockaddr*)&addr, addsize);
	if (r==-1) {
		write(fd[1], "\0", 1);
		return;
	}
	else {
		write(fd[1], "\1", 1);
	}
	listen(serv, 1);
	for (;;) {
		int fd = accept(serv, (struct sockaddr*)&addr, &addsize);
		/*check if new connection is a valid client*/
		struct pollfd pfd[1]; pfd[0].events=POLLIN; pfd[0].fd=fd;
		if (!poll(pfd, 1, 2000)) {close(fd);continue;}
		char buff[5] = {0};
		read(fd, buff, 4);
		if (strcmp(buff, "3471")) {close(fd);continue;}
		pthread_t t1;
		pthread_create(&t1, NULL, server_handle, &fd);
	}
}

int get_err_code(int fd) {
	char buff[1];
	int r = read(fd, buff, 1);
	return buff[0]-48;
}

struct Srvdata get_answ(struct TabList* tl, int fd) {
	struct Srvdata sd;
	char *buff = calloc(5,1); handleMemError(buff, "calloc(2) on get_answ");
	if (!read(fd, buff, 4) && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
	char *pbuff = buff;
	int digits = atoi(pbuff);
	pbuff = calloc(digits+1, 1); handleMemError(pbuff, "calloc(2) on get_answ");
	if (!read(fd, pbuff, digits) && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
	int size = atoi(pbuff);
	sd.size = size;
	free(pbuff); pbuff = calloc(size+1, 1); handleMemError(pbuff, "calloc(2) on get_answ");
	if (!read(fd, pbuff, size) && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
	sd.content=pbuff;
	return sd;
}

struct Srvdata get_fdata(struct TabList* tl, int fd) {
	struct Srvdata sd;
	char *buff = calloc(5,1); handleMemError(buff, "calloc(2) on get_answ");
	if (!read(fd, buff, 4) && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
	char *pbuff = buff;
	int digits = atoi(pbuff);
	pbuff = calloc(digits+1, 1); handleMemError(pbuff, "calloc(2) on get_answ");
	if (!read(fd, pbuff, digits) && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
	int size = atoi(pbuff);
	sd.size = size;
	free(pbuff);
	char *fdata = malloc(size); handleMemError(fdata, "calloc(2) on get_answ");
	int p=0;
	while (size != p) {
		int r = read(fd, fdata, size-p);
		if (!r && tl) {handleDisconnection(tl);sd.content=NULL;sd.size=-1;return sd;}
		fdata += r;
		p += r;
	}
	fdata -= size;
	sd.content=fdata;
	return sd;
}

void *server_handle(void* conn) { /*server's core*/
	int fd = *((int*)conn);
	for (;;) {
		struct pollfd rfd[1]; rfd[0].fd=fd;rfd[0].events=POLLIN;
		poll(rfd, 1, -1);
		int order=0;
		struct Srvdata sd;sd.content=NULL;
		char buff[1]={0}; int r = read(fd, buff, 1);
		vlog(&r, "server r", INT, __FILE__, __LINE__);
		if (buff[0] != 0) {
			order=buff[0]-48;
			vlog(&order, "order", INT, __FILE__, __LINE__);
			sd = get_answ(NULL, fd);
			vlog(&sd.size, "sd.size", INT, __FILE__, __LINE__);
			if (sd.size==-1) {
				slog("Client disconnected", __FILE__, __LINE__);
				return 0;
			}
		}
		switch (order) {
			case OP_PING: { /*Can be used to disconnect for inactivity*/
				char tmpbf[1] = {1};
				write(fd, tmpbf, 1);
				break;
			}
			case OP_DOWNLOAD: {
				FILE *fn = fopen(sd.content, "rb");
				struct stat st; stat(sd.content, &st);
				if (st.st_size > TRANSF_LIMIT) {
					char buff[1]={0};
					write(fd, buff, 1);
					break;
				}
				else {
					char buff[1]={1};
					write(fd, buff, 1);
				}
				char *buffer = malloc(st.st_size);
				int r = fread(buffer, 1, st.st_size, fn);
				fclose(fn);
				struct string ts; string_init(&ts);
				char files_size[11] = {0}; snprintf(files_size, 11, "%lu", st.st_size);
				string_add(&ts, itodg(enumdig(st.st_size)));
				string_add(&ts, files_size);
				string_nadd(&ts, st.st_size, buffer);
				int p = 0;
				while (ts.size != p) {
					int r = write(fd, ts.str, ts.size);
					p += r;
				}
				free(buffer);
				string_free(&ts);
				break;
			}
			case OP_UPLOAD: {
				char *path = sd.content;
				FILE *FN = fopen(path, "wb");
				sd = get_fdata(NULL, fd);
				fwrite(sd.content, 1, sd.size, FN);
				fclose(FN);
				free(path);
				break;
			}
			case OP_LIST_FILES: {
				DIR *dir = opendir(sd.content);
				int path_size = sd.size;
				char *path=calloc(sd.size+1,1); strcpy(path, sd.content);
				sd = get_answ(NULL, fd);
				if (sd.size==-1) {
					slog("Client disconnected", __FILE__, __LINE__);
					return 0;
				}
				int dotfiles = atoi(sd.content);
				struct dirent *dnt;
				struct string files, hstr, attrs;
				string_init(&files); string_init(&hstr); string_init(&attrs);
				while ((dnt=readdir(dir))!=NULL) {
					if (!strcmp(dnt->d_name, ".") || !strcmp(dnt->d_name, "..")) continue;
					if (dotfiles && dnt->d_name[0] == '.') continue;
					string_add(&files, dnt->d_name);
					string_addch(&files, '/');
					char* name = calloc(path_size+strlen(dnt->d_name)+1, 1);
					strcpy(name, path);
					strcat(name, dnt->d_name);
					struct stat st; stat(name, &st);
					if (S_ISDIR(st.st_mode)) string_addch(&attrs, FA_DIR+48);
					else if (!S_ISDIR(st.st_mode) && st.st_mode & S_IXUSR)
						string_addch(&attrs, FA_EXEC+48);
					else string_addch(&attrs, 48);
				}
				char* files_size = calloc(enumdig(files.size)+1, 1); snprintf(files_size, 11, "%d", files.size);
				char *files_size_digit = itodg(enumdig(files.size));
				string_add(&hstr, files_size_digit);
				string_add(&hstr, files_size);
				string_add(&hstr, files.str);
				char *attr_size = calloc(attrs.size+2,1);
				snprintf(attr_size, attrs.size+2, "%d", attrs.size);
				char *attr_digit = itodg(enumdig(attrs.size));
				string_add(&hstr, attr_digit);
				string_add(&hstr, attr_size);
				string_add(&hstr, attrs.str);
				int wres = write(fd, hstr.str, hstr.size);
				string_free(&hstr); string_free(&files); string_free(&attrs);
				closedir(dir);
				break;
			}
			case OP_GET_HOME: {
				char *home = getenv("HOME");
				char *size = calloc(enumdig(strlen(home))+1+1,1); /*+/ +\0*/ handleMemError(size, "calloc(2) on server_handle");
				sprintf(size, "%lu", strlen(home)+1);
				struct string hstr;
				string_init(&hstr);
				string_add(&hstr, itodg(enumdig(strlen(home))));
				string_add(&hstr, size);
				string_add(&hstr, home);
				string_addch(&hstr, '/');
				write(fd, hstr.str, hstr.size);
				slog("Server finished order 5", __FILE__, __LINE__);
				break;
			}
			case OP_MOVE: { /*rename or move*/
				char *A, *B;
				A = sd.content;
				sd = get_answ(NULL, fd);
				if (sd.size==-1) {
					slog("Client disconnected", __FILE__, __LINE__);
					return 0;
				}
				B = sd.content;
				rename(A, B);
				free(A);
				break;
			}
			case OP_COPY: {
				char *A, *B;
				A = sd.content;
				sd = get_answ(NULL, fd);
				if (sd.size==-1) {
					slog("Client disconnected", __FILE__, __LINE__);
					return 0;
				}
				B = sd.content;
				copy(NULL, A, B);
				free(A);
				break;
			}
			case OP_DELETE: {
				remove(sd.content);
				break;
			}
			case OP_NEW_FILE: {
				struct stat st;
				if (stat(sd.content, &st) != -1) break;
				close(open(sd.content, O_WRONLY | O_CREAT));
				break;
			}
			case OP_NEW_DIR: {
				struct stat st;
				if (stat(sd.content, &st) != -1) break;
				mkdir(sd.content, 0700);
				break;
			}
			case 0:
			case OP_DISCONNECT: {
				if (sd.content) free(sd.content);
				slog("Client disconnected", __FILE__, __LINE__);
				close(fd);
				return 0;
			}
		}
		free(sd.content);
	}
	return 0;
}

void server_kill() {
	char *tmp_path = TMP_PATH;
	char *tp1 = malloc(strlen(tmp_path)+9+1);
	strcpy(tp1, tmp_path); strcat(tp1, "/efti/pid");
	int fd = open(tp1, O_RDONLY);
	if (fd == -1) return;
	int pid=0;
	for (;;) {
		char buff[1];
		read(fd, buff, 1);
		if (buff[0] == 10) break;
		pid*=10;pid+=buff[0]-48;
	}
	kill(pid, SIGKILL);
	unlink(tp1);
	free(tp1);
}

char* gethome(struct TabList* tl, int fd) {
	write(fd, "50000", 5);
	slog("wrote, now getansw", __FILE__, __LINE__);
	struct Srvdata sd = get_answ(tl, fd);
	slog("gotansw", __FILE__, __LINE__);
	vlog(&sd.size, "gethome sd.size", INT, __FILE__, __LINE__);
	return sd.content;
}

int* newBindArr_1(int argc, ...) {
	int *arr = malloc(sizeof(int)*argc);
	va_list argv; va_start(argv, argc);
	for (int i=0; i<argc; i++) {arr[i] = va_arg(argv, int);}
	return arr;
}

bindFunc *newBindArr_2(int argc, ...) {
	bindFunc *arr = malloc(sizeof(bindFunc)*argc);
	va_list argv; va_start(argv, argc);
	for (int i=0; i<argc; i++) {arr[i] = va_arg(argv, bindFunc);}
	return arr;
}

int client_connect(struct TabList *tl, struct Data *data, char* file) {
	WINDOW* wr = tl->wobj[0].data->wins[4];
	WINDOW* wfiles = tl->wobj[0].data->wins[5];
	WINDOW* main = tl->wobj[0].data->wins[4];
	WINDOW* tabwin = tl->wobj[0].data->wins[2];
	WINDOW* stdscr = tl->wobj[0].data->wins[0];
	WINDOW* wins[] = {stdscr, main, wfiles};
	int y, x; getmaxyx(stdscr, y, x);
	WINDOW* win = newwin(4, 37, y/2-2, x/2-18);
	getmaxyx(win, y, x);
	wbkgd(win, COLOR_PAIR(1));
	keypad(win, 1);
	mvwaddstr(win, 0, x/2-8, "Connect to remote");
	mvwaddstr(win, 2, 1, "Address:port:");
	wrefresh(win);
	char *buff;
	ampsread(win, &buff, 2, 15, 21, 21, 0, 1);
	delwin(win); touchwin(main); wrefresh(wfiles);
	if (!buff) {
		dialog(wins, "You have to write the port and the address");
		return 1;
	}
	char a[16]={0}; char b[6]={0}; char *arr[2]={a,b};
	if (!get_addr(arr,buff)) {
		dialog(wins, "There was an error on the format of the address");
		return 1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	inet_aton(arr[0], &addr.sin_addr);
	addr.sin_port = htons(atoi(arr[1]));
	socklen_t size = sizeof(addr);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr*)&addr, size) == -1) {
		dialog(wins, "An error ocurred on connect(2)");
		return 1;
	}
	write(fd, "3471", 4);

	add_tab(tabwin, tl);
	tl->wobj[tl->size-1].data = malloc(sizeof(struct Data));
	struct Fopt *fopt = malloc(sizeof(struct Fopt));
	fopt->dotfiles=0;
	tl->tmp_path.path = NULL;
	tl->wobj[tl->size-1].data->data = fopt;
	tl->wobj[tl->size-1].data->wins_size = tl->wobj[0].data->wins_size;
	tl->wobj[tl->size-1].data->wins = tl->wobj[0].data->wins;

	tl->wobj[tl->size-1].local = 0;
	tl->wobj[tl->size-1].pwd = NULL;

	getmaxyx(main, y, x);
	WINDOW* wrf = newwin(y-2, 50, 3, 4);
	keypad(wrf, 1); wbkgd(wrf, COLOR_PAIR(3));
	tl->wobj[tl->size-1].win = wrf;

	tl->wobj[tl->size-1].fd = fd;
	tl->wobj[tl->size-1].ls = NULL;
	tl->wobj[tl->size-1].attrls = NULL;
	tl->wobj[tl->size-1].bind.keys = newBindArr_1(14, 'v','u','h','M','r','s','m','c','D','n','N', 'C', 9, 'q');
	tl->wobj[tl->size-1].bind.func = newBindArr_2(14, view, updir, hideDot, popup_menu, fileRename,
						fselect, fmove, fcopy, fdelete, fnew, dnew, client_connect, b_tab_switch, client_disconnect);
	tl->wobj[tl->size-1].bind.nmemb = 14;
	char* pwd = gethome(tl, fd);
	tl->wobj[tl->size-1].pwd = pwd;
	slog("client_connect returning", __FILE__, __LINE__);
	return 1;
}
int client_disconnect(struct TabList* tl, struct Data* data, char* disconnect_remote) {
	slog("Someone called client_disconnect ._.", __FILE__, __LINE__);
	struct Wobj* wobj = get_current_tab(tl);
	free(wobj->attrls);
	free(wobj->ls);
	free(wobj->pwd);
	free(wobj->data->data);
	free(wobj->data);
	free(wobj->bind.func);
	free(wobj->bind.keys);
	delwin(wobj->win);
	if (*disconnect_remote)
		high_SendOrder(wobj->fd, OP_DISCONNECT, 1, 0, "");
	close(wobj->fd);
	WINDOW* tabwin = tl->wobj[0].data->wins[2];
	del_tab(tabwin, tl);
	return 1;
}
