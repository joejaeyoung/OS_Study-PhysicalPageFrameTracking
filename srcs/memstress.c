#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static void
usage(void) {
  printf(1, "usage: memstress [-n pages] [-t ticks] [-w]\n");
  exit();
}

int
main(int argc, char *argv[])
{
  // 1. 옵션에 해당하는 변수 기본 값으로 초기화
  int pages = 10;
  int do_write = 0;
  int hold_ticks = 200;

  // 2. 옵션 파싱
  for(int i = 1; i < argc; i++) {
    // 2-1. -n 옵션 파싱
    if (!strcmp(argv[i], "-n")) {
      //2-1-1. -n 옵션 이후 옵션 값이 없는 경우
      if (i + 1 >= argc) usage();
      i++;
      pages = atoi(argv[i]);
      if (pages <= 0) pages = 0;
    }
    else if (!strcmp(argv[i], "-t")) {
      if (i + 1 >= argc) usage();
      i++;
      hold_ticks = atoi(argv[i]);
      if (hold_ticks <= 0) hold_ticks = 0;
    }
    else if (!strcmp(argv[i], "-w")) {
      do_write = 1;
    }
  }

  //3. 상태 출력
  int pid = getpid();
  printf(1, "[memstress] pid=%d pages=%d hold=%d ticks write=%d\n", pid, pages, hold_ticks, do_write);

  //4. 메모리를 할당한다.
  int inc = pages * 4096; 
  char *base = sbrk(inc);
  if (base == (char*)-1) {
    printf(1, "[memstress] sbrk failed\n");
    exit();
  }

  //5. -w옵션일 경우 메모리에 write를 진행한다.
  if (do_write) {
    for (int p = 0; p < pages; p++) {
      base[p*4096] = (char)(p & 0xff);
    }
  }

  sleep(hold_ticks);

  printf(1, "[memstress] pid=%d done\n", pid);
  exit();
}
