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

    /**
     * @brief 保存buffer指针的哈希表。
     */
    struct clm_buf *hash[NBUF];
    uint hashSize;
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
    clm_bcache.hashSize = 0;

    for (b = clm_bcache.buf; b < clm_bcache.buf + NBUF; b++)
    {
        b->next = clm_bcache.head.next;
        b->prev = &clm_bcache.head;
        initsleeplock(&b->lock, "buffer");

        // 初始状态，所有buffer均不在哈希表中
        b->index = NBUF;

        clm_bcache.head.next->prev = b;
        clm_bcache.head.next = b;
    }

    clm_bcache.head.prev = &clm_bcache.head;
    clm_bcache.head.next = &clm_bcache.head;

    for (struct clm_buf **i = clm_bcache.hash; i < clm_bcache.hash + NBUF; i++)
    {
        *i = 0;
    }
}

/**
 * @brief 使用双向平方探测法从哈希表中寻找指定的buffer。
 * @return 如果找到，返回该buffer的二级地址；
 * 如果没找到但找到了从未被分配过的空位，返回空位的地址；
 * 否则，返回空指针。
 */
static struct clm_buf **clm_bFindFromHashTable(uint dev, uint blockno)
{
    if (clm_bcache.hashSize == NBUF)
        return 0;

    uint hash = (dev + blockno) % NBUF;

    struct clm_buf **out = clm_bcache.hash + hash;
    if ((*out)->dev == dev && (*out)->blockno == blockno && (*out)->index != NBUF)
        return out;

    for (uint i = 1; i <= NBUF / 2 + 1; i++)
    {
        uint positiveIndex = (hash + i * i) % NBUF;
        uint negativeIndex = (hash - i * i) % NBUF;

        out = clm_bcache.hash + positiveIndex;

        if (((*out)->dev == dev && (*out)->blockno == blockno && (*out)->index != NBUF) || *out == 0)
            return out;

        out = clm_bcache.hash - positiveIndex;

        if (((*out)->dev == dev && (*out)->blockno == blockno && (*out)->index != NBUF) || *out == 0)
            return out;
    }

    return 0;
}

/**
 * @brief 改进了bget。
 *
 * 获取已缓存buffer的过程改为由哈希表获取；
 * 分配新buffer后将其插入哈希表。
 */
static struct clm_buf *clm_bget(uint dev, uint blockno)
{
    struct clm_buf *b;
    struct clm_buf **index;

    acquire(&clm_bcache.lock);

    index = clm_bFindFromHashTable(dev, blockno);

    if (index != 0) //说明找到了buffer或空位
    {
        b = *index;

        if (b != 0) //说明找到了buffer
        {
            b->refcnt++;
            release(&clm_bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }

        // Not cached.
        // Recycle the least recently used (LRU) unused buffer.
        for (b = clm_bcache.head.prev; b != &clm_bcache.head; b = b->prev)
        {
            if (b->refcnt == 0)
            {
                b->dev = dev;
                b->blockno = blockno;
                b->valid = 0;
                b->refcnt = 1;
                release(&clm_bcache.lock);
                acquiresleep(&b->lock);

                //把新分配的buffer插入到哈希表的空位
                *index = b;
                b->index = index - clm_bcache.hash;
                clm_bcache.hashSize++;

                return b;
            }
        }
    }
    panic("bget: no buffers");
}

/**
 * @brief 改进了brelse。
 * 增加了从哈希表中移除buffer的步骤。
 */
void clm_brelse(struct clm_buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    acquire(&clm_bcache.lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = clm_bcache.head.next;
        b->prev = &clm_bcache.head;
        clm_bcache.head.next->prev = b;
        clm_bcache.head.next = b;

        // b从哈希表中移除
        b->index = NBUF;
        clm_bcache.hashSize--;
        if (clm_bcache.hashSize == 0)
            for (struct clm_buf **i = clm_bcache.hash; i < clm_bcache.hash + NBUF; i++)
                *i = 0;
    }

    release(&clm_bcache.lock);
}
