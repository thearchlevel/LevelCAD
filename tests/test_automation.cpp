#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <levelcad/app/AiAssistant.hpp>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/CadCommands.hpp>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/PluginEngine.hpp>
#include <levelcad/pcb/Footprints.hpp>
#include <stdexcept>
using namespace levelcad;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        core::Document schematic;
        auto a=schematic.addSymbol(core::SymbolKind::Resistor,{0,0});
        auto b=schematic.addSymbol(core::SymbolKind::Resistor,{400000000,100000000});
        schematic.addWire({3000000,0},{397000000,0});schematic.addWire({397000000,0},{397000000,100000000});
        pcb::Board board; auto report=board.synchronize(schematic,{true,true});
        require(report.added==2 && board.footprints.size()==2,"two physical components");
        for(const auto& f:board.footprints) for(const auto& p:f.pads) {
            auto at=board.padPosition(f,p);
            require(at.x>=p.diameter/2 && at.x<=board.width-p.diameter/2 && at.y>=p.diameter/2 && at.y<=board.height-p.diameter/2,"schematic arrangement must fit the PCB");
        }
        require(board.footprints[0].position.x<board.footprints[1].position.x,"preserve schematic order");
        auto original=board.footprints[0];
        auto mapped=pcb::makeFootprint(schematic.symbols()[0],"LevelCAD:R_0603");require(mapped.has_value(),"SMD package mapping");
        for(auto& p:mapped->pads) for(const auto& old:original.pads) if(old.number==p.number) p.net=old.net;
        mapped->position=original.position;mapped->rotation=90;
        app::assignAutomaticModel(*mapped);
        board.footprints[0]=*mapped;
        board.layers.push_back({2,"Inner power","#ab88ff",true});board.solderMask="#223366";
        const auto meshAddress=&board.footprints[0].mesh[0];
        auto history=board;
        require(&history.footprints[0].mesh[0]==meshAddress,"history must share immutable model geometry");
        report=board.synchronize(schematic);
        require(board.footprints[0].source=="LevelCAD:R_0603" && board.footprints[0].pads[0].drill==0 && board.footprints[0].rotation==90,"synchronization preserves SMD mapping and rotation");
        require(board.layers.size()==3 && board.solderMask=="#223366" && !board.footprints[0].mesh.empty(),"preserve layers, materials and real model");
        auto json=app::boardToJson(board);pcb::Board restored;QString error;
        require(app::boardFromJson(restored,json,&error),"material/mesh board roundtrip");
        require(restored.footprints[0].texturePng==board.footprints[0].texturePng && restored.layers.size()==3,"texture and layers persist");
        auto malformed=json;auto footprints=malformed["footprints"].toArray();auto f=footprints[0].toObject();f["modelScale"]=-1;footprints[0]=f;malformed["footprints"]=footprints;
        require(!app::boardFromJson(restored,malformed,&error) && restored.layers.size()==3,"reject invalid model without changing board");
        auto result=app::previewCommands(schematic,board,QJsonArray{QJsonObject{{"op","board_settings"},{"mask_color","#4477aa"}},QJsonObject{{"op","run_shell"},{"command","anything"}}},"Author");
        require(!result.success && result.board.solderMask==board.solderMask,"commands are atomic and reject executable code");
        result=app::previewCommands(schematic,board,QJsonArray{QJsonObject{{"op","board_settings"},{"mask_color","#4477aa"}},QJsonObject{{"op","add_layer"},{"index",3},{"name","Power"},{"color","#aa88dd"}}},"Author");
        require(result.success && result.board.layers.size()==4,"valid commands can customize board and layers");
        auto plan=QString::fromUtf8(QJsonDocument(QJsonObject{{"version",1},{"commands",QJsonArray{QJsonObject{{"op","sync_pcb"}}}}}).toJson(QJsonDocument::Compact));
        auto openai=app::aiProposal("openai",{{"output",QJsonArray{QJsonObject{{"content",QJsonArray{QJsonObject{{"type","output_text"},{"text",plan}}}}}}}});
        auto groq=app::aiProposal("groq",{{"choices",QJsonArray{QJsonObject{{"message",QJsonObject{{"content",plan}}}}}}});
        auto claude=app::aiProposal("claude",{{"content",QJsonArray{QJsonObject{{"type","text"},{"text",plan}}}}});
        require(openai["version"]==1 && groq["version"]==1 && claude["version"]==1,"all provider response formats");
        require(app::aiRequest("groq","user-model","prompt",{}).contains("messages"),"Groq uses chat completions");
        require(app::aiRequest("openai","user-model","prompt",{})["store"]==false,"OpenAI does not store response");
        QTemporaryDir dir;QFile plugin(dir.filePath("test.levelplugin.json"));require(plugin.open(QIODevice::WriteOnly),"plugin fixture");
        plugin.write(R"({"format":"levelcad-plugin","apiVersion":1,"id":"native.sample","name":"Sample","actions":[{"name":"Sync","commands":[{"op":"sync_pcb"}]}]})");plugin.close();
        auto plugins=app::discoverPlugins(dir.path());require(plugins.size()==1 && plugins[0].actions.size()==1,"plugin discovery");
        auto models=app::bundledModels();require(models.size()>=35,"downloaded component models bundled");
        std::size_t triangles=0;for(const auto& model:models) {auto mesh=app::loadStl(":/models3d/"+model);require(!mesh.empty(),"bundled model must parse");triangles+=mesh.size();}
        core::Document large;
        for(int i=0;i<500;++i) large.addSymbol(core::SymbolKind::Resistor,{(i%25)*10000000LL,(i/25)*10000000LL});
        pcb::Board largeBoard;largeBoard.width=largeBoard.height=400000000;
        QElapsedTimer timer;timer.start();largeBoard.synchronize(large);auto firstMs=timer.elapsed();
        timer.restart();largeBoard.synchronize(large);auto secondMs=timer.elapsed();
        require(largeBoard.footprints.size()==500,"large design sync");
        std::cout<<"35+ models / "<<triangles<<" triangles; 500-component sync "<<firstMs<<" ms, resync "<<secondMs<<" ms\n";
        std::cout<<"Footprints, compact placement, shared meshes, materials, AI codecs, atomic commands and plugins passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
