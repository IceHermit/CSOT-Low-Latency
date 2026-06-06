#include <iostream>
#include <dlfcn.h>
#include "../include/engine.hpp"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ./quant_runner <strategy.so> <ticks.csv>" << std::endl;
        return 1;
    }

    // 1. Load the strategy library
    void* handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) {
        std::cerr << "Cannot load library: " << dlerror() << std::endl;
        return 1;
    }

    // 2. Get the factory function
    auto create_strategy = (csot::Strategy* (*)())dlsym(handle, "create_strategy");
    if (!create_strategy) {
        std::cerr << "Cannot find create_strategy: " << dlerror() << std::endl;
        return 1;
    }

    // 3. Instantiate the strategy
    csot::Strategy* strategy = create_strategy();

    // 4. Initialize the engine and load data
    csot::Engine engine;
    engine.load_ticks(argv[2]);

    // 5. Run the engine
    engine.run(*strategy);

    // 6. Cleanup
    delete strategy;
    dlclose(handle);

    return 0;
}