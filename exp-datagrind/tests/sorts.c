#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define SWAP(a, b) do { int t = (a); (a) = (b); (b) = t; } while (0)

static void bubble_sort(int n, int *a)
{
    for (int i = n - 1; i > 0; i--)
        for (int j = 0; j < i; j++)
            if (a[j] > a[j + 1])
                SWAP(a[j], a[j + 1]);
}

static void selection_sort(int n, int *a)
{
    for (int i = 0; i < n; i++)
    {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (a[j] < a[best])
                best = j;
        SWAP(a[i], a[best]);
    }
}

static void insertion_sort(int n, int *a)
{
    for (int i = 1; i < n; i++)
    {
        int v = a[i];
        int p = i - 1;
        while (p >= 0 && a[p] > v)
        {
            a[p + 1] = a[p];
            p--;
        }
        a[p + 1] = v;
    }
}

static bool validate(int n, int *a);

static void quick_sort(int n, int *a)
{
    int l, r, pivot;

    if (n <= 1) return;
    l = 0;
    r = n - 1;
    pivot = a[(l + r) / 2];
    while (l <= r)
    {
        while (a[l] < pivot) l++;
        while (a[r] > pivot) r--;
        if (l > r) break;
        SWAP(a[l], a[r]);
        l++;
        r--;
    }
    quick_sort(r + 1, a);
    quick_sort(n - l, a + l);
}

/* Sort the contents of pegs[0] into pegs[out], using pegs[1] as working
 * space.
 */
static void merge_sort_r(int n, int * restrict *pegs, int out)
{
    if (n <= 1)
    {
        pegs[out][0] = pegs[0][0];
    }
    else
    {
        int h = n / 2;
        int l = 0;
        int r = h;
        int i = 0;
        int * restrict pegs_right[2] = { pegs[0] + h, pegs[1] + h };
        merge_sort_r(h, pegs, !out);
        merge_sort_r(n - h, pegs_right, !out);

        while (l < h && r < n)
        {
            if (pegs[!out][l] < pegs[!out][r])
                pegs[out][i++] = pegs[!out][l++];
            else
                pegs[out][i++] = pegs[!out][r++];
        }
        memcpy(&pegs[out][i], &pegs[!out][l], (h - l) * sizeof(int));
        memcpy(&pegs[out][i], &pegs[!out][r], (n - r) * sizeof(int));
    }
}

static void merge_sort(int n, int *a)
{
    int * restrict pegs[2];
    pegs[0] = a;
    pegs[1] = malloc(n * sizeof(int));
    merge_sort_r(n, pegs, 0);
    free(pegs[1]);
}

static bool validate(int n, int *a)
{
    for (int i = 1; i < n; i++)
    {
        if (a[i - 1] > a[i])
            return false;
    }
    return true;
}

static int compare(const void *X, const void *Y)
{
    int x = *(const int *) X;
    int y = *(const int *) Y;
    return x - y;
}

static void builtin_sort(int n, int *a)
{
    qsort(a, n, sizeof(int), compare);
}

static void run_sort(const char *name, void (*fn)(int, int *), int n)
{
    int *a = malloc(n * sizeof(int));
    srand(1);
    for (int i = 0; i < n; i++)
        a[i] = rand();
    fn(n, a);
    if (!validate(n, a))
    {
        fprintf(stderr, "%s did not sort correctly!\n", name);
    }
    free(a);
}

int main(void)
{
    run_sort("bubble_sort", bubble_sort, 200);
    run_sort("selection_sort", selection_sort, 200);
    run_sort("insertion_sort", insertion_sort, 200);
    run_sort("quick_sort", quick_sort, 200);
    run_sort("merge_sort", merge_sort, 200);
    run_sort("builtin_sort", builtin_sort, 200);
    return 0;
}
