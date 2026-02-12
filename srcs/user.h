struct stat;
struct rtcdate;

/**
 * @struct physframe_info
 * @brief 물리 메모리와 프레임에 대한 정보를 저장하는 구조체
 */
struct physframe_info {
	uint frame_index; // 물리 프레임 번호
	int allocated;    // 1이면 할당, 0이면 free
	int pid;          // 소유 프로세스 PID, 없으면 -1
	uint start_tick;  // 현재 PID가 사용 시작한 tick
};

/**
 * @brief 하나의 물리 페이지에 매핑된 가상 주소 정보를 담는 구조체
 */
struct vlist {
	uint pid;
	uint va;
	ushort flags;
};

#define PFNNUM 60000

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
//A에서 구현한 시스템 콜
int dump_physmem_info(void *addr, int max_entries);
//C에서 구현한 시스템 콜
int vtop(void *va, uint *pa_out, uint *flags_out);
int phys2virt(uint pa_page, struct vlist *out, int max);
int setpageflags(uint addr, int flags);
int print_ipt_status(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
