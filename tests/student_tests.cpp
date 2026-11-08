#include "aiws/build_controller.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

using aiws::BuildController;
using aiws::ProcessingCore;
using aiws::Workspace;
using aiws::gui::MainWindow;
using aiws::gui::SearchWidget;
using aiws::gui::WorkspaceWidget;

namespace {

struct WindowFixture {
    explicit WindowFixture(BuildController::BuildTask task = nullptr)
        : WindowFixture(std::make_unique<BuildController>(std::move(task))) {}

    BuildController* builds;
    MainWindow window;

    QPushButton* button(const QString& text) {
        for (auto* candidate : window.findChildren<QPushButton*>()) {
            if (candidate->text() == text) {
                return candidate;
            }
        }
        qFatal("Missing application button");
        return nullptr;
    }

    QLabel* status() {
        return window.findChild<QLabel*>("applicationStatus");
    }

    QString corpus_status() {
        auto* workspace = window.findChild<WorkspaceWidget*>();
        for (auto* label : workspace->findChildren<QLabel*>()) {
            if (label->text().startsWith("Corpus:")) {
                return label->text();
            }
        }
        return {};
    }

    QLineEdit* query() {
        return window.findChild<SearchWidget*>()->findChild<QLineEdit*>();
    }

private:
    explicit WindowFixture(std::unique_ptr<BuildController> controller)
        : builds(controller.get()), window(std::move(controller)) {}
};

// Do not pump GUI events here: the result must be ready but uncollected before
// Add/Reset. This fixes the ordering without relying on a timer race.
bool wait_for_ready(BuildController& builds) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!builds.ready() && elapsed.elapsed() < 3000) {
        QTest::qSleep(1);
    }
    return builds.ready();
}

void accept_exit(QWidget* parent) {
    parent->close();
}

void decline_exit(QWidget*) {}

std::unique_ptr<ProcessingCore> plain_build(const Workspace& workspace) {
    auto core = std::make_unique<ProcessingCore>();
    core->rebuild(workspace);
    return core;
}

// These functions stand in for a failing or slow build. Function pointers
// let the tests exercise the same controller and GUI path as the real build.
std::atomic<int> test_attempts{0};

std::unique_ptr<ProcessingCore> fail_first_build(
    const Workspace& workspace, int, const std::atomic<bool>&) {
    if (test_attempts.fetch_add(1) == 0) {
        throw std::runtime_error("test build failed");
    }
    return plain_build(workspace);
}

std::unique_ptr<ProcessingCore> unknown_failure(
    const Workspace&, int, const std::atomic<bool>&) {
    throw 42;
}

std::unique_ptr<ProcessingCore> wait_for_cancellation(
    const Workspace& workspace, int, const std::atomic<bool>& cancel) {
    test_attempts.fetch_add(1);
    while (!cancel.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return plain_build(workspace);
}

}  // namespace

class StudentTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        first_ = write_file("first.txt", "apple apple engine. A responsive workspace.");
        second_ = write_file(QString::fromUtf8("second 文档\nfile.txt"),
                             "banana systems. Another document.");
        QVERIFY(!first_.isEmpty());
        QVERIFY(!second_.isEmpty());
    }

    void initial_state_and_file_validation() {
        WindowFixture f;
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
        const QString status = f.status()->text();
        QVERIFY(!f.window.add_document_file(directory_.filePath("missing.txt")));
        QVERIFY(!f.window.add_document_file(directory_.path()));
        QCOMPARE(f.status()->text(), status);
        QVERIFY(f.window.add_document_file(first_));
        QVERIFY(f.button("Build Corpus")->isEnabled());
        auto* list = f.window.findChild<QListWidget*>();
        QCOMPARE(list->count(), 1);
        QVERIFY(f.window.add_document_file(first_));
        QCOMPARE(list->count(), 1);
    }

    void builds_whole_workspace_and_preserves_m3_features() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        QVERIFY(f.window.add_document_file(second_));
        f.query()->setText("banana");
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 3000);
        QCOMPARE(f.corpus_status(), QString("Corpus: 2 chunks"));
        QVERIFY(f.button("Search")->isEnabled());
        QVERIFY(f.button("Inspect")->isEnabled());
        QVERIFY(f.button("Build Context")->isEnabled());
        f.button("Search")->click();
        auto* table = f.window.findChild<QTableView*>();
        QCOMPARE(table->model()->rowCount(), 1);
        table->selectRow(0);
        f.button("View Source")->click();
        QCOMPARE(f.window.findChild<QListWidget*>()->currentRow(), 1);
        auto* inspector = f.window.findChild<aiws::gui::TermInspectorWidget*>();
        inspector->findChild<QLineEdit*>()->setText("banana");
        f.button("Inspect")->click();
        QCOMPARE(f.status()->text(), QString("Term inspected."));
        f.button("Build Context")->click();
        const auto* context = f.window.findChild<aiws::gui::ContextWidget*>();
        QVERIFY(context->findChild<QPlainTextEdit*>()->toPlainText().contains("banana"));
        const QString before = f.status()->text();
        QVERIFY(!f.window.add_document_file(directory_.filePath("unreadable.txt")));
        QCOMPARE(f.status()->text(), before);
        QVERIFY(f.button("Search")->isEnabled());
    }

    void slow_build_keeps_gui_active() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        QVERIFY(f.window.add_document_file(second_));
        f.window.set_build_delay_ms(250);
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QSignalSpy ticks(&heartbeat, &QTimer::timeout);
        heartbeat.start();
        QElapsedTimer elapsed;
        elapsed.start();
        f.button("Build Corpus")->click();
        QVERIFY(elapsed.elapsed() < 150);
        QVERIFY(f.builds->busy());
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        QVERIFY(f.button("Add Document")->isEnabled());
        QVERIFY(f.button("Reset")->isEnabled());
        QVERIFY(f.button("Exit")->isEnabled());
        f.query()->setText("apple");
        f.window.findChild<WorkspaceWidget*>()->select_document(0);
        QTest::qWait(100);
        QVERIFY(ticks.count() >= 3);
        QVERIFY(f.builds->busy());
        QCOMPARE(f.query()->text(), QString("apple"));
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 3000);
        QVERIFY(elapsed.elapsed() >= 500);
        QVERIFY(f.button("Search")->isEnabled());
    }

    void add_during_build_cancels_then_rebuilds() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(1000);
        f.query()->setText("banana");
        f.button("Build Corpus")->click();
        QVERIFY(f.window.add_document_file(second_));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 2000);
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Search")->isEnabled());
        f.window.set_build_delay_ms(0);
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 2000);
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void reset_during_build_leaves_empty_workspace() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(1000);
        f.button("Build Corpus")->click();
        f.button("Reset")->click();
        QCOMPARE(f.window.findChild<QListWidget*>()->count(), 0);
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 2000);
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
    }

    void finished_build_after_add_is_discarded() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.query()->setText("apple");
        f.button("Build Corpus")->click();
        QVERIFY(wait_for_ready(*f.builds));
        QVERIFY(f.window.add_document_file(second_));
        QVERIFY(QMetaObject::invokeMethod(&f.window, "poll_build", Qt::DirectConnection));
        QVERIFY(!f.builds->busy());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
    }

    void finished_build_after_reset_is_discarded() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(wait_for_ready(*f.builds));
        f.button("Reset")->click();
        QVERIFY(QMetaObject::invokeMethod(&f.window, "poll_build", Qt::DirectConnection));
        QVERIFY(!f.builds->busy());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Inspect")->isEnabled());
    }

    void reset_and_readd_cannot_reuse_old_version() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(wait_for_ready(*f.builds));
        f.button("Reset")->click();
        QVERIFY(f.window.add_document_file(second_));
        QVERIFY(QMetaObject::invokeMethod(&f.window, "poll_build", Qt::DirectConnection));
        QVERIFY(!f.button("Inspect")->isEnabled());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 2000);
        f.query()->setText("banana");
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void failure_is_reported_and_retry_works() {
        test_attempts.store(0);
        WindowFixture f(&fail_first_build);
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 1500);
        QVERIFY(f.status()->text().contains("Build failed: test build failed"));
        QVERIFY(f.button("Build Corpus")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 1500);
        QVERIFY(f.button("Inspect")->isEnabled());
        QCOMPARE(test_attempts.load(), 2);
    }

    void unknown_failure_is_reported() {
        WindowFixture f(&unknown_failure);
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 1500);
        QVERIFY(f.status()->text().contains("Unknown build failure"));
        QVERIFY(f.button("Build Corpus")->isEnabled());
    }

    void repeated_build_requests_do_not_overlap() {
        test_attempts.store(0);
        WindowFixture f(&wait_for_cancellation);
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        for (int i = 0; i < 10; ++i) {
            QVERIFY(QMetaObject::invokeMethod(&f.window, "build_corpus", Qt::DirectConnection));
        }
        f.button("Reset")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 1500);
        QCOMPARE(test_attempts.load(), 1);
    }

    void worker_uses_private_snapshot_and_transfers_ownership() {
        Workspace workspace;
        workspace.add_document(aiws::Document("a", "A", "apple"));
        std::unique_ptr<ProcessingCore> transferred;
        {
            BuildController builds;
            QVERIFY(builds.start(workspace, 17, 20));
            workspace.add_document(aiws::Document("b", "B", "banana"));
            QVERIFY(!builds.start(workspace, 18, 0));
            QVERIFY(wait_for_ready(builds));
            aiws::BuildOutcome result;
            QVERIFY(builds.poll(result));
            QCOMPARE(result.workspace_version, std::uint64_t(17));
            QVERIFY(result.error.empty());
            transferred = std::move(result.corpus);
            QCOMPARE(transferred->chunk_count(), std::size_t(1));
            QVERIFY(transferred->search("banana", 5).empty());
            QVERIFY(!builds.busy());
        }
        // The completed corpus does not borrow its former controller's flag.
        transferred->rebuild(workspace);
        QCOMPARE(transferred->chunk_count(), std::size_t(2));
    }

    void close_during_build_drains_without_blocking_gui() {
        WindowFixture f;
        f.window.show();
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(2000);
        f.button("Build Corpus")->click();
        f.window.set_exit_callback(&accept_exit);
        QElapsedTimer elapsed;
        elapsed.start();
        f.button("Exit")->click();
        QVERIFY(elapsed.elapsed() < 150);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.isVisible(), 1500);
        QVERIFY(!f.builds->busy());
    }

    void decline_exit_keeps_build_and_window_alive() {
        WindowFixture f;
        f.window.show();
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(200);
        f.window.set_exit_callback(&decline_exit);
        f.button("Build Corpus")->click();
        f.button("Exit")->click();
        QVERIFY(f.window.isVisible());
        QVERIFY(f.builds->busy());
        QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 2000);
        QVERIFY(f.button("Inspect")->isEnabled());
        f.window.close();
    }

    void repeated_build_reset_cycles_release_each_job() {
        WindowFixture f;
        for (int i = 0; i < 12; ++i) {
            QVERIFY(f.window.add_document_file(first_));
            f.button("Build Corpus")->click();
            QTRY_VERIFY_WITH_TIMEOUT(!f.builds->busy(), 1500);
            QVERIFY(f.button("Inspect")->isEnabled());
            f.button("Reset")->click();
            QVERIFY(!f.button("Inspect")->isEnabled());
            QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 0);
        }
    }

    void direct_destruction_cancels_and_joins() {
        Workspace workspace;
        workspace.add_document(aiws::Document("a", "A", "apple"));
        QElapsedTimer elapsed;
        elapsed.start();
        {
            BuildController builds;
            QVERIFY(builds.start(workspace, 1, 10000));
        }
        QVERIFY(elapsed.elapsed() < 500);
    }

private:
    QString write_file(const QString& name, const QByteArray& bytes) {
        const QString path = directory_.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return {};
        }
        return path;
    }

    QTemporaryDir directory_;
    QString first_;
    QString second_;
};

QTEST_MAIN(StudentTests)
#include "student_tests.moc"
