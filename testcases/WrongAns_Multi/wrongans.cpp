#include <cstdio>
int main() {
freopen("source.in", "r", stdin);    // 评测器会改写为 wrongans.in
freopen("source.out", "w", stdout);  // 改写为 wrongans.out
int a, b;
while (scanf("%d%d", &a, &b) == 2)
printf("%d\n", a + b);
return 0;
}