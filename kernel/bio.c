// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

/**
 * @brief 改进了bcache。
 *
 * 增加了哈希表；
 * 移除了链表；
 * 增加了堆。
 */
struct
{
    struct spinlock lock;
    struct buf buf[NBUF];

    /**
     * @brief 保存buffer指针的哈希表。
     */
    struct buf *hash[NBUF];

    /**
     * @brief 保存buffer指针的堆。
     */
    struct buf *heap[NBUF];

    /**
     * @brief 目前空闲的buffer数量。
     */
    uint heapSize;

    /**
     * @brief 用于LRU算法的时间戳。
     *
     * 每次访问buffer，该时间戳+1。
     */
    uint timeStamp;
} bcache;

/**
 * @brief 改进了binit。
 *
 * 增加了哈希表的初始化；
 * 移除了链表的初始化；
 * 增加了堆的初始化。
 */
void binit()
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");
    bcache.heapSize = NBUF;

    for (int i = 0; i < NBUF; i++)
    {
        b = bcache.buf + i;
        initsleeplock(&b->lock, "buffer");

        // 初始状态，所有buffer均不在哈希表中，且在堆中
        b->prev = 0;
        b->next = 0;
        bcache.hash[i] = 0;

        b->timeStamp = 0;
        b->heapIndex = i;
        bcache.heap[i] = b;
    }
}

/**
 * @brief 在哈希表中寻找指定buffer
 *
 * @return 如果找到，返回该buffer的地址；
 * 如果未找到，返回空指针。
 */
static struct buf *bFindFromHashTable(uint dev, uint blockno)
{
    uint hash = (dev + blockno) % NBUF;

    if (bcache.hash[hash] == 0)
        return 0;
    else
    {
        struct buf *head = bcache.hash[hash];
        while (head != 0)
        {
            if (head->dev == dev && head->blockno == blockno)
                return head;
            head = head->next;
        }
    }

    return 0;
}

/**
 * @brief 从指定位置开始上滤
 */
static void bPercolateUp(int index)
{
    struct buf *value = bcache.heap[index];
    int parentIndex = (index - 1) / 2;

    while (index >= 0 && bcache.heap[parentIndex]->timeStamp > value->timeStamp)
    {
        bcache.heap[index] = bcache.heap[parentIndex];
        bcache.heap[index]->heapIndex = index;
        index = parentIndex;
        parentIndex = (index - 1) / 2;
    }

    bcache.heap[index] = value;
    value->heapIndex = index;
}

/**
 * @brief 从指定位置开始下滤
 */
static void bPercolateDown(int index)
{
    int maxChild = 2 * (index + 1);
    struct buf *value = bcache.heap[index];
    char goDown = 1;
    while (goDown && maxChild < bcache.heapSize)
    {
        goDown = 0;
        if (bcache.heap[maxChild]->timeStamp < bcache.heap[maxChild - 1]->timeStamp)
            --maxChild;
        if (value->timeStamp < bcache.heap[maxChild]->timeStamp)
        {
            goDown = 1;
            bcache.heap[index] = bcache.heap[maxChild];
            bcache.heap[index]->heapIndex = index;
            index = maxChild;
            maxChild = 2 * (maxChild + 1);
        }
    }
    if (maxChild == bcache.heapSize)
    {
        if (value->timeStamp < bcache.heap[maxChild - 1]->timeStamp)
        {
            bcache.heap[index] = bcache.heap[maxChild - 1];
            bcache.heap[index]->heapIndex = index;
            index = maxChild - 1;
        }
    }
    bcache.heap[index] = value;
    value->heapIndex = index;
}

/**
 * @brief 改进了bget。
 *
 * 获取已缓存buffer的过程改为由哈希表获取；
 * 分配新buffer后将其插入哈希表。
 */
static struct buf *bget(uint dev, uint blockno)
{
    struct buf *b;
    struct buf **index;
    uint hash;

    acquire(&bcache.lock);

    b = bFindFromHashTable(dev, blockno);

    if (b != 0)
    {
        b->refcnt++;

        if (b->refcnt == 1)
        {
            //从堆中移除b
            int bIndex = b->heapIndex;
            bcache.heap[bIndex] = bcache.heap[bcache.heapSize - 1];
            bcache.heap[bcache.heapSize - 1] = b;
            bcache.heapSize--;
            b->heapIndex = NBUF;
            if ((bIndex - 1) / 2 >= 0 && bcache.heap[bIndex]->timeStamp < bcache.heap[(bIndex - 1) / 2])
                bPercolateUp(bIndex);
            if (2 * (bIndex + 1) < NBUF && bcache.heap[bIndex]->timeStamp > bcache.heap[2 * (bIndex + 1)]->timeStamp)
                bPercolateDown(bIndex);
            if (2 * bIndex + 1 < NBUF && bcache.heap[bIndex]->timeStamp > bcache.heap[2 * bIndex + 1]->timeStamp)
                bPercolateDown(bIndex);
        }

        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
    }

    while (bcache.heapSize > 0)
    {
        //从堆中取出一个空闲buffer，相当于LRU算法
        b = *(bcache.heap);
        bcache.heapSize--;
        *(bcache.heap) = bcache.heap[bcache.heapSize];
        bPercolateDown(0);

        if (b->refcnt == 0)
        {
            //从哈希表中移除b
            if(b->prev == 0)
            {
                hash = (b->dev + b->blockno) % NBUF;
                bcache.hash[hash] = b->next;
            }
            else
                b->prev->next = b->next;

            if(b->next != 0)
                b->next->prev = b->prev;

            //原bget函数中的相关操作
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;

            //把新分配的buffer插入到哈希表的空位
            hash = (b->dev + b->blockno) % NBUF;
            b->next = bcache.hash[hash];
            if(b->next != 0)
                b->next->prev = b;
            b->prev = 0;
            bcache.hash[hash] = b;

            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

/**
 * @brief 改进了brelse。
 *
 * 增加了把buffer加入空闲buffer堆的步骤。
 */
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        //更新timeStamp
        bcache.timeStamp++;
        b->timeStamp = bcache.timeStamp;

        //把b加入堆中
        bcache.heap[bcache.heapSize] = b;
        bcache.heapSize++;
        bPercolateUp(bcache.heapSize - 1);
    }

    release(&bcache.lock);
}
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


