// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  if(logstate_get() != 0)
  log_write(bp);
  else 
  bwrite(bp);
  //bwrite(bp);//log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        if(logstate_get() != 0)
        log_write(bp);
        else 
        bwrite(bp);//log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  if(logstate_get() != 0)
    log_write(bp);
  else 
    bwrite(bp);//log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      dip->atime = ticks;
      dip->ctime = ticks;
      dip->mtime = ticks;
      dip->dtime = 0;
  
      if(logstate_get() != 0)
        log_write(bp);
      else 
        bwrite(bp);//log_write(bp);   // mark it allocated on the disk

      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;

  dip->atime = ip->atime;
  dip->ctime = ip->ctime;
  dip->mtime = ip->mtime;
  dip->dtime = ip->dtime;

  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  if(logstate_get() != 0)
  log_write(bp);
  else 
  bwrite(bp);//log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;

    ip->atime = dip->atime;
    ip->ctime = dip->ctime;
    ip->mtime = dip->mtime;
    ip->dtime = dip->dtime;

    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Set 4 kinds of time in an inode
// 
// Param: ip  inode
//        at  access time
//        ct  create time
//        mt  modify time
//        dt  delete time

void
itimeset(struct inode *ip, uint at, uint ct, uint mt, uint dt)
{
  if(!holdingsleep(&ip->lock))
    panic("inode timeset");
  ip->atime = at;
  ip->ctime = ct;
  ip->mtime = mt;
  ip->dtime = dt;
}


// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.


// Indirect-Path
//
// 给定深度，通过间接寻址寻找目标块，二重间接寻址深度为2
// 

static uint
indirect_path(struct inode *ip, struct buf *bl, int depth, uint bn) // bn 应为残值
{
  uint addr;
  uint *a;
  a = (uint*)bl->data;

  if (depth == 1) // 表明该层中可以直接读到目标数据块的位置
  {
    if (bn >= NINDIRECT)
    {
      printf("depth = %d, bn = %d\n", depth, bn);
      panic("indirect path overflow");
    }
    addr = a[bn];
    if (addr == 0)
    {
      a[bn] = addr = balloc(ip->dev);
      log_write(bl);
    }
    brelse(bl);
    return addr;
  }
  else  // 表明该层仍然是间接层
  {
    // 先解码bn, 分成高位和低位，高位用于片选
    uint bn_high, bn_low;
    if (depth == 2)
    {
      bn_high = bn / NINDIRECT;
      bn_low = bn % NINDIRECT;
    }
    else
    {
      bn_high = bn / (NINDIRECT * NINDIRECT);
      bn_low = bn % (NINDIRECT * NINDIRECT);
    }
    // 判断片选位是否初始化过
    if (a[bn_high] == 0)
    {
      a[bn_high] = balloc(ip->dev);
      log_write(bl);
    }
    struct buf *nextbl = bread(ip->dev, a[bn_high]);
    addr = indirect_path(ip, nextbl, depth - 1, bn_low);
    brelse(bl);
    return addr;
  }
}

static uint
bmap(struct inode *ip, uint bn)
{
  uint addr;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  /*
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      if(logstate_get() != 0)
      log_write(bp);
      else 
      bwrite(bp);//log_write(bp);
    }
    brelse(bp);
    return addr;
  }
*/
  bn -= NDIRECT;
  if(bn < NINDIRECT)
  {
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    return indirect_path(ip, bp, 1, bn);
  }
  bn -= NINDIRECT;
  if(bn < DOUBLE_INDIRECT)
  {
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    return indirect_path(ip, bp, 2, bn);
  }
  bn -= DOUBLE_INDIRECT;
  if(bn < TRIPLE_INDIRECT)
  {
    if((addr = ip->addrs[NDIRECT + 2]) == 0)
      ip->addrs[NDIRECT + 2] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    return indirect_path(ip, bp, 3, bn);
  }
  
  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  ip->atime = ticks;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  ip->atime = ticks;
  ip->mtime = ticks;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    if(logstate_get() == 1)
    log_write(bp);
    else 
    bwrite(bp);//log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}


// Revised Directory Layer
// 
// 重写了 dirlookup 和 dirlink
// 使用了新的目录结构体 dirent_vn，支持可变长度的目录名
// 接口和原始函数保持一致，但需要增加 remove 接口
// for root dir, just use original

int
namecmp_vn(const char *s, const char *t, uint8 len)
{
  return strncmp(s, t, len);
}

struct inode*
dirlookup_vn(struct inode *dp, char *name, uint8 n_len, uint *poff, uint *lastpoff)// lastpoff should set 0
{
  struct dirent_vn de;

  if (dp->type == T_DIR)
    return dirlookup(dp, name, poff);
  
  if (dp->type != T_VNDIR)
  {
    panic("dirlookup_vn not DIR");
  }

  //printf("dirlookup---dp=%d, size=%d,n_len=%d\n", dp->inum, dp->size, n_len);
  uint32 off = 0;
  while (off < dp->size)
  {
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
      panic("dirlookup_vn readi");
    }
    
    if(de.inum == 0){
      off += de.rec_len;
      continue;
    }

    char namebuf[NAME_MAX_LEN];
    memset(namebuf, 0, sizeof(namebuf));
    if(readi(dp, 0, (uint64)&namebuf, off + sizeof(de), de.name_len) != de.name_len){
      panic("dirlookup_vn readi name");
    }
    //printf("off=%d,len=%d\n",off,de.name_len);
    //printf("%s___%s\n", namebuf, name);
    if(namecmp_vn(namebuf, name, n_len) == 0){
      //printf("bingo\n");
      if(poff)
      {
        *poff = off;
      }

      return(iget(dp->dev, de.inum));
    }
    if(lastpoff)
    {
      *lastpoff = off;
    }
    off += de.rec_len;
  }
  //printf("ALIVE!!---n_len:%d\n", n_len);
  return 0;
}

int
dirlink_vn(struct inode *dp, char *name, uint8 n_len, uint inum)
{

  if (dp->type == T_DIR){
    return dirlink(dp, name, inum);
  }

  struct dirent_vn de;
  struct inode *ip;

  // check
  if((ip = dirlookup_vn(dp, name, n_len, 0, 0)) != 0){
    iput(ip);
    return -1;
  }
  // look for new
  uint32 off = 0;
  uint32 available_padding = 0;

  while (off < dp->size)
  {
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
      panic("dirlink_vn readi");
    }

    if(de.inum == 0)
      break;
    available_padding = de.rec_len - sizeof(de) - de.name_len;
    if(available_padding >= n_len)
      break;

    off += de.rec_len;
  }

  char namebuf[NAME_MAX_LEN];
  memset(namebuf, 0, sizeof(namebuf));
  memmove(namebuf, name, n_len);
  if(off >= dp->size)
  // new
  {
    de.inum = inum;
    de.name_len = n_len;
    de.rec_len = sizeof(de) + de.name_len;
    //printf("dirlink,new---n_len=%d, rec_len=%d, off=%d, name=%s\n",de.name_len,de.rec_len, off, name);
    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink_vn write new");
    if(writei(dp, 0, (uint64)&namebuf, off + sizeof(de), n_len) != n_len)
      panic("dirlink_vn write new name");
  }
  else
  // pad
  {
    struct dirent_vn newde;
    newde.inum = inum;
    newde.name_len = n_len;
    newde.rec_len = available_padding;
    de.rec_len -= available_padding;
    //printf("dirlink,pad---------\n newde:inum:%d,n_len:%d,rec_len:%d\n",newde.inum, newde.name_len, newde.rec_len);

    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink_vn write pad old");
    if(writei(dp, 0, (uint64)&newde, off + de.rec_len, sizeof(newde)) != sizeof(newde))
      panic("dirlink_vn write pad");
    if(writei(dp, 0, (uint64)&namebuf, off + de.rec_len, n_len) != n_len)
      panic("dirlink_vn write pad name");
  }
  return 0;
}

int
rmdir_vn(struct inode* dp, uint off, uint lastoff)
{
  struct dirent_vn de;
  struct dirent_vn lastde;
  if(dp->type == T_DIR)
  {
    memset(&de, 0, sizeof(de));
    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("unlink: writei");
    return 0;
  }
  
  if(readi(dp, 0, (uint64)&de, off, sizeof(de))!= sizeof(de))
    panic("1111");
  if(readi(dp, 0, (uint64)&lastde, lastoff, sizeof(de))!= sizeof(de))
    panic("2222");

  uint rec_len = de.rec_len;
  lastde.rec_len += rec_len;
  if(writei(dp, 0, (uint64)&lastde, lastoff, sizeof(lastde))!= sizeof(lastde))
    panic("33333");

  return 0;

}


// Indexed Directory Layer
// 
// 重写了 dirlookup 和 dirlink
// 使用了新的目录结构体 dirent_dx
// 接口和原始函数保持一致，但需要增加 remove 接口，上一部分同理
//



/*
struct inode*
dirlookup_dx(struct inode *dp, char *name, uint8 n_len, uint *poff)
{
  uint inum;
  struct meta_dx meta;
  struct dirent_dx de;

  if(dp->type != T_DIR)
    panic("dirlookup_dx not DIR");

  uint32 hashcode = murmur3_32(name, n_len, 14);
  uint32 blockn = 1;
  while (1)
  {
    
  }
  

  return 0;
}
*/



// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

// revised path

int 
get_name_len(char* path)
{
  char *s;
  int len;
  while(1)
  {
    while(*path == '/')
      path++;
    if(*path == 0)
      return 0;
    s = path;
    while(*path != '/' && *path != 0)
      path++;
    len = path - s;
    if(*path == 0)
    {
      return len;
    }
  }
}

static char*
skipelem_vn(char *path, char *name, uint8 *n_len)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  *n_len = len;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

static struct inode*
namex_vn(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;
  uint8 n_len;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);
  while((path = skipelem_vn(path, name, &n_len)) != 0){
    //printf("ip=%d, path=%s,name=%s, n_len=%d\n",ip->inum, path, name, n_len);
    ilock(ip);
    if(ip->type != T_DIR && ip->type != T_VNDIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    
    if((next = dirlookup_vn(ip, name, n_len, 0, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    //printf("qwqwq\n");
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei_vn(char *path)
{
  char name[DIRSIZ];
  return namex_vn(path, 0, name);
}

struct inode*
nameiparent_vn(char *path, char *name)
{
  return namex_vn(path, 1, name);
}