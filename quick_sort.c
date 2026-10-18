#include <stdio.h>

/* 交换两个整数 */
static void swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/*
 * 分区函数（Lomuto 方案）
 * 取最后一个元素作为基准值 pivot，把小于等于 pivot 的元素放到左侧，
 * 大于 pivot 的元素放到右侧，返回基准值最终所在的下标。
 */
static int partition(int arr[], int low, int high)
{
    int pivot = arr[high];
    int i = low - 1; /* i 指向小于等于 pivot 区域的最后一个位置 */

    for (int j = low; j < high; j++) {
        if (arr[j] <= pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }

    swap(&arr[i + 1], &arr[high]);
    return i + 1;
}

/*
 * 快速排序（递归实现）
 * 平均时间复杂度 O(n log n)，最坏 O(n^2)，空间复杂度 O(log n)（递归栈）。
 */
void quick_sort(int arr[], int low, int high)
{
    if (low < high) {
        int pivot_index = partition(arr, low, high);

        quick_sort(arr, low, pivot_index - 1);  /* 排序左半部分 */
        quick_sort(arr, pivot_index + 1, high); /* 排序右半部分 */
    }
}

/* 打印数组 */
static void print_array(const int arr[], int n)
{
    for (int i = 0; i < n; i++)
        printf("%d ", arr[i]);
    printf("\n");
}

int main(void)
{
    int arr[] = {10, 7, 8, 9, 1, 5, -3, 42, 0, 8};
    int n = sizeof(arr) / sizeof(arr[0]);

    printf("排序前: ");
    print_array(arr, n);

    quick_sort(arr, 0, n - 1);

    printf("排序后: ");
    print_array(arr, n);

    return 0;
}
