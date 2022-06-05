/**
 * @file clm_bio.c
 * @author Chlamydomonos
 * @brief 对bio.c改进后的文件
 * 
 * 目前，bio.c中的函数被命名为clm_xxx的格式。最后，此文件的函数将会覆盖bio.c。
 */

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "clm_buf.h"

/**
 * @brief 改进了bcache
 * 
 * 取消了head
 * 增加了一个用于LRU算法的堆
 */
struct {
  struct spinlock lock;
  struct clm_buf buf[NBUF];
  int bufHeap[NBUF];
  int bufHeapSize;
} clm_bcache;

/**
 * @brief 改进了binit
 * 
 * 删除了原来的链表初始化相关内容
 * 增加了堆的初始化
 */
void
clm_binit(void)
{
  struct clm_buf *b;

  initlock(&clm_bcache.lock, "bcache");
  clm_bcache.bufHeapSize = 0;

  for(b = clm_bcache.buf; b < clm_bcache.buf+NBUF; b++)
  {
    initsleeplock(&b->lock, "buffer");
  }
}

static struct clm_buf*
bget(uint dev, uint blockno)
{
    return 0;
}