/**
 * @file clm_buf.h
 * @author Chlamydomonos
 * @brief 对buf.h改进后的文件
 */

/**
 * @brief 改进了buf。
 *
 * 增加了用来描述哈希表的字段。
 */
struct clm_buf
{
    int valid; // has data been read from disk?
    int disk;  // does disk "own" buf?
    uint dev;
    uint blockno;
    struct sleeplock lock;
    uint refcnt;
    struct clm_buf *prev; // LRU cache list
    struct clm_buf *next;

    /**
     * @brief 该buffer在哈希表中的实际位置。
     *
     * 范围为[0, NBUF)，值为NBUF时代表该buffer不在哈希表中。
     */
    uint index;
    uchar data[BSIZE];
};
