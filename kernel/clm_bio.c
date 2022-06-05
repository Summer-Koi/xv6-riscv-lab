/**
 * @file clm_bio.c
 * @author Chlamydomonos
 * @brief 对bio.c改进后的文件
 *
 * 目前，bio.c中的函数被命名为clm_xxx的格式。最后，此文件的函数将会覆盖bio.c。
 */

#include "types.h"
#include "clm_param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "clm_buf.h"

/**
 * @brief 改进了bcache。
 *
 * 增加了哈希表。
 */
struct
{
    struct spinlock lock;
    struct clm_buf buf[NBUF];
    struct clm_buf *bufHashTable[NBUF];
    uint bufSize;
    struct clm_buf head;
} clm_bcache;

/**
 * @brief 改进了binit。
 *
 * 增加了哈希表的初始化。
 */
void clm_binit()
{
    struct clm_buf *b;

    initlock(&clm_bcache.lock, "bcache");
    clm_bcache.bufSize = 0;

    for (b = clm_bcache.buf; b < clm_bcache.buf + NBUF; b++)
    {
        initsleeplock(&b->lock, "buffer");
    }

    clm_bcache.head.prev = &clm_bcache.head;
    clm_bcache.head.next = &clm_bcache.head;

    for (int i = 0; i < NBUF; i++)
    {
        clm_bcache.bufHashTable[i] = clm_bcache.buf + i;
        b->next = clm_bcache.head.next;
        b->prev = &clm_bcache.head;
        clm_bcache.head.next->prev = b;
        clm_bcache.head.next = b;
    }
}

/**
 * @brief 从哈希表中获取buffer。
 * @return 如果buffer在哈希表中，返回该buffer；
 * 如果哈希表中没有该buffer，返回空指针。
 */
static struct clm_buf *clm_bGetFromHashTable(uint dev, uint blockno)
{
    uint targetHash = (dev + blockno) % NBUF;

    struct clm_buf *out = clm_bcache.bufHashTable[targetHash];

    if (out->dev == dev && out->blockno == blockno)
        return out;

    for (uint i = 0; i < NBUF / 2 + 1; i++)
    {
        uint positiveIndex = (targetHash + i * i) % NBUF;
        uint negativeIndex = (targetHash - i * i) % NBUF;

        out = clm_bcache.bufHashTable[positiveIndex];

        if (out->dev == dev && out->blockno == blockno)
            return out;

        if (out->hash == NBUF)
            return 0;

        out = clm_bcache.bufHashTable[negativeIndex];

        if (out->dev == dev && out->blockno == blockno)
            return out;

        if (out->hash == NBUF)
            return 0;
    }

    return 0;
}

/**
 * @brief 向哈希表中添加一个新的buffer。
 * @return 如果添加成功，返回新添加的buffer；
 * 如果哈希表已满，返回空指针。
 */
static struct clm_buf *clm_bAddNewToHashTable(uint dev, uint blockno)
{
    uint targetHash = (dev + blockno) % NBUF;

    struct clm_buf *out = clm_bcache.bufHashTable[targetHash];
    if (!out->inTable)
    {
        out->hash = targetHash;
        out->inTable = 1;
        out->dev = dev;
        out->blockno = blockno;
        clm_bcache.bufSize++;
        return out;
    }

    for (int i = 0; i < NBUF / 2 + 1; i++)
    {
        uint positiveIndex = (targetHash + i * i) % NBUF;
        uint negativeIndex = (targetHash - i * i) % NBUF;

        out = clm_bcache.bufHashTable[positiveIndex];

        if (!out->inTable)
        {
            out->hash = targetHash;
            out->inTable = 1;
            out->dev = dev;
            out->blockno = blockno;
            clm_bcache.bufSize++;
            return out;
        }

        out = clm_bcache.bufHashTable[negativeIndex];

        if (!out->inTable)
        {
            out->hash = targetHash;
            out->inTable = 1;
            out->dev = dev;
            out->blockno = blockno;
            clm_bcache.bufSize++;
            return out;
        }
    }

    return 0;
}

static struct clm_buf *clm_bget(uint dev, uint blockno)
{
    struct buf *b;

    acquire(&bcache.lock);

    //如果b已经在哈希表中，返回b
    b = clm_bGetFromHashTable(dev, blockno);
    if (b != 0)
    {
        b->refcnt++;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
    {
        if (b->refcnt == 0)
        {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    panic("bget: no buffers");
}