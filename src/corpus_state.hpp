#pragma once

#include "aiws/gui/main_window.hpp"

namespace aiws::gui {

class MainWindow::CorpusState {
public:
    using Next = std::unique_ptr<CorpusState>;

    virtual ~CorpusState() = default;
    static Next initial();

    virtual Next build_requested(MainWindow&) const { return {}; }
    virtual Next document_added(MainWindow& window) const;
    virtual Next reset(MainWindow& window) const;
    virtual Next build_finished(MainWindow&, BuildOutcome&) const { return {}; }
    virtual Next close(MainWindow& window) const;

    virtual bool accepts_documents() const { return true; }
    virtual bool can_build() const { return false; }
    virtual bool has_corpus() const { return false; }

private:
    class Empty;
    class Dirty;
    class Building;
    class Current;
    class Closing;
    static Next start_build(MainWindow& window);
};

}  // namespace aiws::gui
