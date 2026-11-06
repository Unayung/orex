#pragma once

// Where a Rex server listens, in the forms `rex -S` and `rex hosts add` take:
// a Unix socket path or unix:///path, https://[account@]host[:port],
// http://[account@]host[:port], or a bare [account@]host[:port].

#include <QList>
#include <QString>

struct Endpoint {
    enum Kind { Unix, Tcp, Tls };

    Kind kind = Unix;
    QString socketPath; // Unix only
    QString host;       // Tcp and Tls only
    quint16 port = 0;
    // The login-server account to ask for, or empty for the caller's default.
    QString account;

    // The endpoint written as an address that parses back to this one endpoint.
    QString toString() const;
    // The value of the HTTP Host header for this endpoint.
    QByteArray hostHeader() const;
};

// The endpoints to try for an address, in order. A bare host name gives
// several, because Rex probes HTTPS on 443, then HTTPS and HTTP on 7297; the
// first that completes the WebSocket upgrade is the server. An address that
// can't be parsed gives an empty list and sets *error.
QList<Endpoint> parseEndpoint(const QString &address, QString *error = nullptr);
