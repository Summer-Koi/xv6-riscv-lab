// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
  uint reserved[20];
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define DOUBLE_INDIRECT (NINDIRECT * NINDIRECT)
#define TRIPLE_INDIRECT (DOUBLE_INDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + DOUBLE_INDIRECT + TRIPLE_INDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+3];  // Data block addresses, double(triple) indirect added
  uint32 atime;           // the last time this inode was accessed
  uint32 ctime;           // when the inode was created
  uint32 mtime;           // the last time this inode was modified
  uint32 dtime;           // when the inode was deleted
  uint iflags;            // flag
  uint32 generation;      // indicate the file version
  uint32 gid;             // group id
  uint osd_2[7];          // OS dependant structure in EXT, be blank here
};

// iflags values
#define SECRM_FL        0x00000001    // secure deletion
#define UNRM_FL         0x00000002    // record for undelete
#define COMPR_FL        0x00000004    // compressed file
#define SYNC_FL         0x00000008    // synchronous updates
#define IMMUTABLE_FL    0x00000010    // Immutable File
#define APPEND_FL       0x00000020    // Append Only
#define NODUMP_FL       0x00000040    // Do No Dump/Delete
#define NOATIME_FL      0x00000080    // Do Not Update .i_atime

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

// Variable-Length-of-Name Directory
// 可变长的顺序检索目录

#define NAME_MAX_LEN 256
// name 长度应该在 name_len 可以表示的范围中，定为 256*char

struct dirent_vn
{
  uint32 inum;      //4 bytes
  uint16 rec_len;   //2 bytes
  uint8 name_len;    //1 byte
  uint8 file_type;  //1 byte
};

// Indexed Directory
// hash 索引检索目录
// 
// 每一个区中的结构
// [ meta_dx | dirent_dx | ... | dirent_dx ]
struct meta_dx
{
  uint8 count;    // 1 byte
  uint32 max;     // 4 bytes
  uint32 min;     // 4 bytes
};

struct dirent_dx
{
  uint32 inum;    // 4 bytes
  uint32 hash;    // 4 bytes
  char name_pre[4]; // 4 bytes
};

#define HASH_SIZE_PER_MT 256