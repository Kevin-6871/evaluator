#include <cstdio>
int main() {
	int a, b;
	FILE* in = fopen("source.in", "r");
	if (!in) return 1;
	if (fscanf(in, "%d %d", &a, &b) != 2) { fclose(in); return 1; }
	fclose(in);
	while (b) { int t = a % b; a = b; b = t; }
	FILE* out = fopen("source.out", "w");
	if (!out) return 1;
	fprintf(out, "%d", a);
	fclose(out);
	return 0;
}