#include <iostream>
#include <dlfcn.h>
#include "../include/engine.hpp"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ./quant_runner <strategy.so> <ticks.csv>" << std::endl;
        return 1;
    }
    
    void* handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) {
        std::cerr << "Cannot load library: " << dlerror() << std::endl;
        return 1;
    }
    auto create_strategy = (csot::Strategy* (*)())dlsym(handle, "create_strategy");
    if (!create_strategy) {
        std::cerr << "Cannot find create_strategy: " << dlerror() << std::endl;
        return 1;
    }

    csot::Strategy* strategy = create_strategy();

    csot::Engine engine;
    engine.load_ticks(argv[2]);

    engine.run(*strategy);

    delete strategy;
    dlclose(handle);

    return 0;
}
