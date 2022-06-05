/**
 * @file clm_buf.h
 * @author Chlamydomonos
 * @brief 对buf.h改进后的文件
 */

/**
 * @brief 改进了buf。
 *
 * 增加了两个用来描述哈希表的字段。
 */
struct clm_buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uint hash;     ///< 该buffer的哈希值，范围为[0, NBUF)，值为NBUF时代表该buffer从未被分配哈希值
  uchar inTable; ///< 该buffer是否正在哈希表中，相当于布尔值
  uchar data[BSIZE];
};

