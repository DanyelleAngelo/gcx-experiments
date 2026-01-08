  665  git checkout 7353057a59ff7ce3f5f45a545d085c9468c3b151
  666  cd ..
  667  nano prepare_external_tools.sh 
  668  cp -rf external-overrides/gcis/* external/GCIS/
  669  cd external/GCIS/
  670  ../sdsl-lite/./install.sh 
  671  rm -rf build
  672  chmod +x build.sh 
  673  ./build.sh 
  674  nano ../../external-overrides/gcis/src/gc-is-codec.cpp 
  675  rm -rf build
  676  ./build.sh 
  677  ls build/src/gcis
  678  cd ..
  679  nano measure_compression.sh 
  680  ./measure_compression.sh 