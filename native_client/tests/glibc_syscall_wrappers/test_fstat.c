/*
 * Copyright 2010 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
  struct stat st;
  FILE* file;
  int fd;
  if (2 != argc) {
    printf("Usage: sel_ldr test_fstat.nexe test_stat_data\n");
    return 1;
  }
  st.st_size = 0;
  printf("st_dev offset %d, size %d\n",
         offsetof(struct stat, st_dev),
         sizeof(st.st_dev));
  printf("st_ino offset %d, size %d\n",
         offsetof(struct stat, st_ino),
         sizeof(st.st_ino));
  printf("st_mode offset %d, size %d\n",
         offsetof(struct stat, st_mode),
         sizeof(st.st_mode));
  printf("st_nlink offset %d, size %d\n",
         offsetof(struct stat, st_nlink),
         sizeof(st.st_nlink));
  printf("st_uid offset %d, size %d\n",
         offsetof(struct stat, st_uid),
         sizeof(st.st_uid));
  printf("st_gid offset %d, size %d\n",
         offsetof(struct stat, st_gid),
         sizeof(st.st_gid));
  printf("st_rdev offset %d, size %d\n",
         offsetof(struct stat, st_rdev),
         sizeof(st.st_rdev));
  printf("st_size offset %d, size %d\n",
         offsetof(struct stat, st_size),
         sizeof(st.st_size));
  printf("st_blksize offset %d, size %d\n",
         offsetof(struct stat, st_blksize),
         sizeof(st.st_blksize));
  printf("st_blocks offset %d, size %d\n",
         offsetof(struct stat, st_blocks),
         sizeof(st.st_blocks));
  printf("st_atim offset %d, size %d\n",
         offsetof(struct stat, st_atim),
         sizeof(st.st_atim));
  printf("struct st size %d\n",
         sizeof(st));
  file = fopen(argv[1], "r");
  if (file == NULL)
    return 2;
  fd = fileno(file);
  errno = 0;
  if (fstat(fd, &st))
    return 3;
  printf("%d\n", (int)st.st_size);
  if (errno != 0)
    return 4;
  if (fclose(file))
    return 5;
  errno = 0;
  if (fstat(-1, &st) != -1)
    return 6;
  if (errno != EBADF)
    return 7;
  return 0;
}
