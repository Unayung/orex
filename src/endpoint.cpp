#include "endpoint.h"

#include <QRegularExpression>
#include <QUrl>

// The port of a tailnet:// listener, which Rex uses when an address names none.
static constexpr quint16 kRexPort = 7297;

static QString bracketed(const QString &host) {
    // An IPv6 literal needs brackets wherever a port may follow it.
    return host.contains(':') ? QLatin1Char('[') + host + QLatin1Char(']') : host;
}

QString Endpoint::toString() const {
    if (kind == Unix)
        return socketPath;
    const QString scheme = kind == Tls ? QStringLiteral("https://") : QStringLiteral("http://");
    const QString user = account.isEmpty() ? QString() : account + QLatin1Char('@');
    // The port is always written out, so the string can't parse back to a
    // different default than the one this endpoint was resolved to.
    return scheme + user + bracketed(host) + QLatin1Char(':') + QString::number(port);
}

QByteArray Endpoint::hostHeader() const {
    // The local server doesn't look at Host, and a socket path isn't a host.
    if (kind == Unix)
        return "localhost";
    const quint16 standard = kind == Tls ? 443 : 80;
    QString value = bracketed(host);
    if (port != standard)
        value += QLatin1Char(':') + QString::number(port);
    return value.toUtf8();
}

QList<Endpoint> parseEndpoint(const QString &address, QString *error) {
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QList<Endpoint>{};
    };

    const QString text = address.trimmed();
    if (text.isEmpty())
        return fail(QStringLiteral("no server address"));

    if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1String("unix://"))) {
        Endpoint local;
        local.socketPath = text.startsWith(QLatin1Char('/')) ? text : text.mid(7);
        if (!local.socketPath.startsWith(QLatin1Char('/')))
            return fail(QStringLiteral("%1: a Unix socket path must be absolute").arg(text));
        return {local};
    }

    // A bare host is parsed as if it were HTTPS, then expanded into the
    // schemes Rex probes for it.
    const bool bare = !text.contains(QLatin1String("://"));
    const QUrl url(bare ? QStringLiteral("https://") + text : text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return fail(QStringLiteral("%1: not a server address").arg(text));
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
        return fail(QStringLiteral("%1: only unix, https and http addresses are supported").arg(text));
    if ((!url.path().isEmpty() && url.path() != QLatin1String("/")) || url.hasQuery() || url.hasFragment())
        return fail(QStringLiteral("%1: a server address has no path").arg(text));

    Endpoint base;
    base.host = url.host();
    base.account = url.userName();
    // The account travels in a WebSocket subprotocol name, so it must be a
    // plain token; account names on Linux and FreeBSD always are.
    static const QRegularExpression token(QStringLiteral("^[A-Za-z0-9._$-]+$"));
    if (!base.account.isEmpty() && !token.match(base.account).hasMatch())
        return fail(QStringLiteral("%1: %2 is not an account name").arg(text, base.account));

    const int port = url.port();
    auto make = [&base](Endpoint::Kind kind, quint16 p) {
        Endpoint e = base;
        e.kind = kind;
        e.port = p;
        return e;
    };
    if (!bare) {
        const bool tls = url.scheme() == QLatin1String("https");
        return {make(tls ? Endpoint::Tls : Endpoint::Tcp, port >= 0 ? quint16(port) : tls ? 443 : kRexPort)};
    }
    if (port >= 0)
        return {make(Endpoint::Tls, quint16(port)), make(Endpoint::Tcp, quint16(port))};
    return {make(Endpoint::Tls, 443), make(Endpoint::Tls, kRexPort), make(Endpoint::Tcp, kRexPort)};
}
