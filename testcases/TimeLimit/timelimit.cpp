#include <cstdio>
volatile long long g_acc = 0;
int main() {
	while (true) {
		g_acc = g_acc * 31 + 7;
	}
	return 0;
}