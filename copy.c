#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#define MAX_FILE_LEN 131072 
#define MAX_FILES 2048
#define MAX_HEADER_SIZE 512

#if defined(DEBUG)
#define debugprint(...) printf("[DEBUG]: " __VA_ARGS__)
#else
#define debugprint(...) ;
#endif

char *tocopy;
int fileamount = 0;

void append(char *filename) {
  FILE *file = fopen(filename,"r");
  if (!file) { printf("Couldn't find file: %s\n", filename); return; }

  char running[MAX_FILE_LEN];
  char header[MAX_HEADER_SIZE];

  size_t readbytes = fread(running, 1, sizeof(running)-1, file);
  running[readbytes] = '\0';
  fclose(file);

  snprintf(header,sizeof(header), "=== %s ===\n", filename);
  strcat(tocopy, header);
  strcat(tocopy, running);
  fileamount++;
  return;
}

typedef struct {
  const char *bytes;
  int len;
} sig_t;
#define sig(t) (sig_t) { .bytes = t, .len = sizeof(t)-1 }
const sig_t sigs[] = {
  sig("\x7F\x45\x4C\x46"), // ELF
  sig("\x4D\x5A"), // EXE/DLL
  sig("\xFF\xD8\xFF"), // JPEG
};

int isnontext(char* path) {
  FILE *file = fopen(path,"rb");
  if (!file) {printf("Unable to find file when checking isnontext: %s", path); return 1;}
  char fbytes[16];
  int nread = fread(fbytes,1,sizeof(fbytes),file);
  fclose(file);
  
  for (int i = 0; i < sizeof(sigs) / sizeof(sig_t); i++) {
    sig_t badsign = sigs[i];
    if (nread >= badsign.len && !memcmp(badsign.bytes,fbytes,badsign.len)) {
      return 1;
    }
  }

  return 0;
}

char **gitignored;
int numgitignored = 0;
int isgitignored(char* path) {
  for (int i = 0; i < numgitignored; i++) {
    if (!fnmatch(gitignored[i], path, FNM_PATHNAME | FNM_PERIOD)) {
      debugprint("Ignoring file: %s\n",path);
      return 1;
    }
  }
  return 0;
}

void getfiles(char **filearr, int *filearrc, char *path) {
  DIR *d = opendir(path);
  if (!d) {printf("Couldn't find dir: %s\n", path); return;}
  struct dirent *dir;
  
  char ignorepath[PATH_MAX];
  snprintf(ignorepath,sizeof(ignorepath),"%s/.gitignore",path);
  FILE *ignorefile = fopen(ignorepath,"r");
  if (ignorefile) {
    for (char line[PATH_MAX-1]; fgets(line, sizeof(line), ignorefile); numgitignored++) {
      char tmp[PATH_MAX];
      line[strcspn(line,"\n")] = '\0';
      
      if (strcmp(path,".")) {
        snprintf(tmp,sizeof(tmp),"%s/%s",path,line);
      } else {
        strcpy(tmp,line);
      }

      gitignored[numgitignored] = malloc(PATH_MAX);
      strcpy(gitignored[numgitignored],tmp);
    }
    fclose(ignorefile);
  }

  while ((dir = readdir(d))) {
    char filepath[PATH_MAX];
    if (!strcmp(path,".")) {
      strcpy(filepath,dir->d_name);
    } else {
      snprintf(filepath, sizeof(filepath),"%s/%s",path,dir->d_name);
    }
    
    char dirpath[PATH_MAX+1]; snprintf(dirpath,sizeof(dirpath),"%s/",filepath); 
    if (isgitignored(dir->d_type == DT_DIR ? dirpath : filepath)) {continue;}

    switch(dir->d_type) {
      case DT_DIR:
        if (!strcmp(dir->d_name,".") || !strcmp(dir->d_name,"..") || !strcmp(dir->d_name,".git")) {continue;}
        getfiles(filearr,filearrc,filepath);
        break;
      case DT_REG:;
        if (isnontext(filepath)) {continue;}
        filearr[(*filearrc)++] = strdup(filepath);
        break;
    }
  }
  closedir(d);
}

void copyfiles(int filec, char** files) {
  char** filearr = malloc(sizeof(char*)*MAX_FILES);
  int filearrc = 0;
  gitignored = malloc(sizeof(char*)*MAX_FILES);
  getfiles(filearr, &filearrc, ".");

  for (int i = 0; i < filearrc; i++) {
    for (int j = 0; j < filec; j++) {
      if (!fnmatch(files[j],filearr[i],0)) {
        debugprint("Appending file: %s\n", filearr[i]);
        append(filearr[i]);
        break;
      }
    }
  }

  if (numgitignored) {
    debugprint("Printing gitignore file:\n");
    for (int i = 0; i < numgitignored; i++) {
      debugprint("Ignored: %s\n", gitignored[i]);
    }
  }

  for (int i = 0; i < filearrc; i++) {free(filearr[i]);} free(filearr);
  for (int i = 0; i < numgitignored; i++) {free(gitignored[i]);} free(gitignored);
}

int main(int argc, char* argv[]) {
  system("xclip -selection clipboard /dev/null");
  tocopy = malloc((MAX_HEADER_SIZE + MAX_FILE_LEN) * MAX_FILES); tocopy[0] = '\0';

  if (argc > 1) {
    copyfiles(argc-1, argv+1);
  } else {
    char* files[] = {"*"};
    copyfiles(1,files);
  }

  FILE *pipe = popen("xclip -selection clipboard", "w");
  if (pipe) {
    fputs(tocopy, pipe);
    pclose(pipe);
    printf(fileamount <= 0 ? "No files copied.\n" : "Succesfully copied %d files!\n", fileamount);
  } else {
    fprintf(stderr, "Clipboard copy failed\n");
  }

  free(tocopy);
  return 0;
}
