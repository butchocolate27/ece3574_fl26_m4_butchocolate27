#include "aiws/gui/main_window.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>

namespace {

struct SenderResult {
    int code{-1};
    QByteArray error;
};

SenderResult send_file(const QString& socket, const QString& file,
                       const QString& working_directory = {}) {
    QProcess sender;
    if (!working_directory.isEmpty()) {
        sender.setWorkingDirectory(working_directory);
    }
    sender.start(QCoreApplication::applicationDirPath() + "/aiws_send",
                 {"--socket", socket, "add", file});
    if (!sender.waitForFinished(6500)) {
        sender.kill();
        sender.waitForFinished();
        return {-1, "Sender did not finish."};
    }
    return {sender.exitStatus() == QProcess::NormalExit ? sender.exitCode() : -1,
            sender.readAllStandardError()};
}

struct IpcFixture {
    QTemporaryDir directory;
    aiws::gui::MainWindow window;
    QString socket_path;

    IpcFixture() : socket_path(directory.filePath("server.sock")) {
        QString error;
        if (!directory.isValid() || !window.start_ipc_server(socket_path, error)) {
            qFatal("Could not start test server: %s", error.toUtf8().constData());
        }
    }

    QString file(const QString& name, const QByteArray& contents = "apple engine") {
        const QString path = directory.filePath(name);
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size()) {
            qFatal("Could not write test document");
        }
        return path;
    }

    QListWidget* documents() { return window.findChild<QListWidget*>(); }

    QPushButton* button(const QString& name) {
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->text() == name) {
                return button;
            }
        }
        qFatal("Missing application button");
        return nullptr;
    }
};

int connect_to(const QString& path) {
    std::string error;
    const int descriptor = aiws::ipc::connect_socket(
        path.toStdString(), std::chrono::steady_clock::now() + std::chrono::seconds(2), error);
    return descriptor;
}

int listen_without_reply(const QString& path) {
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const QByteArray bytes = path.toUtf8();
    if (bytes.size() >= static_cast<int>(sizeof(address.sun_path))) {
        ::close(descriptor);
        return -1;
    }
    std::memcpy(address.sun_path, bytes.constData(), bytes.size() + 1);
    if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0
        || ::listen(descriptor, 1) < 0) {
        ::close(descriptor);
        return -1;
    }
    return descriptor;
}

void accept_exit(QWidget* parent) {
    parent->close();
}

}  // namespace

class IpcTests final : public QObject {
    Q_OBJECT

private slots:
    void reply_does_not_wait_for_gui_and_document_is_added() {
        IpcFixture f;
        const QString path = f.file("first.txt");
        // waitForFinished blocks this GUI thread. A reply must still arrive.
        QCOMPARE(send_file(f.socket_path, path).code, 0);
        QCOMPARE(f.documents()->count(), 0);
        QTRY_COMPARE(f.documents()->count(), 1);
        QCOMPARE(f.documents()->item(0)->text(), QString("first.txt"));
        QVERIFY(f.button("Build Corpus")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
    }

    void relative_spaces_unicode_and_newline_path() {
        IpcFixture f;
        const QString name = QString::fromUtf8("a 文档 with spaces\nand newline.txt");
        f.file(name, "banana remote path");
        QCOMPARE(send_file(f.socket_path, name, f.directory.path()).code, 0);
        QTRY_COMPARE(f.documents()->count(), 1);
        QCOMPARE(f.documents()->item(0)->text(), name);
        auto* search = f.window.findChild<aiws::gui::SearchWidget*>();
        search->findChild<QLineEdit*>()->setText("banana");
        f.button("Build Corpus")->click();
        QTRY_VERIFY(f.button("Inspect")->isEnabled());
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void unreadable_file_is_rejected_without_workspace_change() {
        IpcFixture f;
        auto result = send_file(f.socket_path, f.directory.filePath("missing.txt"));
        QCOMPARE(result.code, 1);
        QVERIFY(!result.error.trimmed().isEmpty());
        QTest::qWait(30);
        QCOMPARE(f.documents()->count(), 0);
        QCOMPARE(send_file(f.socket_path, f.directory.path()).code, 1);
        QCOMPARE(send_file(f.socket_path, f.file("good.txt")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 1);
    }

    void unreadable_remote_file_keeps_current_corpus() {
        IpcFixture f;
        const QString path = f.file("valid.txt");
        QVERIFY(f.window.add_document_file(path));

        auto* search = f.window.findChild<aiws::gui::SearchWidget*>();
        search->findChild<QLineEdit*>()->setText("apple");
        f.button("Build Corpus")->click();
        QTRY_VERIFY(f.button("Search")->isEnabled());
        f.button("Search")->click();

        auto* table = f.window.findChild<QTableView*>();
        QCOMPARE(table->model()->rowCount(), 1);

        const auto result = send_file(f.socket_path, f.directory.filePath("missing.txt"));
        QCOMPARE(result.code, 1);
        QVERIFY(!result.error.trimmed().isEmpty());

        QCoreApplication::sendPostedEvents(&f.window);
        QCOMPARE(f.documents()->count(), 1);
        QCOMPARE(f.button("Search")->isEnabled(), true);
        QCOMPARE(f.button("Inspect")->isEnabled(), true);
        QCOMPARE(f.button("Build Context")->isEnabled(), true);
        QCOMPARE(table->model()->rowCount(), 1);

        f.button("Search")->click();
        QCOMPARE(table->model()->rowCount(), 1);
    }

    void duplicate_add_keeps_current_corpus() {
        IpcFixture f;
        const QString path = f.file("first.txt");
        QVERIFY(f.window.add_document_file(path));
        f.button("Build Corpus")->click();
        QTRY_VERIFY(f.button("Inspect")->isEnabled());
        QCOMPARE(send_file(f.socket_path, path).code, 0);
        QTest::qWait(30);
        QCOMPARE(f.documents()->count(), 1);
        QVERIFY(f.button("Inspect")->isEnabled());
    }

    void request_split_across_writes_is_received() {
        IpcFixture f;
        const QByteArray path = f.file("split.txt").toUtf8();
        aiws::ipc::Socket socket(connect_to(f.socket_path));
        QVERIFY(socket.get() >= 0);
        const auto length = htonl(static_cast<std::uint32_t>(path.size()));
        const QByteArray header(reinterpret_cast<const char*>(&length), sizeof(length));
        for (int i = 0; i < header.size(); ++i) {
            QCOMPARE(::send(socket.get(), header.constData() + i, 1, 0), ssize_t(1));
            QTest::qSleep(20);
        }
        QCOMPARE(::send(socket.get(), path.constData(), 3, 0), ssize_t(3));
        QTest::qSleep(40);
        QCOMPARE(::send(socket.get(), path.constData() + 3, path.size() - 3, 0),
                 ssize_t(path.size() - 3));
        std::string reply;
        std::string error;
        QVERIFY(aiws::ipc::receive_message(socket.get(), reply,
            std::chrono::steady_clock::now() + std::chrono::seconds(2), nullptr, error));
        QCOMPARE(reply, std::string("0"));
        QCOMPARE(f.documents()->count(), 0);
        QTRY_COMPARE(f.documents()->count(), 1);
    }

    void remote_add_invalidates_current_corpus() {
        IpcFixture f;
        QVERIFY(f.window.add_document_file(f.file("first.txt")));
        f.button("Build Corpus")->click();
        QTRY_VERIFY(f.button("Inspect")->isEnabled());
        QCOMPARE(send_file(f.socket_path, f.file("second.txt", "banana")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 2);
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
    }

    void remote_add_during_build_rejects_old_corpus() {
        IpcFixture f;
        QVERIFY(f.window.add_document_file(f.file("first.txt")));
        f.window.set_build_delay_ms(1000);
        f.button("Build Corpus")->click();
        QCOMPARE(send_file(f.socket_path, f.file("second.txt", "banana")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 2);
        QVERIFY(!f.button("Inspect")->isEnabled());
        QTRY_VERIFY(f.button("Build Corpus")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        f.window.set_build_delay_ms(0);
        f.button("Build Corpus")->click();
        QTRY_VERIFY(f.button("Inspect")->isEnabled());
        f.window.findChild<aiws::gui::SearchWidget*>()->findChild<QLineEdit*>()->setText("banana");
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void accepted_file_contents_are_transferred_before_gui_runs() {
        IpcFixture f;
        const QString path = f.file("snapshot.txt", "original contents");
        QCOMPARE(send_file(f.socket_path, path).code, 0);
        f.file("snapshot.txt", "changed after reply");
        QTRY_COMPARE(f.documents()->count(), 1);
        bool found = false;
        for (auto* view : f.window.findChildren<QPlainTextEdit*>()) {
            if (view->toPlainText() == "original contents") {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void exit_stops_partial_client_and_removes_socket() {
        IpcFixture f;
        f.window.show();
        aiws::ipc::Socket client(connect_to(f.socket_path));
        QVERIFY(client.get() >= 0);
        const char first_header_byte = 0;
        QCOMPARE(::send(client.get(), &first_header_byte, 1, 0), ssize_t(1));
        f.window.set_exit_callback(&accept_exit);
        QElapsedTimer elapsed;
        elapsed.start();
        f.button("Exit")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.isVisible(), 450);
        QVERIFY(elapsed.elapsed() < 500);
        QVERIFY(!QFile::exists(f.socket_path));
        aiws::ipc::Socket stopped(connect_to(f.socket_path));
        QCOMPARE(stopped.get(), -1);
    }

    void exit_during_build_stops_both_workers() {
        IpcFixture f;
        f.window.show();
        QVERIFY(f.window.add_document_file(f.file("first.txt")));
        f.window.set_build_delay_ms(2000);
        f.button("Build Corpus")->click();
        f.window.set_exit_callback(&accept_exit);
        QElapsedTimer elapsed;
        elapsed.start();
        f.button("Exit")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.isVisible(), 450);
        QVERIFY(elapsed.elapsed() < 500);
        QVERIFY(!QFile::exists(f.socket_path));
    }

    void disconnected_client_does_not_stop_server() {
        IpcFixture f;
        {
            aiws::ipc::Socket client(connect_to(f.socket_path));
            QVERIFY(client.get() >= 0);
            const char byte = 0;
            QCOMPARE(::send(client.get(), &byte, 1, 0), ssize_t(1));
        }
        QCOMPARE(send_file(f.socket_path, f.file("after disconnect.txt")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 1);
    }

    void repeated_connections_deliver_and_release_each_client() {
        IpcFixture f;
        for (int i = 0; i < 12; ++i) {
            const QString path = f.file(QString("document-%1.txt").arg(i));
            aiws::ipc::Socket client(connect_to(f.socket_path));
            QVERIFY(client.get() >= 0);
            std::string error;
            std::string reply;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            QVERIFY(aiws::ipc::send_message(client.get(), path.toStdString(), deadline, nullptr, error));
            QVERIFY(aiws::ipc::receive_message(client.get(), reply, deadline, nullptr, error));
            QCOMPARE(reply, std::string("0"));
            QTRY_COMPARE(f.documents()->count(), i + 1);
        }
    }

    void existing_socket_is_not_removed_by_second_server() {
        IpcFixture f;
        aiws::gui::MainWindow second;
        QString error;
        QVERIFY(!second.start_ipc_server(f.socket_path, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(send_file(f.socket_path, f.file("still running.txt")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 1);
    }

    void malformed_messages_are_rejected_and_server_recovers() {
        IpcFixture f;
        const std::string messages[] = {"", "relative.txt", std::string("/bad\0name", 9)};
        for (const auto& message : messages) {
            aiws::ipc::Socket client(connect_to(f.socket_path));
            QVERIFY(client.get() >= 0);
            std::string error;
            std::string reply;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            QVERIFY(aiws::ipc::send_message(client.get(), message, deadline, nullptr, error));
            QVERIFY(aiws::ipc::receive_message(client.get(), reply, deadline, nullptr, error));
            QVERIFY(!reply.empty() && reply.front() == '1');
        }
        {
            aiws::ipc::Socket client(connect_to(f.socket_path));
            QVERIFY(client.get() >= 0);
            const auto length = htonl(std::uint32_t(1024 * 1024 + 1));
            QCOMPARE(::send(client.get(), &length, sizeof(length), 0), ssize_t(sizeof(length)));
            std::string error;
            std::string reply;
            QVERIFY(aiws::ipc::receive_message(client.get(), reply,
                std::chrono::steady_clock::now() + std::chrono::seconds(2), nullptr, error));
            QVERIFY(!reply.empty() && reply.front() == '1');
        }
        QCOMPARE(send_file(f.socket_path, f.file("after malformed.txt")).code, 0);
        QTRY_COMPARE(f.documents()->count(), 1);
    }

    void client_no_server_and_no_reply_exit_with_code_two() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("silent.sock");
        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(send_file(path, "missing.txt").code, 2);
        QVERIFY(elapsed.elapsed() < 2000);
        aiws::ipc::Socket listener(listen_without_reply(path));
        QVERIFY(listener.get() >= 0);
        elapsed.restart();
        const auto result = send_file(path, "missing.txt");
        QCOMPARE(result.code, 2);
        QVERIFY(result.error.contains("timed out"));
        QVERIFY(elapsed.elapsed() >= 4500);
        QVERIFY(elapsed.elapsed() < 5500);
    }

    void default_socket_uses_user_or_numeric_uid() {
        const bool had_user = qEnvironmentVariableIsSet("USER");
        const QByteArray old_user = qgetenv("USER");
        qputenv("USER", "m4-test-user");
        const auto named = aiws::ipc::default_socket_path();
        qunsetenv("USER");
        const auto numeric = aiws::ipc::default_socket_path();
        if (had_user) {
            qputenv("USER", old_user);
        } else {
            qunsetenv("USER");
        }
        QCOMPARE(named, std::string("/tmp/aiws-m4-test-user.sock"));
        QCOMPARE(numeric, "/tmp/aiws-" + std::to_string(::getuid()) + ".sock");
    }
};

QTEST_MAIN(IpcTests)
#include "ipc_tests.moc"
