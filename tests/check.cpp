#include "crypto/Bech32.h"
#include "crypto/Keys.h"
#include "nostr/Event.h"
#include "nostr/RelayPool.h"
#include "util/Bytes.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QtLogging>

#include <cstdio>

namespace {

int g_failed = 0;

void expect(bool ok, const char* name) {
    if (ok) {
        std::fprintf(stderr, "ok  %s\n", name);
        return;
    }
    std::fprintf(stderr, "FAIL %s\n", name);
    ++g_failed;
}

void testBech32() {
    const auto zeros = bech32::encode(QStringLiteral("npub"), QByteArray(32, '\0'));
    expect(zeros == QStringLiteral("npub1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqzqujme"), "bech32 zeros");
    const auto decoded = bech32::decode(*zeros);
    expect(decoded && decoded->first == QStringLiteral("npub") && decoded->second == QByteArray(32, '\0'), "bech32 roundtrip");
    QString broken = *zeros;
    broken[10] = broken[10] == QLatin1Char('q') ? QLatin1Char('p') : QLatin1Char('q');
    expect(!bech32::decode(broken), "bech32 rejects bad checksum");

    const auto secret = fromHex(QStringLiteral("b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfef"));
    const auto nsec = bech32::encode(QStringLiteral("nsec"), *secret);
    expect(nsec == QStringLiteral("nsec1kls4zc52a54x40m3tzqfea8nca3ww9s08z6d5448snvsg5vselhsjv8uxn"), "bech32 nsec");
}

void testCanonical() {
    NostrEvent event;
    event.pubkey = QStringLiteral("6e468422dfb74a5738702a8823b9b28168abab8655faacb6853cd0ee15deee93");
    event.createdAt = 1673347337;
    event.kind = 1;
    event.tags = {
        {QStringLiteral("e"), QStringLiteral("3da979448d9ba263864c4d6f14984c423a383ddbc7b540f2e4c5a1c7b0e3c5c1")},
        {QStringLiteral("p"), QStringLiteral("bf2376e17ba4ec269d10fcc996a4746b451152be9031fa48e74553dde5526bce")},
    };
    event.content = QStringLiteral("walled gardens became prisons, and nostr is the first step towards a free world");
    event.computeId();
    expect(event.id == QStringLiteral("c17b84ccfc5fc269baa1cce7d8ead309bdcea802513fe74a954d3fb588657544"), "canonical id");

    NostrEvent unicode;
    unicode.pubkey = QString(64, QLatin1Char('a'));
    unicode.createdAt = 1;
    unicode.kind = 1;
    unicode.tags = {};
    unicode.content = QStringLiteral("say \"hi\"\n");
    unicode.computeId();
    expect(unicode.id == QStringLiteral("7b63c2cc16aa92515ceab8defe3e7b63247182f8f162ad1d4434bac4352366dc"), "canonical escapes");
}

void testKeys() {
    const auto keys = Keys::fromSecretHex(QStringLiteral("B7E151628AED2A6ABF7158809CF4F3C762E7160F38B4DA56A784D9045190CFEF"));
    expect(keys.has_value(), "key import");
    if (!keys)
        return;
    expect(keys->pubkeyHex() == QStringLiteral("dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659"), "xonly pubkey");

    const auto msg = fromHex(QStringLiteral("243F6A8885A308D313198A2E03707344A4093822299F31D0082EFA98EC4E6C89"));
    const auto aux = fromHex(QStringLiteral("0000000000000000000000000000000000000000000000000000000000000001"));
    const auto expected = fromHex(QStringLiteral("6896BD60EEAE296DB48A229FF71DFE071BDE413E6D43F917DC8DCF8C78DE33418906D11AC976ABCCB20B091292BFF4EA897EFCB639EA871CFA95F6DE339E4B0A"));
    expect(keys->sign(*msg, *aux) == *expected, "bip340 sign");
    expect(Keys::verify(keys->pubkey(), *msg, *expected), "bip340 verify");

    NostrEvent event;
    event.kind = 1;
    event.createdAt = 1700000000;
    event.content = QStringLiteral("café ☕");
    event.tags.append({QStringLiteral("client"), QStringLiteral("nostarchy")});
    expect(event.sign(*keys), "sign event");
    expect(event.verify(), "verify own event");
    event.sig[0] = event.sig[0] == QLatin1Char('a') ? QLatin1Char('b') : QLatin1Char('a');
    expect(!event.verify(), "reject bad sig");

    const auto generated = Keys::generate();
    expect(generated && generated->npub().startsWith(QStringLiteral("npub1")), "generate npub");
    QString error;
    expect(!Keys::fromBech32(generated->npub(), &error), "reject npub import");
    auto imported = Keys::fromBech32(generated->nsec(), &error);
    expect(imported && imported->pubkeyHex() == generated->pubkeyHex(), "nsec import");
}

int testRelay() {
    RelayPool pool;
    int verified = 0;
    int dropped = 0;
    QObject::connect(&pool, &RelayPool::eventReceived, &pool, [&](const QString&, const QString&, const QJsonObject& object) {
        auto event = NostrEvent::fromJson(object);
        if (!event)
            return;
        QString reason;
        if (!event->verify(&reason)) {
            ++dropped;
            qInfo().noquote() << "bad" << reason << event->id.left(12) << event->kind;
            return;
        }
        if (event->kind != 1)
            return;
        ++verified;
        qInfo().noquote() << "verified" << event->id.left(12) << event->content.left(80);
        if (verified >= 1)
            QCoreApplication::quit();
    });
    QObject::connect(&pool, &RelayPool::noticeReceived, &pool, [](const QString& relay, const QString& message) {
        qInfo().noquote() << "notice" << relay << message;
    });
    QObject::connect(&pool, &RelayPool::relayStateChanged, &pool, [](const QString& url, const QString& state, const QString& detail) {
        if (!url.isEmpty())
            qInfo().noquote() << state << url << detail;
    });
    pool.setRelays({
        QStringLiteral("wss://relay.damus.io"),
        QStringLiteral("wss://nos.lol"),
        QStringLiteral("wss://relay.nostr.band"),
        QStringLiteral("wss://nostr.mom"),
    });
    QJsonObject filter;
    filter.insert(QStringLiteral("kinds"), QJsonArray{1});
    filter.insert(QStringLiteral("limit"), 3);
    pool.subscribe(QStringLiteral("feed"), QJsonArray{filter});
    QTimer::singleShot(20000, &pool, [] { QCoreApplication::exit(2); });
    const int code = QCoreApplication::exec();
    std::fprintf(stderr, "relay verified=%d dropped=%d exit=%d\n", verified, dropped, code);
    return verified > 0 && dropped == 0 ? 0 : 1;
}

}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    bool net = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--net"))
            net = true;
    }
    testBech32();
    testCanonical();
    testKeys();
    if (g_failed)
        return 1;
    if (!net)
        return 0;
    return testRelay();
}
