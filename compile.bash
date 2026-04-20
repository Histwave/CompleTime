#!/bin/bash
echo "building CompleTime pass"
rm -f pass.so
clang++ -Wno-c++17-extensions -fPIC -shared -o pass.so \
    ctllvm.cpp ComplexityAnalysisPass.cpp \
    `llvm-config --cxxflags --ldflags --system-libs --libs core passes`
echo "Built Pass Plugin"
clang -O0 -Xclang -disable-O0-optnone -g \
    -fno-inline-functions -fno-unroll-loops \
    -fpass-plugin=./pass.so \
    main.c -S -o main.s
