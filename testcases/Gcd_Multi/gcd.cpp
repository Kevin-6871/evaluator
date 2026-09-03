#include <cstdio>
static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }
int main() {
// 直接用与源文件同名的文件流, 无需评测器改写
freopen("gcd.in", "r", stdin);
freopen("gcd.out", "w", stdout);
int a, b;
while (scanf("%d%d", &a, &b) == 2)
printf("%d\n", gcd(a, b));
return 0;
}