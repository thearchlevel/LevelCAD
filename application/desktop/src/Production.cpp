#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/DocumentIO.hpp>
#include <levelcad/app/Production.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace levelcad::app {
namespace {
QString quoted(const std::string& text) {
    auto value = QString::fromStdString(text);
    value.replace("\\", "\\\\").replace("\"", "\\\"").replace('\n', ' ').replace('\r', ' ');
    return '"' + value + '"';
}

QString number(core::Coord value) {
    return QString::number(double(value) / core::nmPerMm, 'f', 6);
}

QString layer(int index) {
    return index == 0 ? "F.Cu" : index == 1 ? "B.Cu" : QString("In%1.Cu").arg(index - 1);
}

void save(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw std::runtime_error(("Falha ao salvar: " + path).toStdString());
}

QString csv(const std::string& text) {
    auto value = QString::fromStdString(text);
    value.replace("\"", "\"\"");
    return '"' + value + '"';
}

void run(const QString& tool, const QStringList& args) {
    QProcess process;
    process.start(tool, args);
    if (!process.waitForStarted(10000))
        throw std::runtime_error("Nao foi possivel iniciar o gerador de fabricacao.");
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        throw std::runtime_error("Tempo limite na geracao de fabricacao.");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(
            QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput())
                .toStdString());
}
} // namespace

QString productionTool() {
    auto path = QStandardPaths::findExecutable("kicad-cli");
#ifdef Q_OS_WIN
    if (path.isEmpty()) {
        QDir base(qEnvironmentVariable("ProgramFiles") + "/KiCad");
        auto versions =
            base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const auto& version : versions) {
            auto candidate = base.filePath(version + "/bin/kicad-cli.exe");
            if (QFileInfo::exists(candidate)) {
                path = candidate;
                break;
            }
        }
    }
#endif
    return path;
}

QString productionBoard(const pcb::Board& board) {
    if (board.width <= 0 || board.height <= 0 || board.thickness <= 0 || board.footprints.empty())
        throw std::runtime_error("Placa vazia ou dimensoes invalidas.");
    std::set<int> indices;
    for (const auto& l : board.layers)
        if (l.index < 0 || l.index > 31 || !indices.insert(l.index).second)
            throw std::runtime_error("Camadas invalidas ou duplicadas.");
    if (!indices.contains(0) || !indices.contains(1))
        throw std::runtime_error("Defina as camadas superior e inferior.");
    // Nao gere um contorno diferente daquele desenhado pelo usuario.
    if (!board.outline.empty())
        throw std::runtime_error("Exportacao de contorno personalizado ainda nao suportada.");
    std::map<std::string, int> nets;
    for (const auto& f : board.footprints)
        for (const auto& p : f.pads)
            if (!p.net.empty())
                nets.emplace(p.net, 0);
    int next = 1;
    for (auto& [name, id] : nets)
        id = next++;
    auto net = [&](const std::string& name) {
        auto found = nets.find(name);
        if (name.empty())
            return 0;
        if (found == nets.end())
            throw std::runtime_error("Cobre sem rede de pad correspondente.");
        return found->second;
    };
    QString output = "(kicad_pcb (version 20240108) (generator \"levelcad\")\n";
    output += " (general (thickness " + number(board.thickness) + ")) (paper \"A4\")\n";
    output += " (title_block (title " + quoted(board.projectName) + ") (rev " +
              quoted(board.revision) + ") (company " + quoted(board.creator) + "))\n (layers\n";
    output += " (0 \"F.Cu\" signal)\n";
    for (int index : indices)
        if (index >= 2)
            output +=
                QString(" (%1 %2 signal)\n").arg(index - 1).arg(quoted(layer(index).toStdString()));
    output += " (31 \"B.Cu\" signal)\n (34 \"B.Paste\" user)\n (35 \"F.Paste\" user)\n (36 "
              "\"B.SilkS\" user)\n (37 \"F.SilkS\" user)\n (38 \"B.Mask\" user)\n (39 \"F.Mask\" "
              "user)\n (44 \"Edge.Cuts\" user)\n )\n";
    output += " (net 0 \"\")\n";
    for (const auto& [name, id] : nets)
        output += QString(" (net %1 %2)\n").arg(id).arg(quoted(name));
    std::set<std::string> references;
    for (const auto& f : board.footprints) {
        if (f.pads.empty() || f.reference.empty() || !references.insert(f.reference).second)
            throw std::runtime_error("Footprint vazio ou referencia duplicada.");
        output += " (footprint " + quoted(f.source) + " (layer \"F.Cu\")";
        output += " (at " + number(f.position.x) + " " + number(f.position.y) + " " +
                  QString::number(-f.rotation, 'f', 6) + ")\n";
        output += " (fp_text reference " + quoted(f.reference) + " (at 0 " +
                  number(-f.length / 2 - 1000000) +
                  ") (layer \"F.SilkS\") (effects (font (size 0.8 0.8) (thickness 0.12))))\n";
        for (const auto& p : f.pads) {
            if (p.number <= 0 || p.drill < 0 || p.diameter <= 0 || !std::isfinite(p.rotation) ||
                !std::isfinite(p.cornerRatio) || p.cornerRatio < 0 || p.cornerRatio > .5)
                throw std::runtime_error("Pad invalido.");
            auto x = p.sizeX ? p.sizeX : p.diameter, y = p.sizeY ? p.sizeY : p.diameter;
            if (x <= 0 || y <= 0 || p.drill >= std::min(x, y))
                throw std::runtime_error("Furo ou tamanho de pad invalido.");
            if (p.shape != "circle" && p.shape != "rect" && p.shape != "roundrect" &&
                p.shape != "oval")
                throw std::runtime_error("Forma de pad nao suportada na fabricacao.");
            output += " (pad " + quoted(std::to_string(p.number)) + " " +
                      (p.drill > 0 ? "thru_hole" : "smd") + " " + QString::fromStdString(p.shape);
            output += " (at " + number(p.offset.x) + " " + number(p.offset.y) + " " +
                      QString::number(-f.rotation - p.rotation, 'f', 6) + ") (size " + number(x) +
                      " " + number(y) + ")";
            if (p.drill > 0)
                output += " (drill " + number(p.drill) + ") (layers \"*.Cu\" \"*.Mask\")";
            else if ((p.layerMask & 3u) == 1u)
                output += " (layers \"F.Cu\" \"F.Paste\" \"F.Mask\")";
            else if ((p.layerMask & 3u) == 2u)
                output += " (layers \"B.Cu\" \"B.Paste\" \"B.Mask\")";
            else
                throw std::runtime_error("Pad SMD precisa de uma unica camada externa.");
            if (p.shape == "roundrect")
                output += " (roundrect_rratio " + QString::number(p.cornerRatio) + ")";
            if (!p.net.empty())
                output += QString(" (net %1 %2)").arg(net(p.net)).arg(quoted(p.net));
            output += ")\n";
        }
        output += " )\n";
    }
    for (const auto& t : board.tracks) {
        if (!indices.contains(t.layer) || t.width <= 0 || t.from == t.to)
            throw std::runtime_error("Trilha invalida.");
        output += " (segment (start " + number(t.from.x) + " " + number(t.from.y) + ") (end " +
                  number(t.to.x) + " " + number(t.to.y) + ") (width " + number(t.width) +
                  ") (layer " + quoted(layer(t.layer).toStdString()) + ") (net " +
                  QString::number(net(t.net)) + "))\n";
    }
    for (const auto& v : board.vias) {
        if (v.drill <= 0 || v.diameter <= v.drill)
            throw std::runtime_error("Via invalida.");
        output += " (via (at " + number(v.position.x) + " " + number(v.position.y) + ") (size " +
                  number(v.diameter) + ") (drill " + number(v.drill) +
                  ") (layers \"F.Cu\" \"B.Cu\") (net " + QString::number(net(v.net)) + "))\n";
    }
    for (auto pair : std::vector<std::pair<core::Point, core::Point>>{
             {{0, 0}, {board.width, 0}},
             {{board.width, 0}, {board.width, board.height}},
             {{board.width, board.height}, {0, board.height}},
             {{0, board.height}, {0, 0}}})
        output += " (gr_line (start " + number(pair.first.x) + " " + number(pair.first.y) +
                  ") (end " + number(pair.second.x) + " " + number(pair.second.y) +
                  ") (stroke (width 0.05) (type default)) (layer \"Edge.Cuts\"))\n";
    return output + ")\n";
}

QString exportProduction(const pcb::Board& board, const core::Document& document,
                         const QString& destination, const QString& tool) {
    if (board.projectName.empty() || board.revision.empty() || board.creator.empty())
        throw std::runtime_error("Preencha projeto, revisao e autor.");
    auto issues = board.checkRules();
    if (!document.symbols().empty()) {
        auto audit = board.schematicIssues(document);
        issues.insert(issues.end(), audit.begin(), audit.end());
    }
    if (!issues.empty()) {
        QStringList lines;
        for (const auto& issue : issues)
            lines << QString::fromStdString(issue);
        throw std::runtime_error(lines.join('\n').toStdString());
    }
    auto source = productionBoard(board);
    if (tool.isEmpty() || !QFileInfo::exists(tool))
        throw std::runtime_error(
            "Gerador de fabricacao nao encontrado. Configure o executavel CLI.");
    QTemporaryDir stage(QDir(destination).filePath(".levelcad-production-XXXXXX"));
    if (!stage.isValid())
        throw std::runtime_error("Pasta de destino nao acessivel.");
    auto path = stage.filePath("board.kicad_pcb");
    save(path, source.toUtf8());
    // Uma segunda verificacao usa o motor do gerador sobre a placa de intercambio.
    // Isso tambem detecta erros que o DRC simplificado do editor ainda nao cobre.
    auto report = stage.filePath("drc-report.txt");
    try {
        run(tool, {"pcb", "drc", "--output", report, "--format", "report", "--severity-error",
                   "--exit-code-violations", path});
    } catch (const std::exception& error) {
        QFile file(report);
        if (file.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                ("DRC de fabricacao:\n" + QString::fromUtf8(file.readAll())).toStdString());
        throw;
    }
    QDir(stage.path()).mkdir("gerber");
    QDir(stage.path()).mkdir("drill");
    QStringList layers{"F.Cu"};
    for (const auto& l : board.layers)
        if (l.index >= 2)
            layers << layer(l.index);
    layers << "B.Cu" << "F.Mask" << "B.Mask" << "F.Paste" << "B.Paste" << "F.SilkS" << "B.SilkS"
           << "Edge.Cuts";
    run(tool, {"pcb", "export", "gerbers", "--output", stage.filePath("gerber/"), "--layers",
               layers.join(','), "--subtract-soldermask", path});
    run(tool,
        {"pcb", "export", "drill", "--output", stage.filePath("drill/"), "--format", "excellon",
         "--excellon-units", "mm", "--excellon-separate-th", "--generate-report", path});
    save(stage.filePath("board.levelpcb"), QJsonDocument(boardToJson(board)).toJson());
    auto project = documentToJson(document);
    project["pcb"] = boardToJson(board);
    save(stage.filePath("project.levelsch"), QJsonDocument(project).toJson());
    QString bom = "Reference,Value,Footprint\n",
            placement = "Reference,X_mm,Y_mm,Rotation_deg,Side\n";
    for (const auto& f : board.footprints) {
        bom += csv(f.reference) + "," + csv(f.value) + "," + csv(f.source) + "\n";
        placement += csv(f.reference) + "," + number(f.position.x) + "," + number(f.position.y) +
                     "," + QString::number(f.rotation) + ",Top\n";
    }
    save(stage.filePath("bom.csv"), bom.toUtf8());
    save(stage.filePath("placement.csv"), placement.toUtf8());
    QJsonObject manifest{
        {"project", QString::fromStdString(board.projectName)},
        {"revision", QString::fromStdString(board.revision)},
        {"creator", QString::fromStdString(board.creator)},
        {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"generator", "LevelCAD"},
        {"electricalAudit", document.symbols().empty() ? "PCB only; schematic unavailable"
                                                       : "pin and net correspondence checked"},
        {"units", "mm"},
        {"placementCoordinates", "LevelCAD: X right, Y down, clockwise degrees; top side"},
        {"releaseStatus",
         "Review Gerbers, drills and assembly orientation with the manufacturer before ordering"}};
    QJsonArray files;
    QDirIterator it(stage.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto p = it.next();
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error("Falha ao verificar arquivo gerado.");
        files.append(QJsonObject{
            {"path", QDir(stage.path()).relativeFilePath(p)},
            {"sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex())}});
    }
    if (files.size() < 8)
        throw std::runtime_error("Pacote de fabricacao incompleto.");
    manifest["files"] = files;
    save(stage.filePath("manifest.json"), QJsonDocument(manifest).toJson());
    auto final = QDir(destination)
                     .filePath("LevelCAD-production-" +
                               QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss") + "-" +
                               QUuid::createUuid().toString(QUuid::Id128).left(8));
    if (!QDir().rename(stage.path(), final))
        throw std::runtime_error("Falha ao concluir pacote de fabricacao.");
    stage.setAutoRemove(false);
    return final;
}
} // namespace levelcad::app