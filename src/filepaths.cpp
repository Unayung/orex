#include "filepaths.h"

#include <QRegularExpression>
#include <QSet>

namespace {
const QString kSeparators = QStringLiteral(" \t\"'`()[]<>{},|⎿⏺│");
// Without a slash, only these count, so code such as `app.stream` doesn't.
const QSet<QString> kKnown = {"png", "jpg", "jpeg", "gif", "webp", "heic", "svg", "pdf", "mp4", "mov", "md",
                              "txt", "log", "csv", "json", "yaml", "yml", "toml", "html", "css", "js", "ts",
                              "tsx", "py", "go", "rs", "swift", "c", "h", "cpp", "rb", "sh", "sql", "xml",
                              "zip", "diff", "patch"};

bool separator(const QString &cell) {
    return cell.isEmpty() || (cell.size() == 1 && kSeparators.contains(cell.at(0)));
}
}

QString FilePaths::path(QString t) {
    while (!t.isEmpty() && QStringLiteral(".,:;!?").contains(t.back()))
        t.chop(1);
    static const QRegularExpression lineColumn(QStringLiteral(":\\d+(:\\d+)?$"));
    t.remove(lineColumn);
    if (t.size() <= 2 || t.contains(QStringLiteral("://")))
        return {};
    static const QRegularExpression extension(QStringLiteral("(?<=[^/.])\\.([A-Za-z][A-Za-z0-9]{0,7})$"));
    const QRegularExpressionMatch m = extension.match(t);
    const QString ext = m.hasMatch() ? m.captured(1).toLower() : QString();
    if (t.contains('/')) {
        const bool rooted = t.startsWith('/') || t.startsWith(QStringLiteral("~/")) ||
                            t.startsWith(QStringLiteral("./")) || t.startsWith(QStringLiteral("../"));
        return !ext.isEmpty() || (rooted && t.mid(1).contains('/')) ? t : QString();
    }
    return kKnown.contains(ext) ? t : QString();
}

QList<FilePaths::Hit> FilePaths::find(const QStringList &cells) {
    QList<Hit> hits;
    int i = 0;
    while (i < cells.size()) {
        if (separator(cells.at(i))) {
            ++i;
            continue;
        }
        int j = i;
        QString token;
        while (j < cells.size() && !separator(cells.at(j)))
            token += cells.at(j++);
        const QString p = path(token);
        if (!p.isEmpty())
            hits.append({i, j, p});
        i = j;
    }
    return hits;
}
