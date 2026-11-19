#pragma once

// File paths as agents print them: "Wrote docs/report.md", "Saved ./shots/home.png:12",
// "⏺ Read(src/app.swift)". A guess, not a parser. A row of a terminal is a list of
// cells, one string each, so what's found is in columns.

#include <QList>
#include <QString>
#include <QStringList>

namespace FilePaths {
struct Hit {
    int start = 0; // first column
    int end = 0;   // one past the last
    QString path;
};
// Every path in a row of cells (an empty cell is blank).
QList<Hit> find(const QStringList &cells);
// The path a word names, trimmed of trailing punctuation and :line:column, or empty.
QString path(QString token);
}
