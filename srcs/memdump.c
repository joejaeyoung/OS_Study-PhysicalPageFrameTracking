#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_FRINFO 60000 

static void
usage(void)
{
    printf(1, "usage: memdump [-a] [-p PID]\n");
    exit();
}

/**
 * @brief dump_physmem_info() 시스템 콜을 호출해 받아온 프레임 정보를 표 형태로 출력한다.
 * @param -a : free 프레임을 포함한 전체 프레임 테이블을 출력한다.asm
 * @param -p <PID> : 특정 PID가 점유한 프레임만 출력한다.
 * 
 * @return
 */
int main(int argc, char *argv[])
{
    if (argc == 1)
        usage();

    //0. 옵션 처리 변수 할당
    int all = 0;
    int pid = -1;

    //1. 옵션 파싱
    for(int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a")) {
            all = 1;
        }
        else if (!strcmp(argv[i], "-p")) {
            if (i + 1 >= argc) {
                usage();
            }
            i++;
            pid = atoi(argv[i]);
            if (pid <= 0) {
                usage();
            }
        }
    }

    //1-1. 중복 옵션 usage()처리
    if (all == 1 && pid > 0) {
        usage();
    }

    //2. 구현된 내용 - 시스템 콜 호출을 통해 전역 테이블 정보 받아옴
    static struct physframe_info buf[MAX_FRINFO];
    int n = dump_physmem_info((void *)buf, MAX_FRINFO);
    if (n < 0)
    {
        printf(1, "memdump: dump_physmem_info failed\n");
        exit();
    }

    printf(1, "[memdump] pid=%d\n", getpid());
    printf(1, "[frame#]\t[alloc]\t[pid]\t[start_tick]\n");

    
    //3. 구현한 내용 - 옵션 값에 따라 출력
    for (int i = 0; i < n; i++) {
        if (all != 1)
            if (buf[i].allocated == 0) continue;

        if (pid == -1 || (pid != 0 && buf[i].pid == pid))
            printf(1, "%d\t\t%d\t%d\t%d\n",
                buf[i].frame_index, buf[i].allocated,
                buf[i].pid, buf[i].start_tick);
    }
    
    exit();
}