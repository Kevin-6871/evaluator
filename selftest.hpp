#ifndef SELFTEST_HPP
#define SELFTEST_HPP

#include <QString>

namespace selftest {

// 遍历 root 下的用例目录并跑完整评测。
// report 返回完整文本报告，返回值为通过个数 (0 ~ 总用例数)。
// casesFound 返回探测到的用例目录数。
int runAll(const QString &root, QString &report, int &casesFound);

} // namespace selftest

#endif // SELFTEST_HPP