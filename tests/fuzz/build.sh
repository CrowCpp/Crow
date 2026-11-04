mkdir -p build && cd build
cmake .. -DCROW_BUILD_FUZZER=ON -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=
cmake --build . --target install
make -j$(nproc)
cd ../tests/fuzz
zip -q $OUT/template_fuzzer_seed_corpus.zip template_corpus/*
zip -q $OUT/request_fuzzer_seed_corpus.zip html_corpus/*
zip -q $OUT/json_fuzzer_seed_corpus.zip json_corpus/*
cp json_fuzzer.dict $OUT/json_fuzzer.dict

