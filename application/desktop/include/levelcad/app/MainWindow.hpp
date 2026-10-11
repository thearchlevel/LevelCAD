#pragma once
#include <QMainWindow>
#include <levelcad/app/BoardEditor.hpp>
#include <levelcad/app/Collaboration.hpp>
#include <levelcad/app/ModelView.hpp>
#include <levelcad/app/SchematicView.hpp>
#include <levelcad/app/SymbolLibrary.hpp>
#include <levelcad/core/Document.hpp>
#include <vector>
class QTreeWidget;
class QListWidget;
class QLabel;
class QAction;
class QCloseEvent;
class QMenu;
class QMdiArea;
class QMdiSubWindow;
class QProcess;
class QComboBox;
class QTimer;

namespace levelcad::app {
class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(std::function<void(int,const QString&)> progress = {});
    ~MainWindow() override;

    void openProject(const QString& file) {
        openDocument(file);
    }

  protected:
    void closeEvent(QCloseEvent*) override;

  private:
    bool confirmDiscard();
    void synchronizeBoard(bool reposition=false);
    void setupAutomation();
    void reviewProposal(const QJsonObject& proposal,std::uint64_t revision);
    void managePlugins();
    std::uint64_t designRevision_{};
    bool syncing_{}, autoSync_{};
    QTimer* syncTimer_{};
    void hostProject();
    void rebuildBoardPanels();
    void stopHosting();
    void newDocument();
    void openDocument(const QString& file = QString());
    bool saveDocumentAs();
    bool saveDocument();
    void updateTitle();
    void rebuildTree();
    void recordChange();
    void resetHistory();
    void undo();
    void redo();
    void exportBom();
    void exportNetlist();
    void exportSpice();
    void importLibrary();
    void appendLibrary(const std::vector<LibraryEntry>& entries);
    void runErc();
    void rebuildRecent();
    void rememberFile();
    void setupWorkspace();
    void showWorkspace(int index);
    void manageWindows();
    void restoreLayout();
    CollaborationClient* collaboration_{};
    QProcess* serverProcess_{};
    bool hosting_{};
    quint16 hostedPort_{};
    QString hostedDirectory_;
    QString hostToken_;
    QTreeWidget* boardLayers_{};
    QComboBox* copperLayers_{};
    QComboBox* boardNets_{};
    core::Document document_;
    pcb::Board board_;
    QMdiArea* workspace_{};
    std::vector<QMdiSubWindow*> editorWindows_;
    BoardEditor* boardView_{};
    ModelView* boardModel_{};
    ModelView* schematicModel_{};
    QByteArray defaultLayout_;
    SchematicView* view_{};
    QTreeWidget* tree_{};
    QListWidget* libraryList_{};
    QLabel* libraryCount_{};
    std::vector<LibraryEntry> libraryEntries_;
    QAction* undoAction_{};
    QAction* redoAction_{};
    QMenu* recentMenu_{};
    QString path_;
    std::vector<core::Document> history_;
    std::vector<pcb::Board> boardHistory_;
    int historyIndex_{}, savedIndex_{};
};
} // namespace levelcad::app
