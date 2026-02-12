// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

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

#define PFNNUM 60000

/**
 * @brief 전역 프레임 정보 테이블
 */
struct physframe_info pf_table[PFNNUM];


struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);

  uint pa;
  uint frame_idx;
  //1. 가상 주소 -> 물리주소
  pa = V2P(v);

  //2. 물리 주소 -> 프레임 번호
  frame_idx = pa / PGSIZE;

  //3. 범위 체크
  if (frame_idx >= PFNNUM)
    panic("kfree: frame index out of bounds");
  
  //4. 전역 테이블 초기화
  pf_table[frame_idx].allocated = 0;
  pf_table[frame_idx].pid = -1;
  pf_table[frame_idx].start_tick = 0;

  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  
  if(r) {
    kmem.freelist = r->next;
    struct proc *p;

    p = 0;
    if (kmem.use_lock && tracing_initialized)
      p = myproc();

    //유저 프로세스가 할당하는 경우만 추적
    if (p && p->pid > 0) {
      uint pa;
      uint frame_idx;

      //1. 가상 주소 -> 물리 주소
      pa = V2P((char *)r);

      //2. 물리 주소 -> 프레임 번호
      frame_idx = pa / PGSIZE;

      //3. 범위 체크
      if (frame_idx >= PFNNUM)
        panic("kalloc: frame index out of bounds");

      //4. 전역 테이블 업데이트
      pf_table[frame_idx].frame_index = frame_idx;
      pf_table[frame_idx].allocated = 1;
      acquire(&tickslock);
      pf_table[frame_idx].start_tick = ticks;
      release(&tickslock);
      pf_table[frame_idx].pid = p->pid;
      // cprintf("kalloc : frame_idx %d is allocated\n", frame_idx);
    }
  
  }

  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}


/**
 * @brief 커널 영역의 전역 프레임 정보를 사용자 공간으로 추가하기 위한 시스템 콜
 * 
 * @param addr 사용자 제공 버퍼
 * @param max_entries 프레임 사용 정보를 반납할 개수
 * @return 총 복사된 엔트리 개수를 리턴한다.
 */
int sys_dump_physmem_info() {
  char *addr;
  int max_entries;
  int copied;
  struct proc *curproc;

  //0. 인자 불러오기, 사용 변수 초기화 실패시 -1 반환
  if (argint(1, &max_entries) < 0) return -1;
  if (argptr(0, &addr, sizeof(struct physframe_info)) < 0) return -1;

  copied = 0;
  curproc = myproc();
  if (!curproc) return -1;

  //1. 유효성 검사
  if (max_entries <= 0 || max_entries > PFNNUM)
    return -1;

  //2. 락 획득
  acquire(&kmem.lock);

  //3. 유저 영역으로 복사
  for (int i = 0; i < PFNNUM && copied < max_entries; i++) {
    if (copyout(curproc->pgdir,
                (uint)addr + (copied * sizeof(struct physframe_info)),
                (char *)&pf_table[i],
                sizeof(struct physframe_info)) < 0) {
      release(&kmem.lock);
      return -1;
    }
    copied++;
  }
  release(&kmem.lock);

  //4. 복사된 개수 반환
  return copied;
}