#include "types.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"

struct vfs_operations {
  int           (*fs_init)(void);
  int           (*mount)(struct inode *, struct inode *);
  int           (*unmount)(struct inode *);
  struct inode* (*getroot)(int, int);
  void          (*readsb)(int dev, struct superblock *sb);
  struct inode* (*ialloc)(uint dev, short type);
  uint          (*balloc)(uint dev);
  void          (*bzero)(int dev, int bno);
  void          (*bfree)(int dev, uint b);
  void          (*brelse)(struct buf *b);
  void          (*bwrite)(struct buf *b);
  struct buf*   (*bread)(uint dev, uint blockno);
  int           (*namecmp)(const char *s, const char *t);
};

`

filedup(f);
fileread(f, p, n);
filewrite(f, p, n);
fileclose(f);
filestat(f, st);

begin_op();
end_op();

iunlockput(ip);
iupdate(ip);
iunlock(ip);
ilock(dp);
ialloc(dp->dev, type);
readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de);
writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de);

dirlink(dp, name, ip->inum);
nameiparent(new, name);
namecmp(name, "..");
namei(path);
isdirempty(ip)