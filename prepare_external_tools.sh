#!/bin/bash
source utils.sh
OS=$(uname)
REPO_DIR="$(cd "$(dirname "$0")"; pwd)"

submodules=("external/GCIS" "external/ShapeSlp" "external/sdsl-lite" "external/malloc_count") 

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

if [ ! -d "external/sdsl-lite/build/include" ]; then
    echo -e "\n\t${GREEN}####### Instalando a SDSL-lite ${RESET}"
    cp external-overides/sdsl/include/sdsl/louds_tree.hpp external/sdsl-lite/include/sdsl/louds_tree.hpp
    if [ "$OS" = "Darwin" ]; then
        echo "Substituindo o arquivo de instalação da SDSL para funcionamento correto no Mac(arm64)"
        cp external-overrides/sdsl/install.sh external/sdsl-lite/
    fi
    cd external/sdsl-lite
    chmod +x install.sh
    ./install.sh
else
    echo -e "${BLUE}## SDSL já configurada. ${RESET}"
fi


if [ ! -d "external/ShapeSl/SubstrBenchmark" ]; then
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
else
    echo -e "${BLUE}## ShapeSlp já configurada. ${RESET}"
fi

if [ ! -d "external/GCIS/build/src/gc-is-codec" ]; then
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
else
    echo -e "${BLUE}## GCIS já configurado. ${RESET}"
fi

if [ ! -d "external/GCIS/external/repair/build/src/repair" ]; then
    echo -e "\n\t${GREEN}####### Compilando Re-Pair ${RESET}"
    cp -rf external-overrides/repair_files/* external/GCIS/external/repair/src
    cd external/GCIS/external/repair
    rm -rf build
    mkdir build
    cd build
    if [ "$OS" = "Darwin" ]; then
        echo "Compilando para Mac (arm64)"
        cmake -DCMAKE_OSX_ARCHITECTURES=arm64 ..
    else
        echo "Compilando para Linux"
        cmake ..
    fi
    make
else
    echo -e "${BLUE}## SDSL já configurada. ${RESET}"
fi