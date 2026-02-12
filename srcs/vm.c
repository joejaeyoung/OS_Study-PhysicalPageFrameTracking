#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

/**
 * @struct TLB에 사용하는 엔트리 구조체
 * @brief  TLB에 사용되는 엔트리 구조체이며 캐싱을 구현하기 위한 데이터를 담고 있다.
 */
struct sw_tlb_entry {
  uint pid;     //프로세스 id
  uint va_page; //가상 페이지 번호 (va >> 12)
  uint pa_page; //물리 페이지 번호 (pa >> 12)
  uint flags;   //PTE 플래그
  int valid;    //유효 비트
};

#define SW_TLB_SIZE 64 //TLB 캐시 크기

/**
 * @struct Direct-Mapped TLB 캐시를 구현한 구조체
 */
struct sw_tlb{
  struct sw_tlb_entry entries[SW_TLB_SIZE]; //캐시 set
  uint hits;                                //히트 카운트
  uint misses;                              //미스 카운트
  struct spinlock lock;                 //TLB 락
} sw_tlb;

/**
 * @struct ipt_entry
 * @brief IPT의 각 엔트리를 정의한다.
 */
struct ipt_entry {
  uint pfn;               //물리 프레임 번호
  uint pid;               //소유 프로세스 PID
  uint va;                //매핑된 가상 주소 (페이지 기준))
  ushort flags;           //PTE 권한 (P/W/U 등 스냅샷))
  ushort refcnt;          //역참조 카운트 (옵션))
  struct ipt_entry *next; //해시 체인
};

#define IPT_BUCKETS 1024 //해시 버킷 개수

struct ipt_entry *ipt_hash[IPT_BUCKETS];
struct spinlock ipt_lock;
uint ipt_lock_count = 0; //락 획득 횟수
uint ipt_operations = 0; //총 연산 횟수


/**
 * @brief spin lock 카운터를 유저 영역으로 넘겨주는 함수, test_c 코드에서만 수행되고 디버깅 용으로 출력된다.
 */
int sys_print_ipt_status(void) {
  cprintf("IPT Status : locks = %d ops = %d\n", ipt_lock_count, ipt_operations);
  return 0;
}


/**
 * @brief TLB 캐시를 초기화 하는 함수
 */
void sw_tlb_init(void) {
  int i;
  //1. 락 초기화
  initlock(&sw_tlb.lock, "sw_tlb");

  //2. 캐시 초기화
  for(i = 0; i < SW_TLB_SIZE; i++) {
    sw_tlb.entries[i].valid = 0;
  }

  sw_tlb.hits = 0;
  sw_tlb.misses = 0;
}

/**
 * @brief TLB 캐시에서 인덱싱을 하기 위한 해시 함수
 * 
 * @param pid     PID 값
 * @param va_page va_page 값
 * 
 * @return pid, va_page를 사용한 해시 값
 */
static uint sw_tlb_hash(uint pid, uint va_page) {
  return ((pid^va_page) % SW_TLB_SIZE);
}

/**
 * @brief TLB 캐시 조회
 * 
 * @param pid     캐시 조회에 사용할 PID
 * @param va_page 캐시 조회에 사용할 va_page
 * @param pa_out  캐시 hit시 반환할 pa_out
 * @param flags_out 캐시 hit시 반환할 flags_out
 * 
 * @return Hit 시 1, Miss 시 0 반환
 */
static int sw_tlb_lookup(uint pid, uint va_page, uint *pa_out, uint *flags_out) {
  uint index;
  struct sw_tlb_entry *e;

  //1. sw_tlb 락을 획득한다.
  acquire(&sw_tlb.lock);

  //2. 해시 함수를 통한 인덱스 값을 찾는다.
  index = sw_tlb_hash(pid, va_page);
  e = &sw_tlb.entries[index];

  //3. 캐시 HIT인 경우 반환한다.
  if (e->valid && e->pid == pid && e->va_page == va_page) {
    //Hit일 경우
    *pa_out = (e->pa_page << 12); //페이지 번호 -> 주소
    *flags_out = e->flags;
    sw_tlb.hits++;
    release(&sw_tlb.lock);
    return 1;
  }

  //4. Miss일 경우 0을 반환한다.
  sw_tlb.misses++;
  release(&sw_tlb.lock);
  return 0;
}

/**
 * @brief TLB 캐시에 삽입
 * 
 * @param pid     캐시에 저장할 Pid
 * @param va_page 캐시에 저장할 va_page
 * @param pa_page 캐시에 저장할 pa_page
 * @param flags   캐시에 저장할 flags
 */
static void sw_tlb_insert(uint pid, uint va_page, uint pa_page, uint flags) {
  uint index;
  struct sw_tlb_entry *e;

  //1. 락을 획득한다.
  acquire(&sw_tlb.lock);

  //2. 삽입할 인덱스를 확인한다.
  index = sw_tlb_hash(pid, va_page);

  //3. 캐시에 값을 저장한다.
  e = &sw_tlb.entries[index];
  e->pid = pid;
  e->va_page = va_page;
  e->pa_page = pa_page;
  e->flags = flags;
  e->valid = 1;

  //4. 락을 해제한다.
  release(&sw_tlb.lock);
}

/**
 * @brief 캐시 무효화 (특정 엔트리)
 * 
 * @param pid 무효화할 엔트리를 특정할 pid 변수
 * @param va  무효화할 엔트리를 특정할 va 변수
 */
void sw_tlb_invalidate(uint pid, uint va) {
  uint index;

  //1. 가상 페이지 를 구한다.
  uint va_page = va >> 12;
  struct sw_tlb_entry *e;

  //2. 락을 획득한다.
  acquire(&sw_tlb.lock);

  //3. 캐시 인덱스를 획득한다.
  index = sw_tlb_hash(pid, va_page);
  e = &sw_tlb.entries[index];

  //4. pid, va가 모두 동일하면 invalid하게 바꾼다.
  if (e->valid && e->pid == pid && e->va_page == va_page) {
    e->valid = 0;
  }

  //5. 락을 해제한다.
  release(&sw_tlb.lock);
}

/**
 * @brief 프로세스 종료 시 캐시 전체 무효화
 * @param pid 무효화할 엔트리를 특정할 pid 변수
 */
void sw_tlb_flush_pid(uint pid) {
  int i;

  //1. 락을 획득한다.
  acquire(&sw_tlb.lock);

  //2. 해당 pid를 가진 전체 엔트리를 무효화한다.
  for (i = 0; i < SW_TLB_SIZE; i++) {
    if (sw_tlb.entries[i].valid && sw_tlb.entries[i].pid == pid) {
      sw_tlb.entries[i].valid = 0;
    }
  }

  //3. 락을 해제한다.
  release(&sw_tlb.lock);
}

/**
 * @brief TLB 통계 현황을 출력한다.
 */
void sw_tlb_print_status(void) {
  acquire(&sw_tlb.lock);
  
  cprintf("=== SW TLB Statistics ===\n");
  cprintf("Size:     %d entries\n", SW_TLB_SIZE);
  cprintf("Hits:     %d\n", sw_tlb.hits);
  cprintf("Misses:   %d\n", sw_tlb.misses);
  
  uint total = sw_tlb.hits + sw_tlb.misses;
  if (total > 0) {
    cprintf("Total:    %d\n", total);
    cprintf("Hit Rate: %d%%\n", (sw_tlb.hits * 100) / total);
  }

  release(&sw_tlb.lock);
}

/**
 * @brief pfn을 통해 인덱스로 사용할 해시값을 구한다.
 * 
 * @param pfn : 접근하고자 하는 page frame number
 * @return 해시 테이블 인덱스
 */
uint ipt_hash_func(uint pfn) {
  return pfn % IPT_BUCKETS;
}

/**
 * @brief IPT 락, 해시 테이블 초기화
 */
void ipt_init(void) {
  int i;

  //1. 락 초기화
  initlock(&ipt_lock, "ipt");

  //2. 해시 테이블 초기화
  for (i = 0; i < IPT_BUCKETS; i++) {
    ipt_hash[i] = 0;
  }
}

/**
 * @brief ipt 해시 테이블에 삽입
 * 
 * @param pfn : 엔트리에 저장할 pfn 값
 * @param pid : 엔트리에 저장할 pid 값
 * @param va : 엔트리에 저장할 va 값
 * @param flags : 엔트리에 저장할 flags 스냅샷
 */
void ipt_insert(uint pfn, uint pid, uint va, uint flags) {
  struct ipt_entry *e;
  uint bucket;

  if (!ipt_initialized) {
    return ;
  }

  //1. 가상 주소 페이지 정렬
  //   하위 12비트(오프셋)을 제거하여 페이지 시작 주소만 추출한다.
  uint va_aligned = va & ~0xFFF;

  //2. 동시성 제어를 위한 락 획득
  //   IPT 전역 테이블 보호를 위한 스핀락을 획득한다.
  acquire(&ipt_lock);
  ipt_lock_count++;
  ipt_operations++;

  //3. 해시 버킷 인덱스 계산
  bucket = ipt_hash_func(pfn);

  //4. 중복 엔트리를 검사한다.
  //   pfn, pid, va_page가 모두 같은 경우 중복으로 처리한다.
  for(e = ipt_hash[bucket]; e; e = e->next) {
    if (e->pfn == pfn && e->pid == pid && e->va == va_aligned) {
      //4-1. 중복 발견 시 ref 카운트를 증가시킨다.
      e->refcnt++;
      release(&ipt_lock);
      return ;
    }
  }

  //5. 중복이 없는 경우 새 엔트리를 할당한다.
  e = (struct ipt_entry *)kalloc();
  if (e == 0) {
    release(&ipt_lock);
    if (tracing_initialized) {
      panic("ipt_insert: out of memory");
    }
    return ;
  }

  //5-1. 새 엔트리 변수 초기화
  e->pfn = pfn;
  e->pid = pid;
  e->va = va & ~0xFFF;
  e->flags = flags;
  e->refcnt = 1;

  //7. 해시 체인의 헤드에 삽입한다.
  //   중복이 아닌 해시 충돌일 경우 4번으로 처리되는게 아니기에 헤드에 삽입한다.
  e->next = ipt_hash[bucket];
  ipt_hash[bucket] = e;

  //8. 락 해제한다.
  release(&ipt_lock);
}

/**
 * @brief IPT 엔트리의 flags PTE 권한 (P/W/U 등)의 스냅샷을 업데이트 한다.
 * 
 * @param pfn : 엔트리를 특정할 pfn
 * @param pid : 엔트리를 특정할 pid
 * @param va : 엔트리를 특정할 va
 * @param new_flags : 새로 업데이트할 PTE 권한 스냅샷
 */
void ipt_update_flags(uint pfn, uint pid, uint va, uint new_flags) {
  struct ipt_entry *e;
  uint bucket;

  if (!ipt_initialized) return ;

  //1. 가상 주소 페이지 정렬
  uint va_aligned = va & ~0xFFF;

  //2. 락 획득
  acquire(&ipt_lock);

  //3. 해시 버킷의 인덱스 계산
  bucket = ipt_hash_func(pfn);

  //4. 해시 버킷의 체인을 순회하며 대상 엔트리 검색
  for(e = ipt_hash[bucket]; e; e = e->next) {
    //4-1. 매칭 조건 확인
    if (e->pfn == pfn && e->pid == pid && e->va == va_aligned) {
      //5. 플래그 업데이트
      e->flags = new_flags;
      //6. 락 해제
      release(&ipt_lock);
      return ;
    }
  }

  //6. 해당 엔트리가 없을 경우 락 해제 후 반환
  release(&ipt_lock);
}

/**
 * @brief 주어진 pfn, pid, va에 해당하는 IPT의 엔트리를 제거한다.
 * 
 * @param pfn 제거할 페이지의 프레임 번호
 * @param pid 제거할 페이지의 pid
 * @param va 제거할 페이지의 va
 */
void ipt_remove(uint pfn, uint pid, uint va) {
  struct ipt_entry *e, *prev;
  uint bucket;

  if (!ipt_initialized) return;

  //1. 가상 주소 페이지 정렬
  uint va_aligned = va & ~0xFFF;

  //2. 동시성 제어를 위한 락 획득
  acquire(&ipt_lock);

  //3. 해시 버킷 인덱스 계산
  bucket = ipt_hash_func(pfn);

  //4. 해시 체인 순회
  prev = 0;
  for(e = ipt_hash[bucket]; e; prev = e, e = e->next) {
    //5. refcnt를 감소시킨다.
    if (e->pfn == pfn && e->pid == pid && e->va == va_aligned) {
      e->refcnt--;

      //6. refcnt가 0인 경우 실제 제거를 결정한다.
      if (e->refcnt == 0) {
        //7. 연결 리스트에서 엔트리를 제거한다.
        if (prev)
          prev->next = e->next;
        else
          ipt_hash[bucket] = e->next;
        
        //8. 할당받았던 메모리를 반환한다.
        kfree((char*)e);
      }

      //9. 락 해제후 함수를 종료한다.
      release(&ipt_lock);
      return ;
    }
  }
  //10. 일치하는 엔트리가 없는 경우 락을 해재한다.
  release(&ipt_lock);
}

/**
 * @brief 주어진 프로세스 PID와 관련된 IPT의 모든 엔트리를 제거한다.
 * @param pid : 제거할 매핑 엔트리의 프로세스 ID
 */
void ipt_remove_by_pid(uint pid) {
  struct ipt_entry *e, *next, *prev;
  int i;

  if (!ipt_initialized) return ;

  //1. 락 획득
  acquire(&ipt_lock);

  //2. 전체 해시 테이블 순회 시작
  for(i = 0; i < IPT_BUCKETS; i++) {
    //3. 각 버킷(해시 체인) 탐색을 초기화 한다.
    prev = 0;
    e = ipt_hash[i];

    //4. 해시 체인을 순회한다.
    while(e) {
      //5. 다음 엔트리를 미리 저장
      next = e->next;

      //6. PID 일치 여부를 확인한다.
      if (e->pid == pid) {
        //7. 연결 리스트에서 엔트리를 제거한다.
        if (prev)
          prev->next = next;
        else
          ipt_hash[i] = next;
        
        //8. 제거 엔트리의 메모리를 해제한다.
        kfree((char*)e);

        //9. 다음 순회를 준비한다.
        e = next;
      }
      //10. 제거하지 않는 경우 엔트리 보존 및 순회를 진행한다.
      else {
        prev = e;
        e = next;
      }
    }
  }
  //11. 락 해제 후 함수를 종료한다.
  release(&ipt_lock);
}

/**
 * @brief 주어진 프로세스의 최상위 디렉터리와 가상주소로부터 PTE를 찾아 물리 주소와 플래그를 계산한다.
 * 
 * @param pgdir : 최상위 디렉터리
 * @param va : 가상주소
 * @param pa_out : 물리주소 반환할 변수
 * @param pte_flag : 플래그 반환할 변수
 * @return 0 : 성공, -1 : 실패
 */
int
sw_vtop(pde_t *pgdir, const void *va, uint *pa_out, uint *pte_flags_out) {
  pde_t *pde;
  pte_t *pgtab;
  pte_t *pte;

  uint pa;
  uint pid = myproc() ? myproc()->pid : 0;
  uint va_page = (uint)va >> 12;
  uint flags;

  //1. SW TLB 캐시 조회
  if (sw_tlb_lookup(pid, va_page, &pa, &flags)) {
    //1-1. 캐시 힛
    if (pa_out)
      *pa_out = pa | ((uint)va & 0xFFF); //페이지 주소 + 오프셋
    
    if (pte_flags_out)
      *pte_flags_out = flags;

    return 1;
  }

  //2. MISS
  uint pde_idx = PDX(va);
  pde = &pgdir[pde_idx];

  //2-1. 존재하지 않을 경우
  if ((*pde & PTE_P) == 0)
    return -1;

  //3. PTE 확인
  pgtab = (pte_t *)P2V(PTE_ADDR(*pde));
  uint pte_idx = PTX(va);
  pte = &pgtab[pte_idx];

  //3-1. 존재하지 않을 경우
  if ((*pte & PTE_P) == 0)
    return -1;

  //4. 물리 주소 계산
  pa = PTE_ADDR(*pte) | ((uint)va & 0xFFF);
  flags = PTE_FLAGS(*pte);

  if (pa_out)
    *pa_out = pa;

  if (pte_flags_out)
    *pte_flags_out = flags;

  //5. TLB에 삽입
  sw_tlb_insert(pid, va_page, PTE_ADDR(*pte) >> 12, flags);

  return 0;   //캐시 미스
}

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);

  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;

    if(*pte & PTE_P)
      panic("remap");

    *pte = pa | perm | PTE_P;

    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);

    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }

    uint pa = V2P(mem);
    struct proc *p = myproc();
    if (p) {
      uint pfn = pa / PGSIZE;
      pde_t *pte = walkpgdir(pgdir, (void *)a, 0);
      //ipt 테이블에 추가
      ipt_insert(pfn, p->pid, a, PTE_FLAGS(*pte));
      
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);

    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);

      //IPT, TLB에서 엔트리 삭제
      struct proc *p = myproc();
      if (p && p->pid > 0) {
        uint pfn = pa / PGSIZE;
        ipt_remove(pfn, p->pid, a);
        sw_tlb_invalidate(p->pid, a);
      }

      if(pa == 0)
        panic("kfree");

      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;

  struct proc *p = myproc();
  if (p && p->pid > 0) {
    uint pa = PTE_ADDR(*pte);
    uint pfn = pa / PGSIZE;
    uint va = PGROUNDDOWN((uint)uva);
    uint new_flags = PTE_FLAGS(*pte);

    ipt_update_flags(pfn, p->pid, va, new_flags);
    sw_tlb_invalidate(p->pid, va);
  }
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

/**
 * @brief 구현 C에서 사용할 테스트를 위한 권한 수정 함수 
 * 
 * @param pgdir : 페이지 디렉토리
 * @param addr : 변경할 주소의 가상주소
 * @param flags : 설정할 플래그 값 기본적으로 PTE_P는 보장한다.
 * 
 * @return 0 변환 성공, -1 페이지 없음 혹은 오류
 */
int setpageflags_in(pde_t *pgdir, uint addr, uint flags) {
  pte_t *pte;
  struct proc *p = myproc();

  //페이지 정렬
  addr = PGROUNDDOWN(addr);

  pte = walkpgdir(pgdir, (char *)addr, 0);
  if (!pte || !(*pte & PTE_P)) return -1;

  uint pa = PTE_ADDR(*pte);
  uint pfn = pa/PGSIZE;

  //플래그 설정
  flags |= PTE_P;
  *pte = pa | flags;

  if (p && p->pid > 0) {
    ipt_update_flags(pfn, p->pid, addr, flags);
  }
  sw_tlb_invalidate(p->pid, addr);
  return 0;
}


//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

