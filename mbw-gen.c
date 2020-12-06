#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define COMMON_CMD " &\nPID=$!;for i in {1..60};do sleep 10;test -e /proc/$PID || break;done\nkillall -9 mbw 2>/dev/null\nsleep 1\n"

void execute(int nproc, int step)
{
    printf("./mbw -p %d -f 0:%d:%d -q -n 10 -r 20 1024 > m%d-%d.txt" COMMON_CMD, nproc, step, nproc * step - 1, nproc, step);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage %s NCPU\n", argv[0]);
        return 1;
    }
    int N = strtoul(argv[1], (char **)NULL, 10);
    char *map = malloc(N + 1);
    memset(map, 0, N + 1);

    {
        int im;
        for (im = 1; im <= sqrt(N); im++)
        {
            if (N % im)
                continue;
            int i = N / im;
            for (int j = 1; j <= im; j++)
                execute(i, j);
            map[i] = 1;
        }
        for (; im < N; im++)
        {
            if (N % im)
                continue;
            int i = N / im;
            for (int j = 1; j <= im; j *= 2)
                execute(i, j);
            map[i] = 1;
        }
    }
    for (int i = N; i >= 1; i--)
    {
        if (map[i])
            continue;
        execute(i, 1);
    }
    return 0;
}
