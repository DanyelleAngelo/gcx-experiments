#!/bin/bash
source utils.sh
OS=$(uname)
REPO_DIR="$(cd "$(dirname "$0")"; pwd)"
echo "\n\t${GREEN}$REPO_DIR\n\t${GREEN}"
SOURCE_DIR=$(pwd)

submodules=("external/malloc_count" "external/GCIS" "external/ShapeSlp"  "external/sdsl-lite" "external/7zip" "external/GLZA") 

need_init=0

for path in "${submodules[@]}"; do
    if [ ! -d "$path/.git" ]; then
        echo -e "Submódulo '$path' não está inicializado."
        need_init=1
    fi
done

if [ $need_init -eq 1 ]; then
    echo -e "\n Inicializando submódulos ausentes..."
    git submodule update --init --recursive
else
    echo -e "Todos os submódulos já estão prontos."
fi

# if [ ! -d "external/sdsl-lite/build/include" ]; then
#     echo -e "\n\t${GREEN}####### Instalando a SDSL-lite ${RESET}"
#     cp external-overides/sdsl/include/sdsl/louds_tree.hpp external/sdsl-lite/include/sdsl/louds_tree.hpp
#     if [ "$OS" = "Darwin" ]; then
#         echo "Substituindo o arquivo de instalação da SDSL para funcionamento correto no Mac(arm64)"
#         cp external-overrides/sdsl/install.sh external/sdsl-lite/
#     fi
#     cd external/sdsl-lite
#     chmod +x install.sh
#     ./install.sh

#     cd $SOURCE_DIR
# else
#     echo -e "${BLUE}## SDSL já configurada. ${RESET}"
# fi

if [ ! -d "../GCX/" ]; then
    echo -e "\n\t${GREEN}####### Clonando GCX ${RESET}"
    git clone "git@github.com:DanyelleAngelo/GCX.git" ../
else
    echo -e "${BLUE}## GCX já foi adicionado. ${RESET}"
fi


if [ ! -d "external/ShapeSl/build/SlpEncBuild" ]; then
    echo -e "\n\t${GREEN}####### ShapeSlp: Build não encontrado, configurando a lib${RESET}"

    cp -rf external-overrides/shapedslsp/* external/ShapeSlp/
    cd external/ShapeSlp
    git submodule update --init --recursive
    if [ "$(uname -s)" = "Darwin" ]; then
        # o código atual dessa biblioteca quebra no M1 porque __haswell__ não é definida pelo complicador
        echo -e "${RED}Fazendo ajustes necessários para o funcionamento da lib no M1 ${RESET}"
        cp ../../external-overrides/sux/common.hpp external/sux/sux/support/common.hpp
        cp ../../external-overrides/sux/RecSplit.hpp external/sux/sux/function/RecSplit.hpp
    fi
    mkdir -p build
    cd build
    cmake -DSDSL_INCLUDE_DIR="$REPO_DIR/external/sdsl-lite/include"  -DSDSL_LIB="$REPO_DIR/external/sdsl-lite/build/lib/libsdsl.a" ..
    make

    cd $SOURCE_DIR
else
    echo -e "${BLUE}## ShapeSlp já configurada. ${RESET}"
fi

if [ ! -d "external/GCIS/build/src/gcis" ]; then
    echo -e "\n\t${GREEN}#######  Configurando o GCIS...... ${RESET}"

    echo -e "\n\t####### Copiando arquivos necessários para os experimentos...."
    cp -rf external-overrides/gcis/* external/GCIS/

    echo -e "\n\t####### Instalando o GCIS....."
    cd external/GCIS
    if [ "$OS" = "Darwin" ]; then
        echo "Compilando para Mac (arm64)"
        chmod +x build-mac.sh
        ./build-mac.sh
    else
        ./build.sh
    fi
    cd $SOURCE_DIR
else
    echo -e "${BLUE}## GCIS já configurado. ${RESET}"
fi


if [ ! -f "external/7zip/CPP/7zip/Bundles/Alone2/_o/7zz" ]; then
    echo -e "\n\t${GREEN}####### Compilando  7Zip ${RESET}"
    cp -rf external-overrides/7zip/* external/7zip/
    cd external/7zip/CPP/7zip/Bundles/Alone2
    make -f makefile.gcc
    cd $SOURCE_DIR
else
    echo -e "${BLUE}## 7zip já configurada. ${RESET}"
fi

if [ ! -f "external/GLZA/glza_compress" ] && [ ! -f "external/GLZA/glza_decompress" ]; then
    echo -e "\n\t${GREEN}####### Compilando  GLZA ${RESET}"
    cp -rf external-overrides/glza/* external/GLZA/
    cd external/GLZA
    gcc -o GLZAformat GLZAformat.c -lpthread
    gcc -o GLZAcompress GLZAcompress.c  -lpthread -lm
    gcc -o GLZAencode GLZAencode.c  -lpthread
    gcc -o GLZAdecode GLZAdecode.c -lpthread
    cd $SOURCE_DIR

else
    echo -e "${BLUE}## GLZA já configurada. ${RESET}"
fi