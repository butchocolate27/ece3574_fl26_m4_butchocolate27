#include "aiws/build_controller.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>

using aiws::BuildController;
using aiws::BuildOutcome;
using aiws::ProcessingCore;
using aiws::Workspace;
using aiws::gui::MainWindow;
using aiws::gui::SearchWidget;
using aiws::gui::WorkspaceWidget;

namespace {

class BuildEvents final : public QObject {
    Q_OBJECT

public:
    explicit BuildEvents(BuildController& builds)
        : starts(this, &BuildEvents::started), finishes(this, &BuildEvents::finished) {
        connect(&builds, &BuildController::started, this, &BuildEvents::started,
                Qt::QueuedConnection);
        connect(&builds, &BuildController::finished, this, &BuildEvents::finished,
                Qt::QueuedConnection);
    }

    bool next_start(quint64 version) {
        if (starts.isEmpty() && !starts.wait(2000)) {
            return false;
        }
        return starts.takeFirst().front().toULongLong() == version;
    }

    bool next_finish(BuildOutcome* result = nullptr, int timeout = 2000) {
        if (finishes.isEmpty() && !finishes.wait(timeout)) {
            return false;
        }
        auto outcome = finishes.takeFirst().front().value<BuildOutcome>();
        if (result) {
            *result = std::move(outcome);
        }
        return true;
    }

    QSignalSpy starts;
    QSignalSpy finishes;

signals:
    void started(quint64 version);
    void finished(aiws::BuildOutcome outcome);
};

// Coordinate event order
class TestLatch final : public QObject {
public:
    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_one();
    }

    bool wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2),
                                 [this] { return released_; });
    }

    void hold_first(quint64 version) {
        if (version == 1) {
            wait();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool released_{false};
};

struct WindowFixture {
    explicit WindowFixture(BuildController::BuildTask task = nullptr)
        : WindowFixture(std::make_unique<BuildController>(std::move(task))) {}

    BuildController* builds;
    MainWindow window;
    BuildEvents events;

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
        : builds(controller.get()), window(std::move(controller)), events(*builds) {}
};

void accept_exit(QWidget* parent) {
    parent->close();
}

void decline_exit(QWidget*) {}

std::unique_ptr<ProcessingCore> plain_build(const Workspace& workspace) {
    auto core = std::make_unique<ProcessingCore>();
    core->rebuild(workspace);
    return core;
}

// Inject build failures
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

std::unique_ptr<ProcessingCore> count_worker_builds(
    const Workspace& workspace, int, const std::atomic<bool>&) {
    thread_local int builds_on_this_thread = 0;
    test_attempts.store(++builds_on_this_thread);
    return plain_build(workspace);
}

int idle_exit_after_build(const QString& file) {
    WindowFixture f;
    f.window.show();
    if (!f.window.add_document_file(file)) {
        return 1;
    }
    f.button("Build Corpus")->click();
    if (!f.events.next_finish() || !f.button("Inspect")->isEnabled()) {
        return 1;
    }
    f.window.set_exit_callback(&accept_exit);
    f.button("Exit")->click();
    return f.window.isVisible() ? 1 : 0;
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

        // Bound idle shutdown
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--idle-exit", first_});
        QVERIFY2(child.waitForStarted(2000), qPrintable(child.errorString()));
        const bool exited = child.waitForFinished(4000);
        if (!exited) {
            child.kill();
            child.waitForFinished(1000);
        }
        QVERIFY2(exited, "idle_exit_after_build: Exit hung with an idle worker");
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
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
    }

    void duplicate_document_build_fails_and_reset_recovers() {
        WindowFixture f;
        f.query()->setText("apple");
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        QVERIFY(f.button("Search")->isEnabled());

        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
        QVERIFY(f.status()->text().contains("duplicate document id"));
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QCOMPARE(f.window.findChild<QListWidget*>()->count(), 2);
        QVERIFY(f.button("Build Corpus")->isEnabled());

        f.button("Reset")->click();
        QVERIFY(f.window.add_document_file(second_));
        f.query()->setText("banana");
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void builds_whole_workspace_and_preserves_m3_features() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        QVERIFY(f.window.add_document_file(second_));
        f.query()->setText("banana");
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
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
        QVERIFY(f.events.next_start(2));
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        QVERIFY(f.button("Add Document")->isEnabled());
        QVERIFY(f.button("Reset")->isEnabled());
        QVERIFY(f.button("Exit")->isEnabled());
        f.query()->setText("apple");
        f.window.findChild<WorkspaceWidget*>()->select_document(0);
        QVERIFY(ticks.wait(150));
        QVERIFY(!f.button("Inspect")->isEnabled());
        QCOMPARE(f.query()->text(), QString("apple"));
        QVERIFY(f.events.next_finish());
        QVERIFY(elapsed.elapsed() >= 500);
        QVERIFY(f.button("Search")->isEnabled());
    }

    void add_during_build_cancels_then_rebuilds() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(1000);
        f.query()->setText("banana");
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_start(1));
        QVERIFY(f.window.add_document_file(second_));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(f.events.next_finish());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Search")->isEnabled());
        f.window.set_build_delay_ms(0);
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void reset_during_build_leaves_empty_workspace() {
        WindowFixture f;
        QVERIFY(f.window.add_document_file(first_));
        f.window.set_build_delay_ms(1000);
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_start(1));
        f.button("Reset")->click();
        QCOMPARE(f.window.findChild<QListWidget*>()->count(), 0);
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(f.events.next_finish());
        QVERIFY(!f.button("Build Corpus")->isEnabled());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
    }

    void finished_build_before_workspace_change_data() {
        QTest::addColumn<bool>("reset");
        QTest::newRow("add") << false;
        QTest::newRow("reset and re-add") << true;
    }

    void finished_build_before_workspace_change() {
        QFETCH(bool, reset);
        TestLatch ready;
        WindowFixture f;
        connect(f.builds, &BuildController::finished, &ready, &TestLatch::release,
                Qt::DirectConnection);
        QVERIFY(f.window.add_document_file(first_));
        f.query()->setText("apple");
        f.button("Build Corpus")->click();
        QVERIFY(ready.wait());
        if (reset) {
            f.button("Reset")->click();
        }
        QVERIFY(f.window.add_document_file(second_));
        if (reset) {
            f.button("Build Corpus")->click();
        }
        BuildOutcome old;
        QVERIFY(f.events.next_finish(&old));
        QCOMPARE(old.workspace_version, std::uint64_t(1));
        QVERIFY(old.corpus);
        if (!reset) {
            QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
            QVERIFY(!f.button("Search")->isEnabled());
            QVERIFY(!f.button("Inspect")->isEnabled());
            QVERIFY(!f.button("Build Context")->isEnabled());
            f.button("Build Corpus")->click();
        }
        BuildOutcome current;
        QVERIFY(f.events.next_finish(&current));
        QVERIFY(current.workspace_version > old.workspace_version);
        f.query()->setText("banana");
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void reset_rebuild_skips_remaining_documents() {
        WindowFixture f;
        for (int i = 0; i < 10; ++i) {
            const QString name = QString("old-%1.txt").arg(i);
            QVERIFY(f.window.add_document_file(write_file(name, "old corpus")));
        }
        f.window.set_build_delay_ms(200);
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_start(10));
        QDeadlineTimer deadline(650, Qt::PreciseTimer);
        f.button("Reset")->click();
        QVERIFY(f.window.add_document_file(second_));
        f.window.set_build_delay_ms(0);
        f.query()->setText("banana");
        QVERIFY(f.button("Build Corpus")->isEnabled());
        f.button("Build Corpus")->click();
        BuildOutcome old;
        BuildOutcome current;
        const bool replaced = f.events.next_finish(&old, deadline.remainingTime())
                           && f.events.next_finish(&current, deadline.remainingTime());
        QVERIFY2(replaced && !deadline.hasExpired() && f.button("Search")->isEnabled(),
                 "The replacement corpus was not searchable promptly after Reset");
        QCOMPARE(old.workspace_version, std::uint64_t(10));
        QVERIFY(current.workspace_version > old.workspace_version);
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void failure_is_reported_and_retry_works() {
        test_attempts.store(0);
        WindowFixture f(&fail_first_build);
        QVERIFY(f.window.add_document_file(first_));
        f.query()->setText("apple");
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        QVERIFY(f.status()->text().contains("Build failed: test build failed"));
        QVERIFY(f.button("Build Corpus")->isEnabled());
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
        QCOMPARE(f.corpus_status(), QString("Corpus: Not built"));
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        QVERIFY(f.button("Search")->isEnabled());
        QVERIFY(f.button("Inspect")->isEnabled());
        QVERIFY(f.button("Build Context")->isEnabled());
        QCOMPARE(test_attempts.load(), 2);

        // Failure after success
        test_attempts.store(0);
        f.button("Build Corpus")->click();
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(f.events.next_finish());
        QVERIFY(f.status()->text().contains("Build failed: test build failed"));
        QVERIFY(!f.button("Search")->isEnabled());
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(!f.button("Build Context")->isEnabled());
        QVERIFY(f.button("Build Corpus")->isEnabled());
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        f.button("Search")->click();
        QCOMPARE(f.window.findChild<QTableView*>()->model()->rowCount(), 1);
    }

    void unknown_failure_is_reported() {
        WindowFixture f(&unknown_failure);
        QVERIFY(f.window.add_document_file(first_));
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_finish());
        QVERIFY(f.status()->text().contains("Unknown build failure"));
        QVERIFY(f.button("Build Corpus")->isEnabled());
    }

    void one_worker_builds_only_the_latest_request() {
        Workspace workspace;
        workspace.add_document(aiws::Document("a", "A", "apple"));
        TestLatch release;
        BuildController builds(&count_worker_builds);
        BuildEvents events(builds);
        connect(&builds, &BuildController::started, &release, &TestLatch::hold_first,
                Qt::DirectConnection);
        builds.start(workspace, 1, 0);
        QVERIFY(events.next_start(1));
        builds.start(workspace, 2, 0);
        builds.start(workspace, 3, 0);
        release.release();
        BuildOutcome first;
        QVERIFY(events.next_finish(&first));
        QCOMPARE(first.workspace_version, std::uint64_t(1));
        QVERIFY(events.next_start(3));
        BuildOutcome latest;
        QVERIFY(events.next_finish(&latest));
        QCOMPARE(latest.workspace_version, std::uint64_t(3));
        QVERIFY(latest.corpus);
        QCOMPARE(test_attempts.load(), 2);
        QVERIFY(events.starts.isEmpty());
    }

    void default_worker_matches_processing_core() {
        Workspace workspace;
        std::string text;
        for (int i = 0; i < 250; ++i) {
            text += "apple engine ";
        }
        workspace.add_document(aiws::Document("a", "A", text));

        std::unique_ptr<ProcessingCore> reference;
        bool should_fail = false;
        try {
            reference = std::make_unique<ProcessingCore>();
            reference->rebuild(workspace);
        } catch (const std::exception&) {
            should_fail = true;
        }

        BuildController builds;
        BuildEvents events(builds);
        builds.start(workspace, 1, 0);
        BuildOutcome result;
        QVERIFY(events.next_finish(&result));
        if (should_fail) {
            QVERIFY(!result.error.empty());
            QVERIFY(!result.corpus);
        } else {
            QVERIFY(result.error.empty());
            QVERIFY(result.corpus);
            QCOMPARE(result.corpus->chunk_count(), reference->chunk_count());
            for (std::size_t i = 0; i < reference->chunk_count(); ++i) {
                QCOMPARE(result.corpus->chunks()[i].text, reference->chunks()[i].text);
                QCOMPARE(result.corpus->chunks()[i].token_count,
                         reference->chunks()[i].token_count);
            }
        }
    }

    void worker_uses_private_snapshot_and_transfers_ownership() {
        Workspace workspace;
        workspace.add_document(aiws::Document("a", "A", "apple"));
        std::shared_ptr<const ProcessingCore> transferred;
        {
            BuildController builds;
            BuildEvents events(builds);
            builds.start(workspace, 17, 20);
            workspace.add_document(aiws::Document("b", "B", "banana"));
            BuildOutcome result;
            QVERIFY(events.next_finish(&result));
            QCOMPARE(result.workspace_version, std::uint64_t(17));
            QVERIFY(result.error.empty());
            transferred = std::move(result.corpus);
            QVERIFY(transferred);
        }
        QCOMPARE(transferred->chunk_count(), std::size_t(1));
        QVERIFY(transferred->search("banana", 5).empty());
        QCOMPARE(transferred->search("apple", 5).size(), std::size_t(1));
    }

    void exit_stops_active_build_promptly() {
        WindowFixture f;
        f.window.show();
        QVERIFY(f.window.add_document_file(first_));
        QVERIFY(f.window.add_document_file(second_));
        f.window.set_build_delay_ms(500);
        f.button("Build Corpus")->click();
        QVERIFY(f.events.next_start(2));
        f.window.set_exit_callback(&accept_exit);
        QElapsedTimer elapsed;
        elapsed.start();
        f.button("Exit")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.isVisible(), 700);
        QVERIFY(elapsed.elapsed() < 750);
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
        QVERIFY(!f.button("Inspect")->isEnabled());
        QVERIFY(f.events.next_finish());
        QVERIFY(f.button("Inspect")->isEnabled());
        f.window.close();
    }

    void repeated_build_reset_cycles_release_each_job() {
        WindowFixture f;
        for (int i = 0; i < 12; ++i) {
            QVERIFY(f.window.add_document_file(first_));
            f.button("Build Corpus")->click();
            QVERIFY(f.events.next_finish());
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
        {
            BuildController builds;
            BuildEvents events(builds);
            builds.start(workspace, 1, 10000);
            QVERIFY(events.next_start(1));
            elapsed.start();
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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 3 && args[1] == "--idle-exit") {
        return idle_exit_after_build(args[2]);
    }
    StudentTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "student_tests.moc"
