/************************************* fid table entry */

#ifdef _POSIX_SOURCE
#ifndef MAXNAMLEN
#define MAXNAMLEN  255
#endif  /* MAXNAMLEN */
#endif  /* _POSIX_SOURCE */

#ifndef V_BLKSIZE  
#define V_BLKSIZE  8192
#endif  /* V_BLKSIZE */

typedef struct fid_ent_s {
    ViceFid           fid;
    enum coda_vtype        type;
    ds_list_t        *kids;
    struct fid_ent_s *parent;
    char              name[MAXNAMLEN+1];
} fid_ent_t;

#if defined(__BSD44__) && defined(__i386__)
#define SYS_STRING  "i386_nbsd1"
#endif

#ifdef __STDC__
#define assert(b)                                           \
do {                                                        \
    if (!(b)) {                                             \
	fprintf(stderr,"assert(%s) -- line %d, file %s\n",  \
                #b, __LINE__, __FILE__);                    \
	zombify();                                          \
    }                                                       \
} while (0)
#else /* __STDC__ */
#define assert(b)                                              \
do {                                                           \
    if (!b) {                                                  \
	fprintf(stderr,"assertion failed line %d, file %s\n",  \
		__LINE__, __FILE__);                           \
	zombify();                                             \
    }                                                          \
} while (0)
#endif

#ifdef LINUX
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512	/* Not a change, but a change it */
#define ATTR_ATTR_FLAG	1024
#define MS_MGC_VAL 0xC0ED0000	/* magic flag number to indicate "new" flags */
#define umode_t int
struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	off_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
	unsigned int	ia_attr_flags;
};

static void coda_iattr_to_vattr(struct iattr *, struct coda_vattr *);


#define sigcontext sigaction
#define MOUNT_CFS 0
#define d_namlen d_reclen
#define SYS_STRING "linux"
#define ts_sec tv_sec
#define ts_nsec tv_nsec
#else
#define MOUNT_CFS 1
#endif




