// The paths orex underlines and opens: what agents print, and what they don't mean as files.

#include <cassert>
#include <cstdio>

#include "../src/filepaths.h"

static QStringList cells(const QString &line) {
    QStringList out;
    for (QChar c : line)
        out << (c == ' ' ? QString() : QString(c));
    return out;
}

static QStringList paths(const QString &line) {
    QStringList out;
    for (const FilePaths::Hit &h : FilePaths::find(cells(line)))
        out << h.path;
    return out;
}

int main() {
    assert(paths("Wrote docs/report.md.") == QStringList{"docs/report.md"});
    assert(paths(QStringLiteral("⏺ Read(src/app.swift)")) == QStringList{"src/app.swift"});
    assert(paths("Saved ./shots/home.png:12:3, done") == QStringList{"./shots/home.png"});
    assert(paths("see /tmp/claude/scratch/out and https://x.com/a.png") == QStringList{"/tmp/claude/scratch/out"});
    assert(paths("app.stream e.g. v1.2 notes.md") == QStringList{"notes.md"});
    assert(paths("  2 + let x = 1").isEmpty());
    const QList<FilePaths::Hit> hits = FilePaths::find(cells("ab docs/a.md"));
    assert(hits.size() == 1 && hits[0].start == 3 && hits[0].end == 12);
    std::puts("paths ok");
}
