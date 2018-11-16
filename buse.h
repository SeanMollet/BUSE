#ifndef BUSE_H_INCLUDED
#define BUSE_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif

  /* Most of this file was copied from nbd.h in the nbd distribution. */
#include <stdint.h>
#include <sys/types.h>
#include <linux/nbd.h>

  struct buse_operations
  {
    int (*read)(void *buf, uint32_t len, uint64_t offset, void *userdata);
    int (*write)(const void *buf, uint32_t len, uint64_t offset, void *userdata);
    void (*disc)(void *userdata);
    int (*flush)(void *userdata);
    int (*trim)(uint64_t from, uint32_t len, void *userdata);

    // either set size, OR set both blksize and size_blocks
    uint64_t size;
    uint32_t blksize;
    uint64_t size_blocks;
  };

  int buse_main(const char *dev_file, const struct buse_operations *bop, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* BUSE_H_INCLUDED */
