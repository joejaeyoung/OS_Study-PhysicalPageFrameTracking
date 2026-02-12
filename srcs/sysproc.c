#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

#define IPT_BUCKETS 1024 //해시 버킷 개수

/**
 * @brief 하나의 물리 페이지에 매핑된 가상 주소 정보를 담는 구조체
 */
struct vlist {
  uint pid;   //이 가상 주소를 사용하는 프로세스의 PID
  uint va;    //매핑된 가상 주소 (페이지 경계로 정렬)
  ushort flags; //PTE의 권한/상태 플래그 스냅샷
};

/**
 * @brief IPT의 개별 엔트리를 나타내는 구조체
 */
extern struct ipt_entry {
  uint pfn;               //물리 프레임 번호
  uint pid;               //소유 프로세스 PID
  uint va;                //매핑된 가상 주소 (페이지 기준))
  ushort flags;           //PTE 권한 (P/W/U 등 스냅샷))
  ushort refcnt;          //역참조 카운트 (옵션))
  struct ipt_entry *next; //해시 체인
} ipt_entry;

extern struct ipt_entry *ipt_hash[IPT_BUCKETS];
extern struct spinlock ipt_lock;
extern uint ipt_hash_func(uint pfn);
extern int sw_vtop(pde_t *pgdir, const void *va, uint *pa_out, uint *pte_flags_out);
extern int setpageflags_in(pde_t *pgdir, uint addr, uint flags);

/**
 * @brief 구현 C에서 사용할 테스트를 위한 권한 수정 함수 
 * 
 * @param addr : 변경할 주소의 가상주소
 * @param flags : 설정할 플래그 값 기본적으로 PTE_P는 보장한다.
 * 
 * @return 0 변환 성공, -1 페이지 없음 혹은 오류
 */
int sys_setpageflags(void) {
  uint addr;
  uint flags;
  struct proc * curproc = myproc();

  //1. 인자를 받아온다.
  if (argint(0, (int *)&addr) < 0) return -1;
  if (argint(1, (int *)&flags) < 0) return -1;
  if (!curproc) return -1;

  //2. setpageflags 함수를 호출한다.
  return setpageflags_in(curproc->pgdir, addr, flags);
}

/**
 * @brief 주어진 물리 주소 페이지가 매핑된 모든 가상 주소(va)와 소유 프로세스의 PID 목록을
 * IPT에서 검색하여 유저 공간으로 복사합니다.
 * @param pa_page : 검색할 물리 페이지 주소 (유저 공간에서 받은 인자)
 * @param out : 결과를 저장할 유저 공간 버퍼 주소 (struct vlist 배열))
 * @param max : 유저 공간 버퍼에 복사할 최대 엔트리 수
 * @return 복사한 개수
 */
int sys_phys2virt(void) {
  uint pa_page;
  struct vlist *out;
  int max;
  int copied = 0;
  uint pfn, bucket;
  struct ipt_entry *e;
  struct proc *curproc = myproc();

  //1. 인자 받기
  if (argint(0, (int *)&pa_page) < 0) return -1;
  if (argptr(1, (char**)&out, sizeof(struct vlist)) < 0) return -1;
  if (argint(2, &max) < 0) return -1;

  //if (max <= 0 || max >= 100) return -1;

  //2. 프레임 번호 계산
  pa_page = PGROUNDDOWN(pa_page);
  pfn = pa_page / PGSIZE;
  bucket = ipt_hash_func(pfn);

  acquire(&ipt_lock);

  //3. 해시 버킷에서 검색
  for (e = ipt_hash[bucket]; e && copied < max; e = e->next) {
    if (e->pfn == pfn) {
      struct vlist entry;
      entry.pid = e->pid;
      entry.va = e->va;
      entry.flags = e->flags;

      if (copyout(curproc->pgdir,
                  (uint)out + (copied * sizeof(struct vlist)),
                  (char *)&entry,
                  sizeof(struct vlist)) < 0) {
        release(&ipt_lock);
        return -1;
      }
      copied++;
    }
  }
  release(&ipt_lock);
  return copied;
}

/**
 * @brief 주어진 가상 주소에 매핑된 물리 주소와 페이지 테이블 엔트리의 플래그를 찾는다.
 * 
 * @param va : 검색할 가상 주소
 * @param pa_out : 물리 주소 결과를 저장할 유저 공간 주소 포인터
 * @param flags_out : 플래그 결과를 저장할 유저 공간 주소 포인터
 * @return 성공 시 0, 실패시 -1
 */
int sys_vtop(void) {
  uint va;
  uint pa;
  uint flags;
  uint *pa_out;
  uint *flags_out;
  struct proc *curproc = myproc();

  //1. 인자 가상주소 받기
  if (argint(0, (void *)&va) < 0) return -1;

  if (argptr(1, (char **)&pa_out, sizeof(uint)) < 0) return -1;

  if (argptr(2, (char **)&flags_out, sizeof(uint)) < 0) return -1;

  if (!curproc) return -1;

  //2. sw_vtop 호출
  if (sw_vtop(curproc->pgdir, (void *)va, &pa, &flags) < 0) return -1;

  //3. 결과를 유저 공간에 복사
  if (copyout(curproc->pgdir, (uint)pa_out, (char *)&pa, sizeof(uint)) < 0) return -1;
  if (copyout(curproc->pgdir, (uint)flags_out, (char *)&flags, sizeof(uint)) < 0) return -1;

  return 0;
}

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
