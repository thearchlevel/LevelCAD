#include <QFileDialog>
#include <QRegularExpression>
#include <levelcad/app/ExportDialog.hpp>

namespace levelcad::app {
QString chooseExportFile(QWidget* parent, const QString& title, const QString& suggestedPath,
                         const QString& filter) {
    QFileDialog dialog(parent, title, suggestedPath, filter);
    // A ação é explícita, mesmo quando o diálogo nativo do sistema usa rótulos inconsistentes.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setLabelText(QFileDialog::Accept, QObject::tr("Exportar"));
    const auto suffix = QRegularExpression("\\*\\.([a-zA-Z0-9]+)").match(filter);
    if (suffix.hasMatch())
        dialog.setDefaultSuffix(suffix.captured(1));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    return dialog.selectedFiles().front();
}
} // namespace levelcad::app
