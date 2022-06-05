/**
 * @file clm_buf.h
 * @author Chlamydomonos
 * @brief 对buf.h改进后的文件
 */

struct clm_buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uchar data[BSIZE];
};

