#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <git2.h>

#define MAX_FILE_LEN 131072 
#define MAX_FILES 4096
#define MAX_HEADER_SIZE 512
#define FLAG_NOGIT (1 << 0)

#if defined(DEBUG)
#define debugprint(...) printf("[DEBUG]: " __VA_ARGS__)
#else
#define debugprint(...) ;
#endif

char *tocopy;
int fileamount = 0;
int totalignored = 0;
unsigned int flags;

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
    sig("\x7F\x45\x4C\x46"),                  // ELF
    sig("\x4D\x5A"),                          // EXE/DLL
    sig("\xFF\xD8\xFF"),                      // JPEG
    sig("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"),  // PNG
    sig("\x49\x44\x33"),                      // MP3 (ID3 tag)
    sig("\xFF\xFB"),                          // MP3 (raw frame, common)
    sig("\x4F\x67\x67\x53"),                  // OGG
    sig("\x25\x50\x44\x46\x2D"),              // PDF ("%PDF-")
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

git_repository **repos;
int repoc = 0;

void getfiles(char **filearr, int *filearrc, char *path, char **match_optim, int match_optimc) {
  DIR *d = opendir(path);
  if (!d) {printf("Couldn't find dir: %s\n", path); return;}
  struct dirent *dir;
  
  char gitpath[PATH_MAX];
  snprintf(gitpath,sizeof(gitpath),"%s/.git",path);
  DIR *git = opendir(gitpath);
  if (git) {
    int err = git_repository_open(&repos[repoc++], path);
    if (err < 0) {
      const git_error *e = git_error_last();
      printf("Error opening repo: %s\n", e ? e->message : gitpath);
    } else {
      debugprint("Succesfully opened repo: %s\n", gitpath);
    }
  }

  while ((dir = readdir(d))) {
    char filepath[PATH_MAX];
    if (!strcmp(path,".")) {
      strcpy(filepath,dir->d_name);
    } else {
      snprintf(filepath, sizeof(filepath),"%s/%s",path,dir->d_name);
    }
    
    int ignored = 0;
    for (int i = 0; i < repoc; i++) {
      int err = git_ignore_path_is_ignored(&ignored, repos[i], filepath);
      if (err < 0) {
        const git_error *e = git_error_last();
        printf("Error checking ignore: %s\n", e ? e->message : "unknown");
      }
      if (ignored) {break;}
    }
    if (ignored && !(flags & FLAG_NOGIT)) {
      totalignored++;
      debugprint("Skipping gitignored file: %s\n", filepath);
      continue;
    }

    switch(dir->d_type) {
      case DT_DIR:
        if (!strcmp(dir->d_name,".") || !strcmp(dir->d_name,"..") || !strcmp(dir->d_name,".git")) {continue;}
        char dirfilepath[PATH_MAX];
        snprintf(dirfilepath,sizeof(dirfilepath),"%s/",filepath);
        for (int i = 0; i < match_optimc; i++) {
          if (!fnmatch(match_optim[i],dirfilepath,0) || match_optim[i][0] == '*' || strstr(match_optim[i], dirfilepath)) { // Only grab dirs whose content could match
            getfiles(filearr,filearrc,filepath,match_optim,match_optimc);
            break;
          }
        }
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
  
  for (int i = 0; i < filec; i++) {
    if (files[i][strlen(files[i])-1] == '/') {
      char dirfix[PATH_MAX];
      snprintf(dirfix,sizeof(dirfix),"%s*",files[i]);
      files[i] = dirfix;
    }
  }
  
  getfiles(filearr, &filearrc, ".", files,filec);

  for (int i = 0; i < filearrc; i++) {
    for (int j = 0; j < filec; j++) {
      if (!fnmatch(files[j],filearr[i],0)) {
        debugprint("Appending file: %s\n", filearr[i]);
        append(filearr[i]);
        break;
      }
    }
  }

  for (int i = 0; i < filearrc; i++) {free(filearr[i]);} free(filearr);
}

int main(int argc, char* argv[]) {
  system("xclip -selection clipboard /dev/null");
  tocopy = malloc((MAX_HEADER_SIZE + MAX_FILE_LEN) * MAX_FILES); tocopy[0] = '\0';
  repos = malloc(sizeof(git_repository*) * 8);
  git_libgit2_init();

  if (argc > 0 && !strcmp(argv[1],"-a")) {
    flags |= FLAG_NOGIT;
    argc--;
    argv++;
  }

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
    if (fileamount <= 0) {
      if (totalignored > 0) {
        printf("No files copied. Did you forget the -a flag?\n");
      } else {
        printf("No files copied.\n");
      }
    } else {
      printf("Succesfully copied %d files!\n", fileamount);
    }
  } else {
    fprintf(stderr, "Clipboard copy failed\n");
  }

  free(tocopy);
  for (int i = 0; i < repoc; i++) {git_repository_free(repos[i]);}
  git_libgit2_shutdown();
  return 0;
}
