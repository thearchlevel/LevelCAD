# PCB e inspecao 3D

A sincronizacao transfere componentes fisicos, numeros dos pads e redes do esquema.
Ela preserva as trilhas existentes. Uma ligacao do esquema indica conectividade;
sua geometria nao deve ser copiada como cobre sem verificar distancias e obstaculos.

## Fluxo de trabalho

1. Atribua um encapsulamento real a cada componente.
2. Fontes ideais de simulacao precisam de um conector: PCB > Fonte com conector fisico.
3. Sincronize a PCB. Organizar como o esquematico reaplica a disposicao e ajusta
   o retangulo da placa. Ajustar contorno ao circuito preserva contornos personalizados.
4. Use Verificar esquema x PCB para conferir pinos, pads, valores e redes.
5. Roteie as redes. Rotear ligacoes simples procura caminhos ortogonais na camada
   superior sem invadir pads, vias ou trilhas de outras redes. Os casos sem caminho
   valido continuam pendentes e precisam de roteamento manual.
6. Execute o DRC. Linhas pontilhadas sao ligacoes pendentes, nao trilhas fabricaveis.

## Visualizador 3D

A barra PCB 3D oferece placa montada, camadas separadas, somente cobre e placa
transparente. Componentes e Cobre podem ser ocultados separadamente. Superior,
Inferior e Isometrica mudam a camera. As caixas do painel Camadas e redes PCB
controlam a visibilidade das camadas nos editores 2D e 3D.

O cobre usa largura, camada e posicao das trilhas reais. Pads mostram tamanho,
forma, rotacao, acabamento e indicacao de furacao; SMD nao recebe furos.
Vias mostram anel e furacao. No modo separado, as camadas aparecem identificadas.
O rodape informa trilhas, vias e ligacoes sem cobre.

A renderizacao usa profundidade por pixel, com materiais dos modelos oficiais,
para evitar faces atravessando a placa. A visualizacao de furos usa uma abertura
escura; ainda nao representa a parede metalizada nem simula volumes de solda.
Cores de cobre servem para inspecao: nao simulam a translucidez fisica da mascara.

## Pacote de fabricacao

PCB > Exportar pacote de fabricacao pede projeto, revisao e autor. O gerador
CLI instalado e detectado automaticamente; tambem pode ser configurado no formulario.
Nesta implementacao, a geracao Gerber/Excellon usa kicad-cli. Ele precisa estar
instalado para esta exportacao; editar a placa e inspecionar o 3D nao dependem dele.

O pacote inclui Gerber X2 das camadas de cobre, mascara, pasta, serigrafia e
contorno; arquivos Excellon de furacao; BOM; coordenadas de posicionamento; placa
nativa; projeto nativo com esquema e PCB; placa de intercambio; manifest.json
com autor, revisao, data UTC e SHA-256 dos arquivos. Coordenadas do CSV usam
milimetros, X para direita, Y para baixo e rotacao horaria. Confirme a convencao
do montador antes de usar o arquivo de posicionamento.

A exportacao bloqueia ligacoes sem trilha, divergencias entre esquema e pads,
erros encontrados pelo DRC e geometrias que nao podem ser exportadas fielmente.
Os arquivos sao preparados numa pasta temporaria; um pacote so aparece completo
se todas as etapas forem concluidas. Pastas existentes nao sao sobrescritas.

## Limites atuais

A exportacao suporta placa retangular, pads numericos simples, vias passantes,
trilhas e montagem superior. Contornos personalizados, pads especiais, furos
oblongos, zonas de cobre e montagem inferior completa ainda precisam de suporte
dedicado. Nao e um substituto validado para qualquer projeto industrial.

O DRC atual nao cobre todas as regras de fabricacao, impedancia, termica e
montagem. Revise Gerbers e furos em um visualizador independente e confirme
larguras, distancias, anel anular e empilhamento com o fabricante antes de pedir
uma placa. Os testes de exportacao confirmam formatos e conectividade do
circuito de verificacao, nao uma certificacao de todos os projetos possiveis.