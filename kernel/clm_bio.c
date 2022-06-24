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
 * 增加了哈希表；
 * 移除了链表；
 * 增加了堆。
 */
struct
{
    struct spinlock lock;
    struct clm_buf buf[NBUF];

    /**
     * @brief 保存buffer指针的哈希表。
     */
    struct clm_buf *hash[NBUF];

    /**
     * @brief 保存buffer指针的堆。
     */
    struct clm_buf *heap[NBUF];

    /**
     * @brief 目前正在使用的buffer数量。
     */
    uint bufSize;

    /**
     * @brief 用于LRU算法的时间戳。
     *
     * 每次访问buffer，该时间戳+1。
     */
    uint timeStamp;
    struct clm_buf head;
} clm_bcache;

/**
 * @brief 改进了binit。
 *
 * 增加了哈希表的初始化；
 * 移除了链表的初始化；
 * 增加了堆的初始化。
 */
void clm_binit()
{
    struct clm_buf *b;

    initlock(&clm_bcache.lock, "bcache");
    clm_bcache.bufSize = 0;

    for (int i = 0; i < NBUF; i++)
    {
        b = clm_bcache.buf + i;
        initsleeplock(&b->lock, "buffer");

        // 初始状态，所有buffer均不在哈希表和堆中
        b->hashIndex = NBUF;
        b->heapIndex = NBUF;

        clm_bcache.hash[i] = 0;
        clm_bcache.heap[i] = 0;
    }
}

/**
 * @brief 使用双向平方探测法从哈希表中寻找指定的buffer。
 * @return 如果找到，返回该buffer在哈希表中的地址；
 * 如果没找到但找到了从未被分配过的空位，返回空位的地址；
 * 否则，返回空指针。
 */
static struct clm_buf **clm_bFindFromHashTable(uint dev, uint blockno)
{
    if (clm_bcache.bufSize == NBUF)
        return 0;

    uint hash = (dev + blockno) % NBUF;

    struct clm_buf **out = clm_bcache.hash + hash;

    if ((*out)->dev == dev && (*out)->blockno == blockno)
        return out;

    for (uint i = 1; i <= NBUF / 2 + 1; i++)
    {
        uint positiveIndex = (hash + i * i) % NBUF;
        uint negativeIndex = (hash - i * i) % NBUF;

        out = clm_bcache.hash + positiveIndex;

        if (((*out)->dev == dev && (*out)->blockno == blockno) || *out == 0)
            return out;

        out = clm_bcache.hash - positiveIndex;

        if (((*out)->dev == dev && (*out)->blockno == blockno) || *out == 0)
            return out;
    }

    return 0;
}

/**
 * @brief 从指定位置开始上滤
 */
static void clm_bPercolateUp(int index)
{
    struct clm_buf *value = clm_bcache.heap[index];
    int parentIndex = (index - 1) / 2;

    while (index >= 0 && clm_bcache.heap[parentIndex]->timeStamp > value->timeStamp)
    {
        clm_bcache.heap[index] = clm_bcache.heap[parentIndex];
        clm_bcache.heap[index]->heapIndex = index;
        index = parentIndex;
        parentIndex = (index - 1) / 2;
    }

    clm_bcache.heap[index] = value;
    value->heapIndex = index;
}

/**
 * @brief 从指定位置开始下滤
 */
static void clm_bPercolateDown(int index)
{
    int maxChild = 2 * (index + 1);
    struct clm_buf *value = clm_bcache.heap[index];
    char goDown = 1;
    while (goDown && maxChild < clm_bcache.bufSize)
    {
        goDown = 0;
        if (clm_bcache.heap[maxChild]->timeStamp < clm_bcache.heap[maxChild - 1]->timeStamp)
            --maxChild;
        if (value->timeStamp < clm_bcache.heap[maxChild]->timeStamp)
        {
            goDown = 1;
            clm_bcache.heap[index] = clm_bcache.heap[maxChild];
            clm_bcache.heap[index]->heapIndex = index;
            index = maxChild;
            maxChild = 2 * (maxChild + 1);
        }
    }
    if (maxChild == clm_bcache.bufSize)
    {
        if (value->timeStamp < clm_bcache.heap[maxChild - 1]->timeStamp)
        {
            clm_bcache.heap[index] = clm_bcache.heap[maxChild - 1];
            clm_bcache.heap[index]->heapIndex = index;
            index = maxChild - 1;
        }
    }
    clm_bcache.heap[index] = value;
    value->heapIndex = index;
}

/**
 * @brief 改进了bget。
 *
 * 获取已缓存buffer的过程改为由哈希表获取；
 * 分配新buffer后将其插入哈希表。
 *
 * 获取到已缓存的buffer后，将其从堆中移除。
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

            //从堆中移除b
            int bIndex = b->heapIndex;
            clm_bcache.heap[bIndex] = clm_bcache.heap[clm_bcache.bufSize - 1];
            clm_bcache.heap[clm_bcache.bufSize - 1] = b;
            clm_bcache.bufSize--;
            b->heapIndex = NBUF;
            if (clm_bcache.heap[bIndex]->timeStamp < clm_bcache.heap[(bIndex - 1) / 2])
                clm_bPercolateUp(bIndex);
            if (clm_bcache.heap[bIndex]->timeStamp > clm_bcache.heap[2 * (bIndex + 1)]->timeStamp)
                clm_bPercolateDown(bIndex);
            if (clm_bcache.heap[bIndex]->timeStamp > clm_bcache.heap[2 * bIndex + 1]->timeStamp)
                clm_bPercolateDown(bIndex);

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
                b->hashIndex = index - clm_bcache.hash;
                clm_bcache.bufSize++;

                return b;
            }
        }
    }
    panic("bget: no buffers");
}

/**
 * @brief 改进了brelse。
 *
 * 增加了把buffer加入空闲buffer堆的步骤。
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
        //更新timeStamp
        clm_bcache.timeStamp++;
        b->timeStamp = clm_bcache.timeStamp;

        //把b加入堆中
        clm_bcache.heap[clm_bcache.bufSize] = b;
        clm_bcache.bufSize++;
        clm_bPercolateUp(clm_bcache.bufSize - 1);
    }

    release(&clm_bcache.lock);
}
