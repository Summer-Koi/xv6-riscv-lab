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
#define NDOUBLE_INDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLE_INDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];  // Data block addresses, double indirect added
  uint32 atime;           // the last time this inode was accessed
  uint32 ctime;           // when the inode was created
  uint32 mtime;           // the last time this inode was modified
  uint32 dtime;           // when the inode was deleted
  uint iflags;            // flag
  uint blank[10];         // for future use
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

