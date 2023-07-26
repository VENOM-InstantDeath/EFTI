#ifndef GEARS_H
#define GEARS_H
#include <curses.h>
#include <ncurses.h>
struct Nopt {
	int str_size;
	int underline;
};
struct Fopt {
	int dotfiles;
	char* pwd;
	char* tmp_path;
};
struct Data {
	void* data;
	int ptrs[2];
	WINDOW** wins;
	int wins_size;
};
struct Callback {
	int (**func)(struct Data*, void*);
	void **args;
	int nmemb;
};
struct Binding {
	int *keys;
	int (**func)(struct Data*, char*);
	int nmemb;
};
struct Wobj {
	WINDOW* win;
	struct Data *data;
	struct Callback cb;
	struct Binding bind;
	int local;
	char* pwd;
	char** ls;
};
struct TabList {
	char *list;
	int point;
	int size;
	struct Wobj wobj;
};
void uptime(char* buff);
void dir_up(char *pwd);
void dir_cd(char *pwd, char *dir);
int list(char *path, char*** ls, int dotfiles);
void alph_sort(char** ls, int size);
void pr_ls(char **ls, int size);
void display_opts(WINDOW* win, char **ls, int size, int start, int top, int* ptrs, void* data, int mode);
void display_files(WINDOW* win, char **ls, int size, int start, int top, int* ptrs, void* data, int mode);
int menu(struct TabList *tl, void (*dcb)(WINDOW*,char**,int,int,int,int*,void*,int));
int handleFile(struct Data *data, void* f);
int execute(struct Data *data, char* file);
int view(struct Data *data, char *file);
int updir(struct Data *data, char *file);
int hideDot(struct Data *data, char* file);
int menu_close(struct Data *data, void* args);
int fileRename(struct Data *data, char* file);
int fselect(struct Data *data, char* file);
int fmove(struct Data *data, char* file);
int fcopy(struct Data *data, char* file);
int fdelete(struct Data *data, char* file);
int fnew(struct Data *data, char* file);
int dnew(struct Data *data, char* file);
void tab_init(struct TabList *tl);
void add_tab(WINDOW* tabwin, struct TabList *tl);
void del_tab(WINDOW* tabwin, struct TabList *tl);
void tab_switch(WINDOW* tabwin, struct TabList *tl);
#endif
