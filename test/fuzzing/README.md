Fuzzing test for QAT ZSTD Plugin
==============================

Zstrandard*(ZSTD*) provides an interface for external sequence producer to do fuzzing test: [`fuzz_third_party_seq_prod.h`][1] after [1.5.5][2].

**Steps to run fuzzing test for QAT ZSTD Plugin**

```bash
    # Compile qatseqprodfuzzer.o with clang
    cd test/fuzzing
    make qatseqprodfuzzer.o

    # Build and run fuzzing targets
    git clone https://github.com/facebook/zstd.git
    cd tests/fuzz
    make corpora
    python3 ./fuzz.py build all --custom-seq-prod=*/qatseqprodfuzzer.o --enable-fuzzer --enable-asan --enable-ubsan --cc clang --cxx clang++ --ldflags=-lqat_s
    python3 ./fuzz.py libfuzzer simple_round_trip
    python3 ./fuzz.py libfuzzer stream_round_trip
    python3 ./fuzz.py libfuzzer dictionary_round_trip
    python3 ./fuzz.py libfuzzer block_round_trip
    python3 ./fuzz.py libfuzzer decompress_dstSize_tooSmall
    python3 ./fuzz.py libfuzzer dictionary_decompress
    python3 ./fuzz.py libfuzzer dictionary_loader
    python3 ./fuzz.py libfuzzer dictionary_stream_round_trip
    python3 ./fuzz.py libfuzzer raw_dictionary_round_trip
    python3 ./fuzz.py libfuzzer sequence_compression_api
    python3 ./fuzz.py libfuzzer simple_compress
```

[1]:https://github.com/facebook/zstd/blob/dev/tests/fuzz/fuzz_third_party_seq_prod.h
[2]:https://github.com/facebook/zstd/releases/tag/v1.5.5