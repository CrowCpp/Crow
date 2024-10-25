cd $SRC/crow
mkdir -p build
cmake -S . -B build -DCROW_BUILD_FUZZER=ON -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF && cmake --build build --target install

# Build the corpora
cd tests/fuzz
zip -q $OUT/template_fuzzer_seed_corpus.zip template_corpus/*
zip -q $OUT/request_fuzzer_seed_corpus.zip html_corpus/*
