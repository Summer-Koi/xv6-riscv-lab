/**
 * @file clm_buf.h
 * @author Chlamydomonos
 * @brief 对buf.h改进后的文件
 */

/**
 * @brief 改进了buf。
 *
 * 增加了用来描述哈希表和堆的字段。
 */
struct buf
{
    int valid; // has data been read from disk?
    int disk;  // does disk "own" buf?
    uint dev;
    uint blockno;
    struct sleeplock lock;
    uint refcnt;

    /**
     * @brief 用于哈希表的链表前一项
     */
    struct buf *prev;

    /**
     * @brief 用于哈希表的链表后一项
     */
    struct buf *next;

    /**
     * @brief 该buffer在堆中的位置。
     *
     * 范围为[0, NBUF)，值为NBUF时代表该buffer不在堆中。
     */
    uint heapIndex;

    /**
     * @brief 该buffer上次访问时的时间戳。
     *
     * @see bcache.timeStamp
     */
    uint timeStamp;

    uchar data[BSIZE];
};
