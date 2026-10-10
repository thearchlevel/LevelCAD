#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTranslator>
#include <QTreeWidget>
#include <QWidget>
#include <levelcad/app/Language.hpp>
#include <memory>

namespace levelcad::app {
namespace {
QJsonObject catalog;
QString selectedLanguage = "pt_BR";

class UiTranslator final : public QTranslator {
  public:
    bool isEmpty() const override {
        return false;
    }

    QString translate(const char*, const char* source, const char*, int) const override {
        const auto translations = catalog.value(QString::fromUtf8(source)).toArray();
        if (selectedLanguage == "en" && translations.size() == 2)
            return translations[0].toString();
        if (selectedLanguage == "es" && translations.size() == 2)
            return translations[1].toString();
        return {};
    }
};

std::unique_ptr<UiTranslator> translator;
std::unique_ptr<QTranslator> qtTranslator;

QString original(const QString& value) {
    if (catalog.contains(value))
        return value;
    for (auto entry = catalog.begin(); entry != catalog.end(); ++entry)
        for (const auto& translated : entry.value().toArray())
            if (translated.toString() == value)
                return entry.key();
    return value;
}

QString translated(const QString& source) {
    const auto values = catalog.value(source).toArray();
    if (values.size() != 2 || selectedLanguage == "pt_BR")
        return source;
    return values[selectedLanguage == "en" ? 0 : 1].toString();
}
} // namespace

QString uiLanguage() {
    return selectedLanguage;
}

QString translateUi(const QString& value) {
    return translated(original(value));
}

void setUiLanguage(const QString& selected, QWidget* root) {
    if (catalog.isEmpty()) {
        QFile file(":/translations/ui.json");
        if (file.open(QIODevice::ReadOnly))
            catalog = QJsonDocument::fromJson(file.readAll()).object();
    }
    const auto previous = selectedLanguage;
    selectedLanguage = selected == "en" || selected == "es" ? selected : "pt_BR";
    if (translator)
        QApplication::removeTranslator(translator.get());
    if (qtTranslator)
        QApplication::removeTranslator(qtTranslator.get());
    translator = std::make_unique<UiTranslator>();
    qtTranslator = std::make_unique<QTranslator>();
    QApplication::installTranslator(translator.get());
    if (qtTranslator->load("qtbase_" + selectedLanguage,
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(qtTranslator.get());
    QSettings().setValue("ui/selectedLanguage", selectedLanguage);
    // Guarde o texto de origem uma vez, para trocar de idioma sem acumular traduções.
    auto convert = [&](QObject* object, const char* property, const QString& value) {
        if (value.isEmpty())
            return value;
        const auto key = QByteArray("levelcad/source/") + property;
        if (!object->property(key).isValid())
            object->setProperty(key, original(value));
        return translated(object->property(key).toString());
    };
    for (auto* widget : root->findChildren<QWidget*>()) {
        widget->setWindowTitle(convert(widget, "title", widget->windowTitle()));
        if (auto* label = qobject_cast<QLabel*>(widget))
            label->setText(convert(label, "text", label->text()));
        if (auto* line = qobject_cast<QLineEdit*>(widget))
            line->setPlaceholderText(convert(line, "placeholder", line->placeholderText()));
        if (auto* tree = qobject_cast<QTreeWidget*>(widget)) {
            for (int column = 0; column < tree->columnCount(); ++column)
                tree->headerItem()->setText(column,
                                            translated(original(tree->headerItem()->text(column))));
            QTreeWidgetItemIterator iterator(tree);
            while (*iterator) {
                for (int column = 0; column < tree->columnCount(); ++column)
                    (*iterator)->setText(column, translated(original((*iterator)->text(column))));
                ++iterator;
            }
        }
        if (auto* list = qobject_cast<QListWidget*>(widget)) {
            for (int index = 0; index < list->count(); ++index)
                list->item(index)->setText(translated(original(list->item(index)->text())));
        }
        if (auto* combo = qobject_cast<QComboBox*>(widget))
            for (int index = 0; index < combo->count(); ++index)
                combo->setItemText(index, translated(original(combo->itemText(index))));
    }
    for (auto* action : root->findChildren<QAction*>())
        action->setText(convert(action, "action", action->text()));
    root->update();
    (void)previous;
}
} // namespace levelcad::app
