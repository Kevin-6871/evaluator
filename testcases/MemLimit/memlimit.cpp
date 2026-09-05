#include <cstdio>
#include <vector>
#include <cstdlib>
int main() {
	// 渐进分块提交: 峰值内存会稳定越过软限 (100MB), 直至触及内存硬杀线后 malloc 返回 NULL
	std::vector<void*> blocks;
	long long total = 0;
	for (int i = 0; i < 80; ++i) {
		void* p = malloc(4 * 1024 * 1024);   // 每次 4MB
		if (!p) break;
		volatile char* c = (volatile char*)p;
		for (int j = 0; j < 4 * 1024 * 1024; j += 4096) c[j] = 1;   // 触碰提交
		blocks.push_back(p);
		total += 4 * 1024 * 1024LL;
	}
	FILE* out = fopen("source.out", "w");
	if (out) { fprintf(out, "%lld", total); fclose(out); }
	return 0;
}