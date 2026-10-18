#include <stdio.h>

#define MAX_NUM 10

/* 计算斐波那契数列第 n 项 */
long long fibonacci(int n)
{
    if (n <= 1)
        return n;

    long long prev = 0, curr = 1;
    for (int i = 2; i <= n; i++) {
        long long next = prev + curr;
        prev = curr;
        curr = next;
    }
    return curr;
}

int main(void)
{
    printf("=== 斐波那契数列 (前 %d 项) ===\n", MAX_NUM);

    for (int i = 0; i < MAX_NUM; i++) {
        printf("F(%d) = %lld\n", i, fibonacci(i));
    }

    return 0;
}
