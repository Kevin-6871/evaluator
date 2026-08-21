#include <cstdio>
#include <vector>
int main() {
	std::vector<long long> v(30000000, 1);
	long long sum = 0;
	for (long long x : v) sum += x;
	FILE* out = fopen("source.out", "w");
	if (out) { fprintf(out, "%lld", sum); fclose(out); }
	return 0;
}