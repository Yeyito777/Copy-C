#include <fnmatch.h>
#include <stdio.h>
int main() {
  if (!fnmatch("Copy/*","Copy/",0)) {
    printf("Valid");
  }
}
