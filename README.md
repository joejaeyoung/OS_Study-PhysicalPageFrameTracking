<div align="center">

# ⚙️ XV6 Physical Page Frame Tracking

**XV6 운영체제 커널에 물리 메모리 프레임 추적, 역페이지 테이블(IPT), 소프트웨어 TLB 기능을 구현**

[![Hits](https://hits.sh/github.com/joejaeyoung/OS_Study-PhysicalPageFrameTracking.svg)](https://github.com/joejaeyoung/OS_Study-PhysicalPageFrameTracking)

</div>

---

## 📋 프로젝트 정보

|    항목     | 내용                  |
| :-------: | :------------------ |
|  **분야**   | OS (메모리 관리)         |
| **개발 기간** | 2024.10             |
| **개발 환경** | Ubuntu Linux + QEMU |

---

## 📖 프로젝트 소개

이 프로젝트는 MIT에서 개발한 교육용 운영체제 **XV6**의 메모리 관련 함수들을 확장하여 **물리 메모리 프레임의 실시간 사용 현황을 추적**하는 기능을 구현한 프로젝트입니다.

**전역 프레임 정보 테이블(`pf_table`)** 을 커널에 생성하여, 프로세스가 어떤 프레임을 사용 중이며 언제부터 사용했는지를 실시간으로 파악할 수 있습니다.

추가로 **소프트웨어 페이지 워커(`sw_vtop`)**, **역페이지 테이블(IPT)**, **SW 기반 TLB(Direct-mapped 캐시)** 를 구현하여 하드웨어 페이지 테이블 의존 없이 소프트웨어 루틴만으로 가상주소를 물리주소로 변환하는 기능과 TLB의 기본 동작을 이해할 수 있게 합니다.

---

## 🚀 시작 가이드

### Requirements

- GCC (i686-linux-gnu cross compiler)
- GNU Make
- QEMU (qemu-system-i386)

### Installation & Run

```bash
# 1. XV6 원본 소스 클론
$ git clone https://github.com/mit-pdos/xv6-public.git
$ cd xv6-public

# 2. Physical Page Frame Tracking 소스 클론 및 덮어쓰기
$ git clone https://github.com/joejaeyoung/OS_Study-PhysicalPageFrameTracking.git
$ cp OS_Study-PhysicalPageFrameTracking/srcs/* .

# 3. 빌드 및 실행
$ make qemu
```

### 테스트 실행 (XV6 쉘 내부)

```bash
$ memtest       # 프레임 추적 기능 통합 테스트
$ memdump -a    # 전체 프레임 테이블 출력
$ memdump -p 4  # 특정 PID의 프레임 정보 출력
$ memstress -n 31 -t 500 -w  # 메모리 스트레스 테스트
$ test_c        # IPT/TLB 고급 기능 테스트
```

---

## 🛠️ Stacks

### Environment

![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![QEMU](https://img.shields.io/badge/QEMU-FF6600?style=for-the-badge&logo=qemu&logoColor=white)

### Development

![C](https://img.shields.io/badge/C-A8B9CC?style=for-the-badge&logo=c&logoColor=white)
![XV6](https://img.shields.io/badge/XV6-000000?style=for-the-badge&logo=mit&logoColor=white)
![GCC](https://img.shields.io/badge/GCC-333333?style=for-the-badge&logo=gnu&logoColor=white)

### Config

![Makefile](https://img.shields.io/badge/Makefile-064F8C?style=for-the-badge&logo=gnu&logoColor=white)

---

## 📺 시스템 콜 인터페이스

### `dump_physmem_info(void *addr, int max_entries)`

| 항목 | 설명 |
|:---|:---|
| **시스템 콜 번호** | 22 |
| **첫 번째 인자** | `addr` — 사용자 제공 버퍼 (physframe_info 배열) |
| **두 번째 인자** | `max_entries` — 프레임 정보를 복사할 최대 개수 (1 이상, PFNNUM 이하) |
| **반환값** | 성공 시 복사된 엔트리 개수, 실패 시 `-1` |

### `vtop(void *va, uint *pa_out, uint *flags_out)`

| 항목 | 설명 |
|:---|:---|
| **시스템 콜 번호** | 23 |
| **첫 번째 인자** | `va` — 검색할 가상 주소 |
| **두 번째 인자** | `pa_out` — 물리 주소 결과를 저장할 포인터 |
| **세 번째 인자** | `flags_out` — PTE 플래그 결과를 저장할 포인터 |
| **반환값** | TLB HIT 시 `1`, MISS 시 `0`, 실패 시 `-1` |

### `phys2virt(uint pa_page, struct vlist *out, int max)`

| 항목 | 설명 |
|:---|:---|
| **시스템 콜 번호** | 24 |
| **첫 번째 인자** | `pa_page` — 검색할 물리 페이지 주소 |
| **두 번째 인자** | `out` — 결과를 저장할 vlist 배열 버퍼 |
| **세 번째 인자** | `max` — 복사할 최대 엔트리 수 |
| **반환값** | 복사한 개수 |

### `setpageflags(uint addr, int flags)`

| 항목 | 설명 |
|:---|:---|
| **시스템 콜 번호** | 25 |
| **첫 번째 인자** | `addr` — 대상 가상 주소 |
| **두 번째 인자** | `flags` — 설정할 PTE 플래그 |

### `print_ipt_status(void)`

| 항목 | 설명 |
|:---|:---|
| **시스템 콜 번호** | 26 |
| **기능** | IPT 및 TLB 통계 현황 출력 |

#### 사용 예시

```c
// 프레임 정보 덤프
struct physframe_info buf[60000];
int n = dump_physmem_info((void *)buf, 60000);

// 가상주소 → 물리주소 변환
uint pa, flags;
vtop((void *)0x1000, &pa, &flags);

// 물리주소 → 가상주소 역매핑
struct vlist result[10];
int count = phys2virt(pa, result, 10);
```

---

## ⭐ 주요 기능

### 1. 물리 메모리 프레임 추적 (Part A)

- `kalloc.c`에 **전역 프레임 정보 테이블(`pf_table[60000]`)** 생성
- 프레임 할당(`kalloc`) / 해제(`kfree`) 시 자동으로 테이블 갱신
- 프레임별 **할당 여부, 소유 PID, 사용 시작 tick** 실시간 추적
- `dump_physmem_info` 시스템 콜로 사용자 공간에서 프레임 정보 조회

### 2. 테스트 도구 (Part B)

- **memdump** : 프레임 정보를 표 형태로 출력 (`-a` 전체, `-p <PID>` 필터링)
- **memstress** : 동적 메모리 할당으로 상태 변화 유도 (`-n`, `-t`, `-w` 옵션)
- **memtest** : memdump + memstress 통합 자동 테스트

### 3. 소프트웨어 페이지 워커 (Part C)

- `sw_vtop()` : pgdir과 va로부터 **소프트웨어만으로 PDE/PTE를 파싱**하여 물리주소 계산
- 하드웨어(에뮬레이터) 접근 없이 PDE 인덱스 → PTE 인덱스 → 물리주소 조합 수행
- TLB 캐시 연동 (HIT 시 즉시 반환, MISS 시 페이지 워킹 후 TLB 삽입)

### 4. 역페이지 테이블 (IPT)

- **해시 체인 기반** 역페이지 테이블 (1024 버킷)
- 물리 프레임 번호 → (PID, 가상주소, 플래그) 역매핑
- `refcnt` 관리로 **동일 물리 프레임의 다중 매핑**(COW 시나리오) 지원
- `allocuvm`, `deallocuvm`, `exit` 등에서 자동 갱신
- **스핀락** 기반 동시성 제어

### 5. SW 기반 TLB (Direct-mapped Cache)

- **64 엔트리** Direct-mapped 캐시 구조
- (PID, va_page) → pa_page 매핑 저장
- **HIT/MISS 통계** 추적 및 출력 기능
- 페이지 테이블 변경 시 자동 **캐시 무효화(invalidation)**
- 프로세스 종료 시 해당 PID의 전체 엔트리 **플러시**

### 6. IPT/TLB 일관성 보장

- remap, munmap류 동작에서 **IPT 갱신 + TLB invalidation** 동시 수행
- `deallocuvm()` : 페이지 해제 시 IPT 제거 + TLB 무효화
- `exit()` : 프로세스 종료 시 `ipt_remove_by_pid` + `sw_tlb_flush_pid`

---

## 🏗️ 아키텍쳐

### 핵심 상수 정의

| 상수 | 값 | 설명 |
|:---|:---:|:---|
| `PFNNUM` | 60,000 | 전역 프레임 정보 테이블 크기 |
| `IPT_BUCKETS` | 1,024 | IPT 해시 버킷 개수 |
| `SW_TLB_SIZE` | 64 | TLB 캐시 엔트리 수 |

### 주요 구조체

#### `physframe_info` (물리 프레임 정보)

| 필드 | 타입 | 기본값 | 설명 |
|:---|:---:|:---:|:---|
| `frame_index` | uint | - | 물리 프레임 번호 |
| `allocated` | int | 0 | 1이면 할당, 0이면 free |
| `pid` | int | -1 | 소유 프로세스 PID |
| `start_tick` | uint | 0 | 사용 시작 tick |

#### `ipt_entry` (역페이지 테이블 엔트리)

| 필드 | 타입 | 설명 |
|:---|:---:|:---|
| `pfn` | uint | 물리 프레임 번호 |
| `pid` | uint | 소유 프로세스 PID |
| `va` | uint | 매핑된 가상 주소 (페이지 기준) |
| `flags` | ushort | PTE 권한 (P/W/U 등) 스냅샷 |
| `refcnt` | ushort | 역참조 카운트 |
| `next` | ipt_entry* | 해시 체인 포인터 |

#### `sw_tlb_entry` (TLB 캐시 엔트리)

| 필드 | 타입 | 설명 |
|:---|:---:|:---|
| `pid` | uint | 프로세스 ID |
| `va_page` | uint | 가상 페이지 번호 (va >> 12) |
| `pa_page` | uint | 물리 페이지 번호 (pa >> 12) |
| `flags` | uint | PTE 플래그 |
| `valid` | int | 유효 비트 |

### 디렉토리 구조

```
OS_Study-PhysicalPageFrameTracking/
├── README.md
└── srcs/
    ├── Makefile            # 빌드 설정 (테스트 바이너리 등록)
    ├── defs.h              # 커널 함수 프로토타입 (IPT/TLB 함수 선언 추가)
    ├── kalloc.c            # 물리 프레임 추적 핵심 (pf_table, kalloc/kfree 연동)
    ├── main.c              # 커널 초기화 (ipt_init, sw_tlb_init, 추적 플래그)
    ├── vm.c                # 가상 메모리 관리 (sw_vtop, IPT, TLB 구현)
    ├── proc.c              # 프로세스 관리 (exit 시 IPT/TLB 정리)
    ├── syscall.h           # 시스템 콜 번호 정의 (22~26번)
    ├── syscall.c           # 시스템 콜 디스패치 테이블 등록
    ├── sysproc.c           # 시스템 콜 구현 (sys_vtop, sys_phys2virt 등)
    ├── user.h              # 유저 공간 구조체 및 함수 프로토타입
    ├── usys.S              # 시스템 콜 어셈블리 스텁
    ├── memdump.c           # 프레임 정보 출력 도구
    ├── memstress.c         # 메모리 스트레스 테스트 도구
    ├── memtest.c           # 통합 테스트 프로그램
    └── test_c.c            # IPT/TLB 고급 기능 테스트
```

### 수정 파일 역할 관계

| 파일 | 역할 | 핵심 변경 사항 |
|:---|:---|:---|
| `kalloc.c` | 프레임 추적 핵심 | pf_table 전역 테이블, kalloc/kfree 연동, dump_physmem_info |
| `vm.c` | 가상 메모리 확장 | sw_vtop, IPT (insert/remove/update), SW TLB 전체 구현 |
| `proc.c` | 프로세스 관리 | exit() 시 ipt_remove_by_pid + sw_tlb_flush_pid |
| `main.c` | 커널 초기화 | ipt_init, sw_tlb_init 호출, tracing_initialized 플래그 |
| `sysproc.c` | 시스템 콜 구현 | sys_vtop, sys_phys2virt, sys_setpageflags, sys_print_ipt_status |
| `syscall.h/c` | 시스템 콜 등록 | 22~26번 시스템 콜 등록 |
| `defs.h` | 함수 선언 | IPT/TLB 관련 함수 프로토타입 추가 |
| `user.h` + `usys.S` | 유저 인터페이스 | 유저 공간 구조체 및 시스템 콜 스텁 |
| `Makefile` | 빌드 설정 | 테스트 바이너리 UPROGS 등록 |

### 동시성 제어

| 락 | 보호 대상 | 사용 위치 |
|:---|:---|:---|
| `kmem.lock` | freelist + pf_table | kalloc, kfree, dump_physmem_info |
| `tickslock` | 전역 ticks 변수 | kalloc 내 start_tick 기록 |
| `ipt_lock` | IPT 해시 테이블 | ipt_insert, ipt_remove, ipt_update_flags 등 |
| `sw_tlb.lock` | TLB 캐시 | sw_tlb_lookup, sw_tlb_insert, sw_tlb_invalidate 등 |
