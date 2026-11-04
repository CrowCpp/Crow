mkdir -p build && cd build
cmake .. -DCROW_BUILD_FUZZER=ON -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=ON
cmake --build . --target install
make -j$(nproc)
cd ../tests/fuzz
zip -q $OUT/template_fuzzer_seed_corpus.zip template_corpus/*
zip -q $OUT/request_fuzzer_seed_corpus.zip html_corpus/*
zip -q $OUT/query_string_fuzzer_seed_corpus.zip query_corpus/*
cp query_string_fuzzer.dict $OUT/query_string_fuzzer.dict
