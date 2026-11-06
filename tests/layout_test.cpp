// collectBlocks must stop on an empty layout: floating the only pane of a tab
// leaves the tiled layer's layout null, and orex used to recurse until the
// stack overflowed.

#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <cassert>
#include <cstdio>

void collectBlocks(const QJsonObject &node, QList<QPair<QString, QRectF>> &out);

static QJsonObject json(const char *text) { return QJsonDocument::fromJson(text).object(); }

int main() {
    QList<QPair<QString, QRectF>> out;
    collectBlocks(QJsonObject(), out); // a null layout arrives as an empty object
    assert(out.isEmpty());

    collectBlocks(json(R"({"rect":[0,0,1,1],"block_id":"block:a"})"), out);
    assert(out.size() == 1 && out[0].first == "block:a");

    out.clear();
    collectBlocks(json(R"({"rect":[0,0,1,1],"split":{"direction":"horizontal","ratio":0.5,
        "before":{"rect":[0,0,0.5,1],"block_id":"block:a"},
        "after":{"rect":[0.5,0,0.5,1],"split":{"direction":"vertical","ratio":0.5,
            "before":{"rect":[0.5,0,0.5,0.5],"block_id":"block:b"},
            "after":{"rect":[0.5,0.5,0.5,0.5],"block_id":"block:c"}}}}})"), out);
    assert(out.size() == 3 && out[2].first == "block:c" && out[2].second == QRectF(0.5, 0.5, 0.5, 0.5));

    std::puts("layout_test: ok");
}
