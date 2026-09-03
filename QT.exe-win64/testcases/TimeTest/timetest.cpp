#include <cstdio>
int main() {
	volatile long long sum = 0;
	for (long long i = 0; i < 200000000LL; ++i) sum += i;
	FILE* out = fopen("source.out", "w");
	if (out) { fprintf(out, "%lld", sum); fclose(out); }
	return 0;
}