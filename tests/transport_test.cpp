// For each address: connect RexClient, list the sessions, and, if a session
// label follows the address after '=', open a data connection to that
// session's first block and count the stream bytes that arrive. Read-only:
// it never types into, resizes, or owns a block.
// Usage: transport_test [--parse-only] ADDRESS[=SESSION] ...

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>
#include <cstdio>

#include "../src/rexclient.h"

static int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static QStringList strings(const QList<Endpoint> &endpoints) {
    QStringList out;
    for (const Endpoint &e : endpoints)
        out << e.toString();
    return out;
}

static void checkParsing() {
    QString error;
    auto one = [&](const QString &address) {
        const QList<Endpoint> list = parseEndpoint(address, &error);
        CHECK(list.size() == 1);
        return list.value(0);
    };

    Endpoint e = one("/run/user/1000/rex/server.sock");
    CHECK(e.kind == Endpoint::Unix && e.socketPath == "/run/user/1000/rex/server.sock");
    CHECK(e.hostHeader() == "localhost");
    e = one("unix:///run/rex/server.sock");
    CHECK(e.kind == Endpoint::Unix && e.socketPath == "/run/rex/server.sock");
    CHECK(parseEndpoint("unix://relative.sock", &error).isEmpty());

    e = one("https://build.example.ts.net");
    CHECK(e.kind == Endpoint::Tls && e.host == "build.example.ts.net" && e.port == 443 && e.account.isEmpty());
    CHECK(e.hostHeader() == "build.example.ts.net");
    e = one("https://build.example.ts.net/");
    CHECK(e.kind == Endpoint::Tls && e.port == 443);
    e = one("https://build.example.ts.net:7297/");
    CHECK(e.port == 7297 && e.hostHeader() == "build.example.ts.net:7297");
    e = one("https://root@build.example.ts.net");
    CHECK(e.account == "root" && e.host == "build.example.ts.net" && e.port == 443);
    CHECK(e.toString() == "https://root@build.example.ts.net:443");
    e = one("http://build.example.ts.net");
    CHECK(e.kind == Endpoint::Tcp && e.port == 7297 && e.hostHeader() == "build.example.ts.net:7297");
    e = one("http://alice@10.0.0.5:8080");
    CHECK(e.kind == Endpoint::Tcp && e.account == "alice" && e.host == "10.0.0.5" && e.port == 8080);
    e = one("http://[fd7a:115c::1]:7297");
    CHECK(e.host == "fd7a:115c::1" && e.hostHeader() == "[fd7a:115c::1]:7297");
    CHECK(e.toString() == "http://[fd7a:115c::1]:7297");
    e = one("http://example.com:80");
    CHECK(e.hostHeader() == "example.com");

    CHECK(strings(parseEndpoint("build.example.ts.net", &error))
          == QStringList({"https://build.example.ts.net:443", "https://build.example.ts.net:7297",
                          "http://build.example.ts.net:7297"}));
    CHECK(strings(parseEndpoint("root@build.example.ts.net/", &error))
          == QStringList({"https://root@build.example.ts.net:443", "https://root@build.example.ts.net:7297",
                          "http://root@build.example.ts.net:7297"}));
    CHECK(strings(parseEndpoint("build.example.ts.net:9000", &error))
          == QStringList({"https://build.example.ts.net:9000", "http://build.example.ts.net:9000"}));

    // Every endpoint's string form parses back to the same endpoint.
    for (const QString &s : QStringList{"https://a@h:1", "http://h:7297", "/x.sock"})
        CHECK(strings(parseEndpoint(s, &error)) == QStringList{s});

    CHECK(parseEndpoint("", &error).isEmpty());
    CHECK(parseEndpoint("ws://host", &error).isEmpty() && error.contains("only unix"));
    CHECK(parseEndpoint("https://host/api", &error).isEmpty() && error.contains("no path"));
    CHECK(parseEndpoint("https://host?x=1", &error).isEmpty());
    CHECK(parseEndpoint("https://a%20b@host", &error).isEmpty() && error.contains("account"));
    CHECK(parseEndpoint("https://", &error).isEmpty());
    std::printf("parsing: %s\n", failures ? "FAILED" : "ok");
}

// Runs one address to completion inside its own event loop.
static void runOne(const QString &address, const QString &sessionLabel) {
    std::printf("\n== %s\n", qPrintable(address));
    RexClient rex;
    WsClient data;
    QEventLoop loop;
    QElapsedTimer clock;
    qint64 streamBytes = -1;
    bool welcomed = false;
    QTimer quiet; // ends the stream count once the snapshot has stopped arriving
    quiet.setSingleShot(true);
    quiet.setInterval(1500);
    auto done = [&] { QTimer::singleShot(0, &loop, &QEventLoop::quit); };

    QObject::connect(&quiet, &QTimer::timeout, [&] {
        std::printf("  data: %lld stream bytes after WELCOME (%lld ms)\n", streamBytes, clock.elapsed());
        data.close();
        done();
    });
    QObject::connect(&data, &WsClient::binaryMessage, [&](const QByteArray &m) {
        if (!welcomed) {
            welcomed = true;
            std::printf("  data: first byte 0x%02x, %lld bytes (%s)\n", uchar(m.at(0)), qlonglong(m.size()),
                        uchar(m.at(0)) == 0x81 && m.size() == 32 ? "WELCOME" : "not a WELCOME");
            streamBytes = 0;
        } else {
            streamBytes += m.size();
        }
        quiet.start();
    });
    QObject::connect(&data, &WsClient::closed, [&](int status, const QString &reason) {
        if (!quiet.isActive() && streamBytes < 0) {
            std::printf("  data: closed %d %s\n", status, qPrintable(reason));
            ++failures;
            done();
        }
    });

    QObject::connect(&rex, &RexClient::closed, [&](const QString &reason) {
        std::printf("  closed after %lld ms: %s\n", clock.elapsed(), qPrintable(reason));
        done();
    });
    QObject::connect(&rex, &RexClient::ready, [&] {
        std::printf("  ready after %lld ms via %s\n", clock.elapsed(), qPrintable(rex.endpoint().toString()));
        rex.call("session.list", {}, [&](const QJsonObject &payload, const QJsonObject &error) {
            QStringList labels;
            QString sessionId;
            for (const QJsonValue &v : payload.value("sessions").toArray()) {
                labels << v["label"].toString();
                if (v["label"].toString() == sessionLabel)
                    sessionId = v["session_id"].toString();
            }
            std::printf("  sessions: %s%s\n", qPrintable(labels.join(", ")),
                        error.isEmpty() ? "" : qPrintable(" error " + error["message"].toString()));
            if (sessionLabel.isEmpty()) {
                done();
                return;
            }
            if (sessionId.isEmpty()) {
                std::printf("  no session labeled %s\n", qPrintable(sessionLabel));
                ++failures;
                done();
                return;
            }
            rex.call("session.attach", {{"session_id", sessionId}}, [&, sessionId](const QJsonObject &p, const QJsonObject &) {
                const QJsonArray windows = p["view"]["windows"].toArray();
                const QString blockId =
                    windows.at(0)["layers"].toArray().at(0)["blocks"].toArray().at(0)["block_id"].toString();
                const QJsonObject args{{"session_id", sessionId},
                                       {"block_id", blockId},
                                       {"data_ref_id", "terminal"},
                                       {"subprotocol", "rex.data.terminal.v1"}};
                rex.call("data.connect", args, [&](const QJsonObject &t, const QJsonObject &err) {
                    if (!err.isEmpty()) {
                        std::printf("  data.connect: %s\n", qPrintable(err["message"].toString()));
                        ++failures;
                        done();
                        return;
                    }
                    const QByteArray ticket = QByteArray::fromBase64(
                        t["ticket"].toString().toLatin1(),
                        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                    QObject::connect(&data, &WsClient::opened, [&data, ticket] {
                        data.sendBinary(QByteArray(1, '\x01') + ticket);
                    });
                    clock.restart();
                    rex.openData(data, "/api/data", "rex.data.terminal.v1");
                });
            });
        });
    });
    clock.start();
    rex.open(address);
    loop.exec();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    checkParsing();
    QStringList args = app.arguments().mid(1);
    if (args.value(0) != "--parse-only") {
        for (const QString &arg : args) {
            const int eq = arg.lastIndexOf('=');
            runOne(eq < 0 ? arg : arg.left(eq), eq < 0 ? QString() : arg.mid(eq + 1));
        }
    }
    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
