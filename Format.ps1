$files = git ls-files '*.cpp' '*.cc' '*.cxx' '*.c' '*.hpp' '*.hh' '*.hxx' '*.h' '*.inl' '*.ipp'

foreach ($file in $files) {
    clang-format -i --style=file --fallback-style=LLVM $file
}
