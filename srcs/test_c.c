// test_ipt_advanced.c - IPT/TLB 고급 기능 테스트
#include "types.h"
#include "stat.h"
#include "user.h"

#define PTE_P 0x001
#define PTE_W 0x002
#define PTE_U 0x004

// 테스트 1: 다양한 권한 조합 검증
void test_permission_flags(void)
{
	uint va, pa, flags;
	int hit;
	char *p;
	
	printf(1, "\n========================================\n");
	printf(1, "Test 1: Flags 검증\n");
	printf(1, "========================================\n");
	
	// 힙 메모리 할당 (기본적으로 PTE_P | PTE_W)
	p = sbrk(4096);
	if (p == (char*)-1) {
	printf(2, "sbrk failed\n");
	return;
	}
	
	va = (uint)p;
	p[0] = 'A';	// 실제 페이지 할당 트리거
	
	// vtop으로 확인
	if ((hit = vtop((void*)va, &pa, &flags)) < 0) {
	printf(2, "vtop failed\n");
	return;
	}
	
	printf(1, "\n[Heap Page]\n");
	printf(1, "	VA:	0x%x\n", va);
	printf(1, "	PA:	0x%x\n", pa);
	printf(1, "	Flags: 0x%x\n", flags);
	printf(1, "	P (Present):	%s\n", (flags & 0x1) ? "YES" : "NO");
	printf(1, "	W (Writable): %s\n", (flags & 0x2) ? "YES" : "NO");
	printf(1, "	U (User):	 %s\n", (flags & 0x4) ? "YES" : "NO");
	printf(1, "  Cache: %s\n", hit > 0? "HIT" : "MISS");

	// 검증
	if ((flags & 0x7) == 0x7) {
	printf(1, "	[PASS] Heap has correct flags (P|W|U)\n");
	} else {
	printf(1, "	[FAIL] Unexpected flags\n");
	}
	
	// 스택 주소 테스트
	int stack_var = 42;
	va = (uint)&stack_var;
	
	if ((hit = vtop((void*)va, &pa, &flags)) == 0) {
		printf(1, "\n[Stack Page]\n");
		printf(1, "	VA:	0x%x\n", va);
		printf(1, "	PA:	0x%x\n", pa);
		printf(1, "	Flags: 0x%x\n", flags);
		printf(1, "	P (Present):	%s\n", (flags & 0x1) ? "YES" : "NO");
		printf(1, "	W (Writable): %s\n", (flags & 0x2) ? "YES" : "NO");
		printf(1, "	U (User):	 %s\n", (flags & 0x4) ? "YES" : "NO");
		printf(1, "  Cache: %s\n", hit > 0? "HIT" : "MISS");

		if ((flags & 0x7) == 0x7) {
			printf(1, "	[PASS] Stack has correct flags (P|W|U)\n");
		} else {
			printf(1, "	[FAIL] Unexpected flags\n");
		}
	}
}

// 테스트 2: COW 시나리오 (IPT 중복 체인)
void test_cow_scenario(void)
{
	int pid;
	char *p;
	uint va, pa_parent, pa_child, flags;
	int hit;
	struct vlist buffer[10];
	int count, i;
	
	printf(1, "\n========================================\n");
	printf(1, "Test 2: COW Scenario (IPT 중복)\n");
	printf(1, "========================================\n");
	
	// 부모에서 메모리 할당
	p = sbrk(4096);
	if (p == (char*)-1) {
	printf(2, "sbrk failed\n");
	return;
	}
	
	va = (uint)p;
	p[0] = 'P';	// Parent
	
	// 부모의 물리 주소 확인
	if ((hit = vtop((void*)va, &pa_parent, &flags)) < 0) {
		printf(2, "vtop failed\n");
		return;
	}
	
	printf(1, "\n[Before fork]\n");
	printf(1, "Parent PID=%d, VA=0x%x, PA=0x%x\n", getpid(), va, pa_parent);
	
	// IPT에서 해당 물리 페이지 확인
	count = phys2virt(pa_parent, buffer, 1024);
	printf(1, "IPT entries for PA=0x%x: %d\n", pa_parent, count);
	for (i = 0; i < count; i++) {
		printf(1, "	[%d] PID=%d, VA=0x%x\n", i, buffer[i].pid, buffer[i].va);
	}
	
	if (count == 1) {
		printf(1, "[PASS] IPT has single entry before fork\n");
	} else {
		printf(1, "[FAIL] Unexpected IPT entry count: %d\n", count);
	}
	
	// fork
	pid = fork();
	
	if (pid == 0) {
		// 자식 프로세스
		sleep(20);	// 부모가 먼저 확인하도록 대기
		
		printf(1, "\n[Child after fork]\n");
		
		// 자식의 물리 주소 확인 (COW로 같아야 함)
		if (vtop((void*)va, &pa_child, &flags) == 0) {
			printf(1, "Child	PID=%d, VA=0x%x, PA=0x%x\n", getpid(), va, pa_child);
			
			if (pa_parent == pa_child) {
				printf(1, "[PASS] Child shares same physical page (COW)\n");
				
				// IPT에 두 개 엔트리가 있어야 함
				count = phys2virt(pa_child, buffer, 1024);
				printf(1, "IPT entries for PA=0x%x: %d\n", pa_child, count);
				for (i = 0; i < count; i++) {
					printf(1, "	[%d] PID=%d, VA=0x%x\n", i, buffer[i].pid, buffer[i].va);
				}
				
				if (count == 2) {
					printf(1, "[PASS] IPT has duplicate entries (parent + child)\n");
				} else {
					printf(1, "[FAIL] Expected 2 entries, got %d\n", count);
				}
			} else {
				printf(1, "[INFO] Child has different PA (COW not implemented or already copied)\n");
			}
		}
		
		// 자식이 쓰기 (COW 발생)
		printf(1, "\n[Child writing to page]\n");
		p[0] = 'C';	// Child - COW 트리거
		
		if (vtop((void*)va, &pa_child, &flags) == 0) {
			printf(1, "After write: PA=0x%x\n", pa_child);
			
			if (pa_parent != pa_child) {
				printf(1, "[PASS] COW triggered, new physical page allocated\n");
				
				// 이제 각자 다른 물리 페이지
				count = phys2virt(pa_child, buffer, 1024);
				printf(1, "IPT entries for child PA=0x%x: %d\n", pa_child, count);
				if (count == 1) {
					printf(1, "[PASS] Child has separate IPT entry\n");
				}
			} else {
				printf(1, "[INFO] Still same PA (COW not implemented)\n");
			}
		}
		
		exit();
	} else {
		// 부모 프로세스
		printf(1, "\n[Parent after fork]\n");
		
		// IPT 확인 (fork 직후)
		sleep(20);
		count = phys2virt(pa_parent, buffer, 1024);
		printf(1, "IPT entries for PA=0x%x: %d\n", pa_parent, count);
		for (i = 0; i < count; i++) {
			printf(1, "	[%d] PID=%d, VA=0x%x\n", i, buffer[i].pid, buffer[i].va);
		}
		
		wait();	// 자식 대기
		
		// 자식 종료 후 IPT 확인
		printf(1, "\n[After child exit]\n");
		count = phys2virt(pa_parent, buffer, 1024);
		printf(1, "IPT entries for PA=0x%x: %d\n", pa_parent, count);
		for (i = 0; i < count; i++) {
			printf(1, "	[%d] PID=%d, VA=0x%x\n", i, buffer[i].pid, buffer[i].va);
		}
		
		if (count == 1) {
			printf(1, "[PASS] Child's IPT entry cleaned up after exit\n");
		} else {
			printf(1, "[FAIL] IPT cleanup failed, %d entries remain\n", count);
		}
	}
}

#define PFNNUM 60000

void test_exit_cleanup() {
	printf(1, "\n========================================\n");
	printf(1, "Test 3: EXIT시 IPT 초기화 검증\n");
	printf(1, "========================================\n");

	int pid;
	int child_pid;
	int i;
	char *p;
	uint pa, flags;
	int hit;
	
	// dump_physmem_info를 위한 버퍼
	struct physframe_info *pf_info;
	int total_entries;
	
	// 메모리 할당 (PFNNUM이 크므로 sbrk 사용)
	pf_info = (struct physframe_info*)sbrk(PFNNUM * sizeof(struct physframe_info));
	if (pf_info == (struct physframe_info*)-1) {
		printf(2, "Failed to allocate buffer\n");
		exit();
	}

	pid = fork();

	if (pid == 0) {
		// ==========================================
		// 자식 프로세스
		// ==========================================
		child_pid = getpid();
		
		printf(1, "\n[Child PID=%d allocating memory]\n", child_pid);
		
		// 5개 페이지 할당
		for (i = 0; i < 5; i++) {
			p = sbrk(4096);
			if (p == (char*)-1) {
				printf(2, "sbrk failed\n");
				break;
			}
			p[0] = 'A' + i;  // 실제 할당 트리거
			
			if ((hit = vtop((void*)p, &pa, &flags)) == 0) {
				printf(1, "	Page %d: VA=0x%x, PA=0x%x\n", i, (uint)p, pa);
			}
		}
		
		printf(1, "[Child exiting]\n");
		exit();
		
	} else {
		// ==========================================
		// 부모 프로세스
		// ==========================================
		child_pid = pid;
		
		wait();	// 자식 종료 대기
		
		printf(1, "\n[Parent checking IPT after child exit]\n");
		
		// ★ dump_physmem_info로 전체 프레임 정보 가져오기
		total_entries = dump_physmem_info(pf_info, PFNNUM);
		
		if (total_entries < 0) {
			printf(2, "dump_physmem_info failed\n");
			exit();
		}
		
		printf(1, "Scanned %d frames from IPT\n", total_entries);
		
		// 자식 PID를 가진 프레임 찾기
		int found_child = 0;
		int child_frame_count = 0;
		
		for (i = 0; i < total_entries; i++) {
			if (pf_info[i].allocated && pf_info[i].pid == child_pid) {
				printf(1, "	[FAIL] Found child PID=%d at frame #%d\n",
					   child_pid, pf_info[i].frame_index);
				found_child = 1;
				child_frame_count++;
			}
		}
		
		printf(1, "\n=== Result ===\n");
		if (!found_child) {
			printf(1, "[PASS] No child entries found in IPT (cleaned up)\n");
		} else {
			printf(1, "[FAIL] Found %d child entries remaining in IPT\n", 
				   child_frame_count);
		}
	}
}

// 테스트 4: munmap/remap 류 동작 (sbrk 축소)
void test_munmap_behavior(void)
{
	char *p;
	uint va, pa1, pa2, flags;
	int hit;
	struct vlist buffer[10];
	int count;
	
	printf(1, "\n========================================\n");
	printf(1, "Test 4: munmap 계열 (sbrk로 공간 감소)\n");
	printf(1, "========================================\n");
	
	// 메모리 확장
	printf(1, "\n[Allocating 3 pages]\n");
	p = sbrk(3 * 4096);
	if (p == (char*)-1) {
		printf(2, "sbrk failed\n");
		return;
	}
	
	va = (uint)p;
	p[0] = 'A';
	p[4096] = 'B';
	p[8192] = 'C';
	
	// 첫 페이지 물리 주소 확인
	if (vtop((void*)va, &pa1, &flags) < 0) {
		printf(2, "vtop failed\n");
		return;
	}
	
	printf(1, "Page 0: VA=0x%x, PA=0x%x\n", va, pa1);
	
	// IPT 확인
	count = phys2virt(pa1, buffer, 10);
	printf(1, "IPT entries for PA=0x%x: %d\n", pa1, count);
	
	if (count == 1) {
		printf(1, "[PASS] Page is in IPT before deallocation\n");
	}
	
	// TLB 캐시 확인 (두 번 조회해서 HIT 확인)
	hit = vtop((void*)va, &pa2, &flags);
	printf(1, "TLB status: %s\n", hit ? "HIT (cached)" : "MISS");
	
	// 메모리 축소 (munmap과 유사)
	printf(1, "\n[Shrinking by 2 pages]\n");
	p = sbrk(-2 * 4096);
	if (p == (char*)-1) {
		printf(2, "sbrk shrink failed\n");
		return;
	}
	
	// 해제된 페이지의 IPT 확인
	count = phys2virt(pa1, buffer, 1024);
	printf(1, "IPT entries for PA=0x%x after shrink: %d\n", pa1, count);
	
	// 첫 페이지는 남아있어야 함 (3페이지 중 뒤의 2개만 해제)
	if (count == 1) {
		printf(1, "[PASS] Remaining page still in IPT\n");
	} else if (count == 0) {
		printf(1, "[INFO] Page removed from IPT (implementation specific)\n");
	}
	
	int ret;
	ret = vtop((void*)(va + 8192), &pa2, &flags);
	// printf(1, "ret : %d\n", ret);
	// TLB 무효화 확인
	if ((ret) < 0) {
		printf(1, "[PASS] Deallocated page is not accessible\n");
		
		// TLB에서도 제거되었는지 확인 (접근 실패면 무효화 성공)
		printf(1, "[PASS] TLB invalidation successful\n");
	} else {
		printf(1, "[FAIL] Deallocated page is still accessible\n");
	}
}

// 테스트 5: TLB 캐시 일관성
void test_tlb_consistency(void)
{
	char *p;
	uint va, pa, flags;
	int hit1, hit2, hit3;
	
	printf(1, "\n========================================\n");
	printf(1, "Test 5: TLB Cache 일관성 \n");
	printf(1, "========================================\n");
	
	// 새 페이지 할당
	p = sbrk(4096);
	if (p == (char*)-1) {
		printf(2, "sbrk failed\n");
		return;
	}
	
	va = (uint)p;
	p[0] = 'A';
	
	// 첫 번째 조회 (MISS 예상)
	printf(1, "\n[First vtop call - should be MISS]\n");
	if ((hit1 = vtop((void*)va, &pa, &flags)) == 0) {
		printf(1, "Result: %s\n", hit1 ? "HIT" : "MISS");
		if (!hit1) {
			printf(1, "[PASS] First access is cache MISS\n");
		} else {
			printf(1, "[FAIL] First access should be MISS\n");
		}
	}
	
	// 두 번째 조회 (HIT 예상)
	printf(1, "\n[Second vtop call - should be HIT]\n");
	if ((hit2 = vtop((void*)va, &pa, &flags) == 0)) {
		printf(1, "Result: %s\n", hit2 ? "HIT" : "MISS");
		if (hit2) {
			printf(1, "[PASS] Second access is cache HIT\n");
		} else {
			printf(1, "[FAIL] Second access should be HIT\n");
		}
	}
	
	// 페이지 해제 (TLB 무효화 트리거)
	printf(1, "\n[Deallocating page - TLB should invalidate]\n");
	sbrk(-4096);
	
	// 세 번째 조회 (실패하거나 MISS여야 함)
	printf(1, "\n[Third vtop call after dealloc]\n");
	if ((hit3 = vtop((void*)va, &pa, &flags) < 0)) {
		printf(1, "[PASS] vtop correctly fails on deallocated page\n");
	} else {
		printf(1, "Result: %s\n", hit3 ? "HIT" : "MISS");
		if (!hit3) {
			printf(1, "[PASS] TLB was invalidated (MISS after dealloc)\n");
		} else {
			printf(1, "[FAIL] TLB still has stale entry (HIT after dealloc)\n");
		}
	}
}

void test_permission_different_flags(void) {
	printf(1, "\n========================================\n");
	printf(1, "Test 6: 다양한 권한의 페이지 생성 후 vtop 결과 검증 \n");
	printf(1, "==========================================\n");

	char *pages[4];
	int i;
	uint pa, flags;
	int hit;

	printf(1, "=== Creating Pages with Different Permissions ===\n\n");

	// 4개 페이지 할당 (모두 처음에는 0x7)
	for (i = 0; i < 4; i++) {
	pages[i] = sbrk(4096);
	if (pages[i] == (char*)-1) {
		printf(2, "sbrk failed\n");
		exit();
	}
	pages[i][0] = 'A' + i;  // 실제 할당 트리거
	}

	printf(1, "Allocated 4 pages, now changing permissions...\n\n");

	// ============================================
	// 조합 1: Read-Only (P | U)
	// ============================================
	printf(1, "[Page 0] Setting to Read-Only\n");
	if (setpageflags((uint)pages[0], PTE_P | PTE_U) < 0) {
	printf(2, "setpageflags failed\n");
	} else {
	printf(1, "  Expected flags: 0x5 (P=1 W=0 U=1)\n");
	
	// ★ vtop으로 검증
	if ((hit = vtop(pages[0], &pa, &flags)) == 0) {
		printf(1, "  Actual flags:   0x%x (P=%d W=%d U=%d)\n",
			flags,
			(flags & 0x1) ? 1 : 0,
			(flags & 0x2) ? 1 : 0,
			(flags & 0x4) ? 1 : 0);
		printf(1, "  Cache: %s\n", hit ? "HIT" : "MISS");
		
		if (flags == 0x5) {
		printf(1, "  [PASS] Flags match!\n");
		} else {
		printf(1, "  [FAIL] Flags mismatch!\n");
		}
	} else {
		printf(2, "  [FAIL] vtop failed\n");
	}
	}

	// ============================================
	// 조합 2: Read-Write (P | W | U) - 기본값
	// ============================================
	printf(1, "\n[Page 1] Setting to Read-Write\n");
	if (setpageflags((uint)pages[1], PTE_P | PTE_W | PTE_U) < 0) {
	printf(2, "setpageflags failed\n");
	} else {
	printf(1, "  Expected flags: 0x7 (P=1 W=1 U=1)\n");
	
	// ★ vtop으로 검증
	if ((hit = vtop(pages[1], &pa, &flags)) == 0) {
		printf(1, "  Actual flags:   0x%x (P=%d W=%d U=%d)\n",
			flags,
			(flags & 0x1) ? 1 : 0,
			(flags & 0x2) ? 1 : 0,
			(flags & 0x4) ? 1 : 0);
		printf(1, "  Cache: %s\n", hit ? "HIT" : "MISS");
		
		if (flags == 0x7) {
		printf(1, "  [PASS] Flags match!\n");
		} else {
		printf(1, "  [FAIL] Flags mismatch!\n");
		}
	} else {
		printf(2, "  [FAIL] vtop failed\n");
	}
	}

	// ============================================
	// 조합 3: Write-Only (실제로는 RW가 됨 - x86 제약)
	// ============================================
	printf(1, "\n[Page 2] Setting to Write-Only (becomes RW on x86)\n");
	if (setpageflags((uint)pages[2], PTE_P | PTE_W | PTE_U) < 0) {
	printf(2, "setpageflags failed\n");
	} else {
	printf(1, "  Expected flags: 0x7 (P=1 W=1 U=1)\n");
	printf(1, "  Note: x86 doesn't support write-only\n");
	
	// ★ vtop으로 검증
	if ((hit = vtop(pages[2], &pa, &flags)) == 0) {
		printf(1, "  Actual flags:   0x%x (P=%d W=%d U=%d)\n",
			flags,
			(flags & 0x1) ? 1 : 0,
			(flags & 0x2) ? 1 : 0,
			(flags & 0x4) ? 1 : 0);
		printf(1, "  Cache: %s\n", hit ? "HIT" : "MISS");
		
		if (flags == 0x7) {
		printf(1, "  [PASS] Flags match!\n");
		} else {
		printf(1, "  [FAIL] Flags mismatch!\n");
		}
	} else {
		printf(2, "  [FAIL] vtop failed\n");
	}
	}

	// ============================================
	// 조합 4: Read-Only (다시)
	// ============================================
	printf(1, "\n[Page 3] Setting to Read-Only\n");
	if (setpageflags((uint)pages[3], PTE_P | PTE_U) < 0) {
	printf(2, "setpageflags failed\n");
	} else {
	printf(1, "  Expected flags: 0x5 (P=1 W=0 U=1)\n");
	
	// ★ vtop으로 검증
	if ((hit = vtop(pages[3], &pa, &flags)) == 0) {
		printf(1, "  Actual flags:   0x%x (P=%d W=%d U=%d)\n",
			flags,
			(flags & 0x1) ? 1 : 0,
			(flags & 0x2) ? 1 : 0,
			(flags & 0x4) ? 1 : 0);
		printf(1, "  Cache: %s\n", hit ? "HIT" : "MISS");
		
		if (flags == 0x5) {
		printf(1, "  [PASS] Flags match!\n");
		} else {
		printf(1, "  [FAIL] Flags mismatch!\n");
		}
	} else {
		printf(2, "  [FAIL] vtop failed\n");
	}
	}

	printf(1, "\n=== Summary ===\n");

	// 주소와 플래그 요약 출력
	for (i = 0; i < 4; i++) {
	printf(1, "Page %d: ", i);
	if ((hit = vtop(pages[i], &pa, &flags)) == 0) {
		printf(1, "VA=0x%x, PA=0x%x, Flags=0x%x %s\n",
			(uint)pages[i], pa, flags,
			(flags == 0x5 || flags == 0x7) ? "[PASS]" : "[FAIL]");
	} else {
		printf(1, "VA=0x%x [FAIL - not mapped]\n", (uint)pages[i]);
	}
	}

	printf(1, "\n=== Test Complete ===\n");
}

int main(void)
{
	int start_ticks = uptime();
	printf(1, "\n");
	printf(1, "========================================\n");
	printf(1, "	 C 구현과제 테스트 시작\n");
	printf(1, "========================================\n");
	
	test_permission_flags();
	
	test_cow_scenario();
	
	test_exit_cleanup();
	
	test_munmap_behavior();
	
	test_tlb_consistency();

	test_permission_different_flags();

	printf(1, "\n");
	printf(1, "========================================\n");
	printf(1, "	 All Tests Complete\n");
	printf(1, "========================================\n");
	printf(1, "\n");

	print_ipt_status();
	
	int end_ticks = uptime();
	printf(1, "총 소요 시간 %d틱\n", end_ticks - start_ticks);
	exit();
}